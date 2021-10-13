/*
	Copyright (C) 2008 - 2021
	by Mark de Wever <koraq@xs4all.nl>
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

#define GETTEXT_DOMAIN "wesnoth-lib"

#include "font/text.hpp"

#include "font/font_config.hpp"

#include "font/pango/escape.hpp"
#include "font/pango/font.hpp"
#include "font/pango/hyperlink.hpp"
#include "font/pango/stream_ops.hpp"

#include "gettext.hpp"
#include "gui/widgets/helper.hpp"
#include "gui/core/log.hpp"
#include "sdl/point.hpp"
#include "sdl/utils.hpp"
#include "serialization/string_utils.hpp"
#include "serialization/unicode.hpp"
#include "preferences/general.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/range/adaptor/transformed.hpp>

#include <cassert>
#include <cstring>
#include <stdexcept>

static void add_span(std::vector<PangoLayout::span> &spans, PangoLayout::span span)
{
	boost::algorithm::trim_if(span.text, isblank);
	if (!span.text.empty())
	{
		std::vector<std::string> blocks;
		boost::split(blocks, std::move(span.text), boost::is_any_of("\n\r"));

		spans.reserve(spans.size() + blocks.size());
		for (auto &block : blocks)
		{
			spans.push_back(span);
			spans.back().text = std::move(block);
			spans.back().linebreak = (&block != &blocks.back());
		}
	}
}

void PangoLayout::set_text(std::string s)
{
	lines.clear();
	spans.clear();
	add_span(spans, {s});
}

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
namespace pt = boost::property_tree;

static void walk_ptree(pt::ptree &node, PangoLayout::span &span, const std::function<void(pt::ptree::value_type &entry, PangoLayout::span &span)> &callback)
{
	for (auto &entry : node)
	{
		PangoLayout::span newspan;
		if (entry.first == "span")
		{
			newspan = span;
			span = newspan;
		}
		else
			callback(entry, span);

		walk_ptree(entry.second, span, callback);
	}
}

void PangoLayout::set_markup(const std::string &s)
{
	lines.clear();
	spans.clear();

	pt::ptree tree;
	std::istringstream stream("<xml>" + s + "</xml>");
	pt::read_xml(stream, tree, pt::xml_parser::no_concat_text);

	PangoLayout::span span;
	walk_ptree(tree, span, [&](auto &entry, PangoLayout::span &span)
	{
		if (entry.first == "color")
		{
			std::string col = std::move(entry.second.data());
			if (col[0] == '#')
				col.erase(0, 1);

			if (col.size() == 3)
			{
				col.resize(6);
				col[5] = col[4] = col[2];
				col[3] = col[2] = col[1];
				col[1] = col[0];
			}

			assert(col.size() == 6);
			span.color = color_t::from_hex_string(col);
		}
		else
		if (entry.first == "<xmltext>")
		{
			span.text = std::move(entry.second.data());
			add_span(spans, span);
		}
	});
}

static bool pango_parse_markup(const std::string_view &s)
{
	try 
	{
		PangoLayout layout;
		layout.set_markup(std::string(s));
		return true;
	}
	catch (...)
	{
		return false;
	}
}

namespace font {

pango_text::pango_text()
	: layout_()
	, rect_()
	, surface_()
	, text_()
	, markedup_text_(false)
	, link_aware_(false)
	, link_color_()
	, font_class_(font::FONT_SANS_SERIF)
	, font_size_(14)
	, font_style_(STYLE_NORMAL)
	, foreground_color_() // solid white
	, add_outline_(false)
	, maximum_width_(-1)
	, characters_per_line_(0)
	, maximum_height_(-1)
	, ellipse_mode_(PANGO_ELLIPSIZE_END)
	, alignment_(PANGO_ALIGN_LEFT)
	, maximum_length_(std::string::npos)
	, calculation_dirty_(true)
	, length_(0)
	, surface_dirty_(true)
	, rendered_viewport_()
{
	/*
	 * Set the pango spacing a bit bigger since the default is deemed to small
	 * https://www.wesnoth.org/forum/viewtopic.php?p=358832#p358832
	 */
	layout_.spacing = 4;
}

surface& pango_text::render(const SDL_Rect& viewport)
{
	rerender(viewport);
	return surface_;
}

surface& pango_text::render()
{
	recalculate();
	auto viewport = SDL_Rect{0, 0, rect_.x + rect_.width, rect_.y + rect_.height};
	rerender(viewport);
	return surface_;
}

int pango_text::get_width() const
{
	return this->get_size().x;
}

int pango_text::get_height() const
{
	return this->get_size().y;
}

point pango_text::get_size() const
{
	this->recalculate();

	return point(rect_.width, rect_.height);
}

bool pango_text::is_truncated() const
{
	this->recalculate();

	return false;
}

unsigned pango_text::insert_text(const unsigned offset, const std::string& text)
{
	if (text.empty() || length_ == maximum_length_) {
		return 0;
	}

	// do we really need that assert? utf8::insert will just append in this case, which seems fine
	assert(offset <= length_);

	unsigned len = utf8::size(text);
	if (length_ + len > maximum_length_) {
		len = maximum_length_ - length_;
	}
	const std::string insert = text.substr(0, utf8::index(text, len));
	std::string tmp = text_;
	this->set_text(utf8::insert(tmp, offset, insert), false);
	// report back how many characters were actually inserted (e.g. to move the cursor selection)
	return len;
}

point pango_text::get_cursor_position(
		const unsigned column, const unsigned line) const
{
	this->recalculate();
	// $TODO
	return point();
}

std::size_t pango_text::get_maximum_length() const
{
	return maximum_length_;
}

std::string pango_text::get_token(const point & position, const char * delim) const
{
	this->recalculate();
	// $TODO
	return ""; // if the index is out of bounds, or the index character is a delimiter, return nothing
}

std::string pango_text::get_link(const point & position) const
{
	if (!link_aware_) {
		return "";
	}

	std::string tok = this->get_token(position, " \n\r\t");

	if (looks_like_url(tok)) {
		return tok;
	} else {
		return "";
	}
}

point pango_text::get_column_line(const point& position) const
{
	this->recalculate();
	// $TODO
	return point();
}

bool pango_text::set_text(const std::string& text, const bool markedup)
{
	if(markedup != markedup_text_ || text != text_) {

		const std::u32string wide = unicode_cast<std::u32string>(text);
		const std::string narrow = unicode_cast<std::string>(wide);
		if(text != narrow) {
			ERR_GUI_L << "pango_text::" << __func__
					<< " text '" << text
					<< "' contains invalid utf-8, trimmed the invalid parts.\n";
		}
		if(markedup) {
			if(!this->set_markup(narrow, layout_)) {
				return false;
			}
		} else {
			/*
			 * pango_layout_set_text after pango_layout_set_markup might
			 * leave the layout in an undefined state regarding markup so
			 * clear it unconditionally.
			 */
			layout_.set_text(narrow);
		}
		text_ = narrow;
		length_ = wide.size();
		markedup_text_ = markedup;
		calculation_dirty_ = true;
		surface_dirty_ = true;
	}

	return true;
}

pango_text& pango_text::set_family_class(font::family_class fclass)
{
	if(fclass != font_class_) {
		font_class_ = fclass;
		calculation_dirty_ = true;
		surface_dirty_ = true;
	}

	return *this;
}

pango_text& pango_text::set_font_size(const unsigned font_size)
{
	unsigned int actual_size = preferences::font_scaled(font_size);
	if(actual_size != font_size_) {
		font_size_ = actual_size;
		calculation_dirty_ = true;
		surface_dirty_ = true;
	}

	return *this;
}

pango_text& pango_text::set_font_style(const pango_text::FONT_STYLE font_style)
{
	if(font_style != font_style_) {
		font_style_ = font_style;
		calculation_dirty_ = true;
		surface_dirty_ = true;
	}

	return *this;
}

pango_text& pango_text::set_foreground_color(const color_t& color)
{
	if(color != foreground_color_) {
		foreground_color_ = color;
		surface_dirty_ = true;
	}

	return *this;
}

pango_text& pango_text::set_maximum_width(int width)
{
	if(width <= 0) {
		width = -1;
	}

	if(width != maximum_width_) {
		maximum_width_ = width;
		calculation_dirty_ = true;
		surface_dirty_ = true;
	}

	return *this;
}

pango_text& pango_text::set_characters_per_line(const unsigned characters_per_line)
{
	if(characters_per_line != characters_per_line_) {
		characters_per_line_ = characters_per_line;

		calculation_dirty_ = true;
		surface_dirty_ = true;
	}

	return *this;
}

pango_text& pango_text::set_maximum_height(int height, bool multiline)
{
	if(height <= 0) {
		height = -1;
		multiline = false;
	}

	if(height != maximum_height_) {
		maximum_height_ = height;
		calculation_dirty_ = true;
		surface_dirty_ = true;
	}

	return *this;
}

pango_text& pango_text::set_ellipse_mode(const PangoEllipsizeMode ellipse_mode)
{
	if(ellipse_mode != ellipse_mode_) {
		ellipse_mode_ = ellipse_mode;
		calculation_dirty_ = true;
		surface_dirty_ = true;
	}

	return *this;
}

pango_text &pango_text::set_alignment(const PangoAlignment alignment)
{
	if (alignment != alignment_) {
		alignment_ = alignment;
		surface_dirty_ = true;
	}

	return *this;
}

pango_text& pango_text::set_maximum_length(const std::size_t maximum_length)
{
	if(maximum_length != maximum_length_) {
		maximum_length_ = maximum_length;
		if(length_ > maximum_length_) {
			std::string tmp = text_;
			this->set_text(utf8::truncate(tmp, maximum_length_), false);
		}
	}

	return *this;
}

pango_text& pango_text::set_link_aware(bool b)
{
	if (link_aware_ != b) {
		calculation_dirty_ = true;
		surface_dirty_ = true;
		link_aware_ = b;
	}
	return *this;
}

pango_text& pango_text::set_link_color(const color_t& color)
{
	if(color != link_color_) {
		link_color_ = color;
		calculation_dirty_ = true;
		surface_dirty_ = true;
	}

	return *this;
}

pango_text& pango_text::set_add_outline(bool do_add)
{
	if(do_add != add_outline_) {
		add_outline_ = do_add;
		//calculation_dirty_ = true;
		surface_dirty_ = true;
	}

	return *this;
}

int pango_text::get_max_glyph_height() const
{
	p_font font{ get_font_families(font_class_), font_size_, font_style_ };

	TTF_Font* f = font.get();

	auto ascent = TTF_FontAscent(f);
	auto descent = TTF_FontDescent(f);

	return ascent + descent;
}

void pango_text::recalculate() const
{
	if(calculation_dirty_) {
		calculation_dirty_ = false;
		surface_dirty_ = true;

		rect_ = calculate_size(layout_);
	}
}

PangoRectangle pango_text::calculate_size(PangoLayout& layout) const
{
	PangoRectangle size = {};

	p_font font{get_font_families(font_class_), font_size_, font_style_};

	int word_spacing;
	TTF_SizeUTF8(font.get(), " ", &word_spacing, nullptr);

	int maximum_width = 0;
	if(characters_per_line_ != 0) {
		maximum_width = word_spacing * characters_per_line_;
	} else {
		maximum_width = maximum_width_;
	}

	if(maximum_width_ != -1) {
		maximum_width = std::min(maximum_width, maximum_width_);
	}

	layout.lines.clear();
	layout.lines.push_back({});
	for(auto& span : layout.spans) {
		std::vector<std::string> words;
		boost::split(words, span.text, boost::is_any_of(" "));

		for(auto& word : words) {
			TTF_SizeUTF8(font.get(), word.c_str(), &size.width, &size.height);
			if(maximum_width != -1 && size.x + size.width > maximum_width) {
				size.x = 0;
				size.y += size.height + layout.spacing;
				layout.lines.push_back({});
			}

			PangoLayout::word w{{std::move(word), span.color}, reinterpret_cast<const SDL_Rect&>(size)};
			size.x += size.width + word_spacing;
			layout.lines.back().push_back(w);
		}

		if (span.linebreak && &span != &layout.spans.back())
		{
			size.x = 0;
			size.y += size.height + layout.spacing;
			layout.lines.push_back({});
		}
	}

	if(alignment_ == PANGO_ALIGN_RIGHT && maximum_width != -1) {
		for(auto& line : layout.lines) {
			int diff = maximum_width - (line.back().bounds.x + line.back().bounds.w);
			for(auto& word : line)
				word.bounds.x += diff;
		}
	} else if(alignment_ == PANGO_ALIGN_CENTER && maximum_width != -1) {
		for(auto& line : layout.lines) {
			int diff = (maximum_width - (line.back().bounds.x + line.back().bounds.w)) / 2;
			for(auto& word : line)
				word.bounds.x += diff;
		}
	}

	SDL_Rect bounds = {};
	for(auto& line : layout.lines)
		for(auto& word : line)
			SDL_UnionRect(&word.bounds, &bounds, &bounds);

	size = reinterpret_cast<const PangoRectangle&>(bounds);

	DBG_GUI_L << "pango_text::" << __func__
		<< " text '" << gui2::debug_truncate(text_)
		<< "' maximum_width " << maximum_width
		<< " width " << size.x + size.width
		<< ".\n";

	DBG_GUI_L << "pango_text::" << __func__
		<< " text '" << gui2::debug_truncate(text_)
		<< "' font_size " << font_size_
		<< " markedup_text " << markedup_text_
		<< " font_style " << std::hex << font_style_ << std::dec
		<< " maximum_width " << maximum_width
		<< " maximum_height " << maximum_height_
		<< " result " << size
		<< ".\n";
	if(maximum_width != -1 && size.x + size.width > maximum_width) {
		DBG_GUI_L << "pango_text::" << __func__
			<< " text '" << gui2::debug_truncate(text_)
			<< " ' width " << size.x + size.width
			<< " greater as the wanted maximum of " << maximum_width
			<< ".\n";
	}

	return size;
}

void pango_text::render(PangoLayout& layout, const SDL_Rect& viewport, const unsigned stride)
{
	p_font font{ get_font_families(font_class_), font_size_, font_style_ };

	for (auto &line : layout.lines) 
	{
		for (auto &word : line) 
		{
			surface rendered = TTF_RenderUTF8_Blended(font.get(), word.text.c_str(), (foreground_color_ * word.color).to_sdl());
			SDL_Rect srcrect = {0, 0, word.bounds.w, word.bounds.h};
			SDL_BlitSurface(rendered.get(), &srcrect, surface_.get(), &word.bounds);
		}
	}
}

void pango_text::rerender(const SDL_Rect& viewport)
{
	if(surface_dirty_ || !SDL_RectEquals(&rendered_viewport_, &viewport)) {
		this->recalculate();
		surface_dirty_ = false;
		rendered_viewport_ = viewport;

		const int stride = viewport.w * 4;
		if(stride <= 0 || viewport.h <= 0) {
			surface_ = surface(0, 0);
			return;
		}

		// Check to prevent arithmetic overflow when calculating (stride * height).
		// The size of the viewport should already provide a far lower limit on the
		// maximum size, but this is left in as a sanity check.
		if(viewport.h > std::numeric_limits<int>::max() / stride) {
			throw std::length_error("Text is too long to render");
		}

		// Resize buffer appropriately and set all pixel values to 0.
		surface_ = SDL_CreateRGBSurfaceWithFormat(
			0, viewport.w, viewport.h, 32, SDL_PIXELFORMAT_ARGB8888);

		// Try rendering the whole text in one go. If this throws a length_error
		// then leave it to the caller to handle; one reason it may throw is that
		// cairo surfaces are limited to approximately 2**15 pixels in height.
		render(layout_, viewport, stride);
	}
}

bool pango_text::set_markup(std::string_view text, PangoLayout& layout) {
	std::string semi_escaped;
	bool valid = validate_markup(text, semi_escaped);
	if(semi_escaped != "") {
		text = semi_escaped;
	}

	if(valid) {
		if(link_aware_) {
			std::string formatted_text = format_links(text);
			layout.set_markup(formatted_text);
		} else {
			layout.set_markup(std::string(text));
		}
	} else {
		ERR_GUI_L << "pango_text::" << __func__
			<< " text '" << text
			<< "' has broken markup, set to normal text.\n";
		set_text(_("The text contains invalid Pango markup: ") + std::string(text), false);
	}

	return valid;
}

/**
 * Replaces all instances of URLs in a given string with formatted links
 * and returns the result.
 */
std::string pango_text::format_links(std::string_view text) const
{
	static const std::string delim = " \n\r\t";
	std::ostringstream result;

	std::size_t tok_start = 0;
	for(std::size_t pos = 0; pos < text.length(); ++pos) {
		if(delim.find(text[pos]) == std::string::npos) {
			continue;
		}

		if(const auto tok_length = pos - tok_start) {
			// Token starts from after the last delimiter up to (but not including) this delimiter
			auto token = text.substr(tok_start, tok_length);
			if(looks_like_url(token)) {
				result << format_as_link(std::string{token}, link_color_);
			} else {
				result << token;
			}
		}

		result << text[pos];
		tok_start = pos + 1;
	}

	// Deal with the remainder token
	if(tok_start < text.length()) {
		auto token = text.substr(tok_start);
		if(looks_like_url(token)) {
			result << format_as_link(std::string{token}, link_color_);
		} else {
			result << token;
		}
	}

	return result.str();
}

bool pango_text::validate_markup(std::string_view text, std::string& semi_escaped) const
{
	if(pango_parse_markup(text)) {
		return true;
	}

	/*
	 * The markup is invalid. Try to recover.
	 *
	 * The pango engine tested seems to accept stray single quotes »'« and
	 * double quotes »"«. Stray ampersands »&« seem to give troubles.
	 * So only try to recover from broken ampersands, by simply replacing them
	 * with the escaped version.
	 */
	semi_escaped = semi_escape_text(std::string(text));

	/*
	 * If at least one ampersand is replaced the semi-escaped string
	 * is longer than the original. If this isn't the case then the
	 * markup wasn't (only) broken by ampersands in the first place.
	 */
	if(text.size() == semi_escaped.size()
			|| !pango_parse_markup(semi_escaped)) {

		/* Fixing the ampersands didn't work. */
		return false;
	}

	/* Replacement worked, still warn the user about the error. */
	WRN_GUI_L << "pango_text::" << __func__
			<< " text '" << text
			<< "' has unescaped ampersands '&', escaped them.\n";

	return true;
}

void pango_text::copy_layout_properties(PangoLayout& src, PangoLayout& dst)
{
}

std::vector<std::string> pango_text::get_lines() const
{
	this->recalculate();

	int count = layout_.lines.size();
	if(count < 1) {
		return {};
	}

	std::vector<std::string> res(count);
	for (auto &line : layout_.lines) 
		res.back() = boost::join(line | boost::adaptors::transformed([](const auto &word){ return word.text; }), " ");

	return res;
}

pango_text& get_text_renderer()
{
	static pango_text text_renderer;
	return text_renderer;
}

int get_max_height(unsigned size, font::family_class fclass, pango_text::FONT_STYLE style)
{
	// Reset metrics to defaults
	return get_text_renderer()
		.set_family_class(fclass)
		.set_font_style(style)
		.set_font_size(size)
		.get_max_glyph_height();
}

} // namespace font
