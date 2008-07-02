/* $Id$ */
/*
   Copyright (C) 2006 - 2008 by Joerg Hinrichs <joerg.hinrichs@alice-dsl.de>
   wesnoth playturn Copyright (C) 2003 by David White <dave@whitevine.net>
   Part of the Battle for Wesnoth Project http://www.wesnoth.org/

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2
   or at your option any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/

#include "attack_prediction_display.hpp"

#include "attack_prediction.hpp"
#include "dialogs.hpp"
#include "game_events.hpp"
#include "gettext.hpp"
#include "log.hpp"
#include "marked-up_text.hpp"
#include "show_dialog.hpp"
#include "unit_abilities.hpp"
#include "wml_separators.hpp"
#include "unit_display.hpp"

#include <cstdlib>
// Conversion routine for both unscatched and damage change percentage.
static void format_prob(char str_buf[10], const float prob)
{

	if(prob > 0.9995) {
		snprintf(str_buf, 10, "100 %%");
	} else if(prob >= 0.1) { 
		snprintf(str_buf, 10, "%4.1f %%", 
			static_cast<float>(100.0 * (prob + 0.0005)));
	} else { 
		snprintf(str_buf, 10, " %3.1f %%", 
			static_cast<float>(100.0 * (prob + 0.0005)));
	}

	str_buf[9] = '\0';  //prevents _snprintf error
}


const int battle_prediction_pane::inter_line_gap_ = 3;
const int battle_prediction_pane::inter_column_gap_ = 30;
const int battle_prediction_pane::inter_units_gap_ = 30;
const int battle_prediction_pane::max_hp_distrib_rows_ = 10;

battle_prediction_pane::battle_prediction_pane(display &disp, const battle_context& bc, const gamemap& map,
											   const std::vector<team>& teams, const unit_map& units,
											   const gamestatus& status,
											   const gamemap::location& attacker_loc, const gamemap::location& defender_loc)
			: gui::preview_pane(disp.video()), disp_(disp), bc_(bc), map_(map), teams_(teams), units_(units), status_(status),
			  attacker_loc_(attacker_loc), defender_loc_(defender_loc),
			  attacker_(units.find(attacker_loc)->second), defender_(units.find(defender_loc)->second)
{
	// Predict the battle outcome.
	combatant attacker_combatant(bc.get_attacker_stats());
	combatant defender_combatant(bc.get_defender_stats());
	attacker_combatant.fight(defender_combatant);

	const battle_context::unit_stats& attacker_stats = bc.get_attacker_stats();
	const battle_context::unit_stats& defender_stats = bc.get_defender_stats();

	// Create the hitpoints distribution graphics.
	std::vector<std::pair<int, double> > hp_prob_vector;
	get_hp_prob_vector(attacker_combatant.hp_dist, hp_prob_vector);
	get_hp_distrib_surface(hp_prob_vector, attacker_stats, defender_stats, attacker_hp_distrib_,
						   attacker_hp_distrib_width_, attacker_hp_distrib_height_);
	get_hp_prob_vector(defender_combatant.hp_dist, hp_prob_vector);
	get_hp_distrib_surface(hp_prob_vector, defender_stats, attacker_stats, defender_hp_distrib_,
					   defender_hp_distrib_width_, defender_hp_distrib_height_);
	hp_distribs_height_ = maximum<int>(attacker_hp_distrib_height_, defender_hp_distrib_height_);

	// Build the strings and compute the layout.
	std::stringstream str;

	attacker_label_ = _("Attacker");
	defender_label_ = _("Defender");
	attacker_label_width_ = font::line_width(attacker_label_, font::SIZE_PLUS, TTF_STYLE_BOLD);
	defender_label_width_ = font::line_width(defender_label_, font::SIZE_PLUS, TTF_STYLE_BOLD);

	// Get the units strings.
	get_unit_strings(attacker_stats, attacker_, attacker_loc_, attacker_combatant.untouched,
					 defender_, defender_loc_, defender_stats.weapon,
					 attacker_left_strings_, attacker_right_strings_,
					 attacker_left_strings_width_, attacker_right_strings_width_, attacker_strings_width_);

	get_unit_strings(defender_stats, defender_, defender_loc_, defender_combatant.untouched,
					 attacker_, attacker_loc_, attacker_stats.weapon,
					 defender_left_strings_, defender_right_strings_,
					 defender_left_strings_width_, defender_right_strings_width_, defender_strings_width_);

	units_strings_height_ = maximum<int>(attacker_left_strings_.size(), defender_left_strings_.size())
						    * (font::SIZE_NORMAL + inter_line_gap_) + 14;

	hp_distrib_string_ = _("Expected Battle Result (HP)");
	hp_distrib_string_width_ = font::line_width(hp_distrib_string_, font::SIZE_SMALL);

	attacker_width_ = maximum<int>(attacker_label_width_, attacker_strings_width_);
	attacker_width_ = maximum<int>(attacker_width_, hp_distrib_string_width_);
	attacker_width_ = maximum<int>(attacker_width_, attacker_hp_distrib_width_);
	defender_width_ = maximum<int>(defender_label_width_, defender_strings_width_);
	defender_width_ = maximum<int>(defender_width_, hp_distrib_string_width_);
	defender_width_ = maximum<int>(defender_width_, defender_hp_distrib_width_);
	units_width_ = maximum<int>(attacker_width_, defender_width_);

	dialog_width_ = 2 * units_width_ + inter_units_gap_;
	dialog_height_ = 15 + 24 + units_strings_height_ + 14 + 19 + hp_distribs_height_ + 18;

	// Set the dialog size.
	set_measurements(dialog_width_, dialog_height_);
}

void battle_prediction_pane::get_unit_strings(const battle_context::unit_stats& stats,
										  const unit& u, const gamemap::location& u_loc, float u_unscathed,
										  const unit& opp, const gamemap::location& opp_loc, const attack_type *opp_weapon,
											  std::vector<std::string>& left_strings, std::vector<std::string>& right_strings,
									      int& left_strings_width, int& right_strings_width, int& strings_width)
{
	std::stringstream str;
	char str_buf[10];

	// With a weapon.
	if(stats.weapon != NULL) {

		// Set specials context (for safety, it should not have changed normally).
		const attack_type *weapon = stats.weapon;
		weapon->set_specials_context(u_loc, opp_loc, &units_, &map_, &status_, &teams_, stats.is_attacker, opp_weapon);

		// Get damage modifiers.
		unit_ability_list dmg_specials = weapon->get_specials("damage");
		unit_abilities::effect dmg_effect(dmg_specials, weapon->damage(), stats.backstab_pos);

		// Get the SET damage modifier, if any.
		const unit_abilities::individual_effect *set_dmg_effect = NULL;
		unit_abilities::effect_list::const_iterator i;
		for(i = dmg_effect.begin(); i != dmg_effect.end(); ++i) {
			if(i->type == unit_abilities::SET) {
				set_dmg_effect = &*i;
				break;
			}
		}

		// Either user the SET modifier or the base weapon damage.
		if(set_dmg_effect == NULL) {
			left_strings.push_back(weapon->name());
			str.str("");
			str << weapon->damage();
			right_strings.push_back(str.str());
		} else {
			left_strings.push_back((*set_dmg_effect->ability)["name"]);
			str.str("");
			str << set_dmg_effect->value;
			right_strings.push_back(str.str());
		}

		// Process the ADD damage modifiers.
		for(i = dmg_effect.begin(); i != dmg_effect.end(); ++i) {
			if(i->type == unit_abilities::ADD) {
				left_strings.push_back((*i->ability)["name"]);
				str.str("");
				if(i->value >= 0) str << "+" << i->value;
				else str << i->value;
				right_strings.push_back(str.str());
			}
		}

		// Process the MUL damage modifiers.
		for(i = dmg_effect.begin(); i != dmg_effect.end(); ++i) {
			if(i->type == unit_abilities::MUL) {
				left_strings.push_back((*i->ability)["name"]);
				str.str("");
				str << "* " << (i->value / 100);
				if(i->value % 100) {
					str << "." << ((i->value % 100) / 10);
					if(i->value % 10) str << (i->value % 10);
				}
				right_strings.push_back(str.str());
			}
		}

		// Resistance modifier.
		int resistance_modifier = opp.damage_from(*weapon, !stats.is_attacker, opp_loc);
		if(resistance_modifier != 100) {
			str.str("");
			if(stats.is_attacker) str << _("Defender");
			else str << _("Attacker");
			if(resistance_modifier < 100) str << _(" resistance vs ");
			else str << _(" vulnerability vs ");
			str << gettext(weapon->type().c_str());
			left_strings.push_back(str.str());
			str.str("");
			str << "* " << (resistance_modifier / 100) << "." << ((resistance_modifier % 100) / 10);
			right_strings.push_back(str.str());
		}

		// Slowed penalty.
		if(stats.is_slowed) {
			left_strings.push_back(_("Slowed"));
			right_strings.push_back("* 0.5");
		}

		// Time of day modifier.
		int tod_modifier = combat_modifier(status_, units_, u_loc, u.alignment(), u.is_fearless(), map_);
		if(tod_modifier != 0) {
			left_strings.push_back(_("Time of day"));
			str.str("");
			str << (tod_modifier > 0 ? "+" : "") << tod_modifier << "%";
			right_strings.push_back(str.str());
		}

// Leadership bonus.
int leadership_bonus = 0;
under_leadership(units_, u_loc, &leadership_bonus);
		if(leadership_bonus != 0) {
			left_strings.push_back(_("Leadership"));
			str.str("");
			str << "+" << leadership_bonus << "%";
			right_strings.push_back(str.str());
		}

		// Total damage.
		left_strings.push_back(_("Total damage"));
		str.str("");
		str << stats.damage << "-" << stats.num_blows << " (" << stats.chance_to_hit << "%)";
		right_strings.push_back(str.str());

	// Without a weapon.
	} else {
		left_strings.push_back(_("No usable weapon"));
		right_strings.push_back("");
	}

	// Unscathed probability.
	left_strings.push_back(_("Chance of being unscathed"));
	format_prob(str_buf, u_unscathed);
	right_strings.push_back(str_buf);

#if 0 // might not be en English!
	// Fix capitalisation of left strings.
	for(int i = 0; i < (int) left_strings.size(); i++)
		if(left_strings[i].size() > 0) left_strings[i][0] = toupper(left_strings[i][0]);
#endif

	// Compute the width of the strings.
	left_strings_width = get_strings_max_length(left_strings);
	right_strings_width = get_strings_max_length(right_strings);
	strings_width = left_strings_width + inter_column_gap_ + right_strings_width;
}

int battle_prediction_pane::get_strings_max_length(const std::vector<std::string>& strings)
{
	int max_len = 0;

	for(int i = 0; i < static_cast<int>(strings.size()); i++)
		max_len = maximum<int>(font::line_width(strings[i], font::SIZE_NORMAL), max_len);

	return max_len;
}

void battle_prediction_pane::get_hp_prob_vector(const std::vector<double>& hp_dist,
												std::vector<std::pair<int, double> >& hp_prob_vector)
{
	hp_prob_vector.clear();

	// First, we sort the probabilities in ascending order.
	std::vector<std::pair<double, int> > prob_hp_vector;
	int i;

	for(i = 0; i < static_cast<int>(hp_dist.size()); i++) {
		double prob = hp_dist[i];

		// We keep only values above 0.1%.
		if(prob > 0.001)
			prob_hp_vector.push_back(std::pair<double, int>(prob, i));
	}

	std::sort(prob_hp_vector.begin(), prob_hp_vector.end());

	// We store a few of the highest probability hitpoint values.
	int nb_elem = minimum<int>(max_hp_distrib_rows_, prob_hp_vector.size());

	for(i = prob_hp_vector.size() - nb_elem;
			i < static_cast<int>(prob_hp_vector.size()); i++) {

		hp_prob_vector.push_back(std::pair<int, double>
			(prob_hp_vector[i].second, prob_hp_vector[i].first));
		}

	// Then, we sort the hitpoint values in ascending order.
	std::sort(hp_prob_vector.begin(), hp_prob_vector.end());
}

void battle_prediction_pane::draw_contents()
{
	// We must align both damage lines.
	int damage_line_skip = maximum<int>(attacker_left_strings_.size(), defender_left_strings_.size()) - 2;

	draw_unit(0, damage_line_skip,
			  attacker_left_strings_width_, attacker_left_strings_, attacker_right_strings_,
			  attacker_label_, attacker_label_width_, attacker_hp_distrib_, attacker_hp_distrib_width_);

	draw_unit(units_width_ + inter_units_gap_, damage_line_skip,
			  defender_left_strings_width_, defender_left_strings_, defender_right_strings_,
			  defender_label_, defender_label_width_, defender_hp_distrib_, defender_hp_distrib_width_);
}

void battle_prediction_pane::draw_unit(int x_off, int damage_line_skip, int left_strings_width,
									   const std::vector<std::string>& left_strings,
									   const std::vector<std::string>& right_strings,
									   const std::string& label, int label_width,
									   surface& hp_distrib, int hp_distrib_width)
{
	surface screen = disp_.get_screen_surface();
	int i;

	// NOTE. A preview pane is not made to be used alone and it is not
	// centered in the middle of the dialog. We "fix" this problem by moving
	// the clip rectangle 10 pixels to the right. This is a kludge and it
	// should be removed by 1) writing a custom dialog handler, or
	// 2) modify preview_pane so that it accepts {left, middle, right} as
	// layout possibilities.

	// Get clip rectangle and center it
	SDL_Rect clip_rect = location();
	clip_rect.x += 10;

	// Current vertical offset. We draw the dialog line-by-line, starting at the top.
	int y_off = 15;

	// Draw unit label.
	font::draw_text_line(screen, clip_rect, font::SIZE_15, font::NORMAL_COLOUR, label,
						 clip_rect.x + x_off + (units_width_ - label_width) / 2, clip_rect.y + y_off, 0, TTF_STYLE_BOLD);

	y_off += 24;

	// Draw unit left and right strings except the last two (total damage and unscathed probability).
	for(i = 0; i < static_cast<int>(left_strings.size()) - 2; i++) {
		font::draw_text_line(screen, clip_rect, font::SIZE_NORMAL, font::NORMAL_COLOUR, left_strings[i],
							 clip_rect.x + x_off, clip_rect.y + y_off + (font::SIZE_NORMAL + inter_line_gap_) * i,
							 0, TTF_STYLE_NORMAL);

		font::draw_text_line(screen, clip_rect, font::SIZE_NORMAL, font::NORMAL_COLOUR, right_strings[i],
							 clip_rect.x + x_off + left_strings_width + inter_column_gap_,
							 clip_rect.y + y_off + (font::SIZE_NORMAL + inter_line_gap_) * i, 0, TTF_STYLE_NORMAL);
	}

	// Ensure both damage lines are aligned.
	y_off += damage_line_skip * (font::SIZE_NORMAL + inter_line_gap_) + 14;

	// Draw total damage and unscathed probability.
	for(i = 0; i < 2; i++) {
		const std::string& left_string = left_strings[left_strings.size() - 2 + i];
		const std::string& right_string = right_strings[right_strings.size() - 2 + i];

		font::draw_text_line(screen, clip_rect, font::SIZE_NORMAL, font::NORMAL_COLOUR, left_string,
							 clip_rect.x + x_off, clip_rect.y + y_off + (font::SIZE_NORMAL + inter_line_gap_) * i,
							 0, TTF_STYLE_NORMAL);

		font::draw_text_line(screen, clip_rect, font::SIZE_NORMAL, font::NORMAL_COLOUR, right_string,
							 clip_rect.x + x_off + left_strings_width + inter_column_gap_,
							 clip_rect.y + y_off + (font::SIZE_NORMAL + inter_line_gap_) * i, 0, TTF_STYLE_NORMAL);
	}

	y_off += 2 * (font::SIZE_NORMAL + inter_line_gap_) + 14;

	// Draw hitpoints distribution string.
	font::draw_text(screen, clip_rect, font::SIZE_SMALL, font::NORMAL_COLOUR, hp_distrib_string_,
					clip_rect.x + x_off + (units_width_ - hp_distrib_string_width_) / 2, clip_rect.y + y_off);

	y_off += 19;

	// Draw hitpoints distributions.
	video().blit_surface(clip_rect.x + x_off + (units_width_ - hp_distrib_width) / 2, clip_rect.y + y_off, hp_distrib);
}

void battle_prediction_pane::get_hp_distrib_surface(const std::vector<std::pair<int, double> >& hp_prob_vector,
													const battle_context::unit_stats& stats,
													const battle_context::unit_stats& opp_stats,
													surface& surf, int& width, int& height)
{
	// Font size. If you change this, you must update the separator space.
	int fs = font::SIZE_SMALL;

	// Space before HP separator.
	int hp_sep = 24 + 6;

	// Bar space between both separators.
	int bar_space = 150;

	// Space after percentage separator.
	int percent_sep = 43 + 6;

	// Surface width and height.
	width = 30 + 2 + bar_space + 2 + percent_sep;
	height = 5 + (fs + 2) * hp_prob_vector.size();

	// Create the surface.
	surf = create_neutral_surface(width, height);
	// following SDL code will use color key,
	// so we need to remove the alpha channel to make it work
	// FIXME: use standard alpha and blit_surface instead
	SDL_SetAlpha(surf, 0, SDL_ALPHA_OPAQUE);

	SDL_Rect clip_rect = {0, 0, width, height};
	Uint32 grey_color = SDL_MapRGB(surf->format, 0xb7, 0xc1, 0xc1);
	Uint32 transparent_color = SDL_MapRGB(surf->format, 1, 1, 1);

	// Enable transparency.
	SDL_SetColorKey(surf, SDL_SRCCOLORKEY, transparent_color);
	SDL_FillRect(surf, &clip_rect, transparent_color);

	// Draw the surrounding borders and separators.
	SDL_Rect top_border_rect = {0, 0, width, 2};
	SDL_FillRect(surf, &top_border_rect, grey_color);

	SDL_Rect bottom_border_rect = {0, height - 2, width, 2};
	SDL_FillRect(surf, &bottom_border_rect, grey_color);

	SDL_Rect left_border_rect = {0, 0, 2, height};
	SDL_FillRect(surf, &left_border_rect, grey_color);

	SDL_Rect right_border_rect = {width - 2, 0, 2, height};
	SDL_FillRect(surf, &right_border_rect, grey_color);

	SDL_Rect hp_sep_rect = {hp_sep, 0, 2, height};
	SDL_FillRect(surf, &hp_sep_rect, grey_color);

	SDL_Rect percent_sep_rect = {width - percent_sep - 2, 0, 2, height};
	SDL_FillRect(surf, &percent_sep_rect, grey_color);

	// Draw the rows (lower HP values are at the bottom).
	for(int i = 0; i < static_cast<int>(hp_prob_vector.size()); i++) {
		char str_buf[10];

		// Get the HP and probability.
		int hp = hp_prob_vector[hp_prob_vector.size() - i - 1].first;
		double prob = hp_prob_vector[hp_prob_vector.size() - i - 1].second;

		SDL_Color row_color;

		// Death line is red.
		if(hp == 0) {
			SDL_Color color = {0xe5, 0, 0, 0};
			row_color = color;
		}

		// Below current hitpoints value is orange.
		else if(hp < static_cast<int>(stats.hp)) {
			// Stone is grey.
			if(opp_stats.stones) {
				SDL_Color color = {0x9a, 0x9a, 0x9a, 0};
				row_color = color;
			} else {
				SDL_Color color = {0xf4, 0xc9, 0, 0};
				row_color = color;
			}
		}

		// Current hitpoints value and above is green.
		else {
			SDL_Color color = {0x08, 0xca, 0, 0};
			row_color = color;
		}

		// Print HP, aligned right.
		snprintf(str_buf, 10, "%d", hp);
		str_buf[9] = '\0';  //prevents _snprintf error
		int hp_width = font::line_width(str_buf, fs);

		// Draw bars.
		font::draw_text_line(surf, clip_rect, fs, font::NORMAL_COLOUR, str_buf,
							 hp_sep - hp_width - 2, 2 + (fs + 2) * i, 0, TTF_STYLE_NORMAL);

		int bar_len = maximum<int>(static_cast<int>((prob * (bar_space - 4)) + 0.5), 2);

		SDL_Rect bar_rect_1 = {hp_sep + 4, 6 + (fs + 2) * i, bar_len, 8};
		SDL_FillRect(surf, &bar_rect_1, blend_rgb(surf, row_color.r, row_color.g, row_color.b, 100));

		SDL_Rect bar_rect_2 = {hp_sep + 4, 7 + (fs + 2) * i, bar_len, 6};
		SDL_FillRect(surf, &bar_rect_2, blend_rgb(surf, row_color.r, row_color.g, row_color.b, 66));

		SDL_Rect bar_rect_3 = {hp_sep + 4, 8 + (fs + 2) * i, bar_len, 4};
		SDL_FillRect(surf, &bar_rect_3, blend_rgb(surf, row_color.r, row_color.g, row_color.b, 33));

		SDL_Rect bar_rect_4 = {hp_sep + 4, 9 + (fs + 2) * i, bar_len, 2};
		SDL_FillRect(surf, &bar_rect_4, blend_rgb(surf, row_color.r, row_color.g, row_color.b, 0));

		// Draw probability percentage, aligned right.
		format_prob(str_buf, prob);
		int prob_width = font::line_width(str_buf, fs);
		font::draw_text_line(surf, clip_rect, fs, font::NORMAL_COLOUR, str_buf,
						 width - prob_width - 4, 2 + (fs + 2) * i, 0, TTF_STYLE_NORMAL);
	}
}

Uint32 battle_prediction_pane::blend_rgb(const surface& surf, unsigned char r, unsigned char g, unsigned char b, unsigned char drop)
{
	// We simply decrement each component.
	if(r < drop) r = 0; else r -= drop;
	if(g < drop) g = 0; else g -= drop;
	if(b < drop) b = 0; else b -= drop;

	return SDL_MapRGB(surf->format, r, g, b);
}

attack_prediction_displayer::RESULT attack_prediction_displayer::button_pressed(int selection)
{
	// Get the selected weapon, if any.
	const size_t index = size_t(selection);

	if(index < bc_vector_.size()) {
		battle_prediction_pane battle_pane(disp_, bc_vector_[index], map_, teams_, units_, status_,
										   attacker_loc_, defender_loc_);
		std::vector<gui::preview_pane*> preview_panes;
		preview_panes.push_back(&battle_pane);

		gui::show_dialog(disp_, NULL, _("Damage Calculations"), "", gui::OK_ONLY, NULL, &preview_panes);
	}

	return gui::CONTINUE_DIALOG;
}

	
