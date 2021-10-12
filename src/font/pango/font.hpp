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

#pragma once

#include "font/text.hpp"
#include <SDL2/SDL_ttf.h>

namespace font {

/** Small helper class to make sure the pango font object is destroyed properly. */
class p_font
{
public:
	p_font(const std::string& name, const unsigned size, font::pango_text::FONT_STYLE style)
	{
		static auto init = TTF_Init();
		static std::unordered_map<unsigned, TTF_Font *> cache;

		auto &font = cache[size];
		if (!font)
			font = TTF_OpenFont("fonts/Lato-Medium.ttf", size);

		font_ = font;
	}

	p_font(const p_font &) = delete;
	p_font & operator = (const p_font &) = delete;

	~p_font() {}

	TTF_Font* get() { return font_; }

private:
	TTF_Font *font_;
};

} // namespace font
