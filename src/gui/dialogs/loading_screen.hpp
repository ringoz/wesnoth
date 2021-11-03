/*
	Copyright (C) 2016 - 2021
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

#include "events.hpp"
#include "tstring.hpp"

namespace cursor
{
	struct setter;
}

/**
 * Loading screen stage IDs.
 * When adding new entries here, don't forget to update the stage_names
 * map with an appropriate description.
 */
enum class loading_stage
{
	build_terrain,
	create_cache,
	init_display,
	init_fonts,
	init_teams,
	init_theme,
	load_config,
	load_data,
	load_level,
	init_lua,
	init_whiteboard,
	load_unit_types,
	load_units,
	refresh_addons,
	start_game,
	verify_cache,
	connect_to_server,
	login_response,
	waiting,
	redirect,
	next_scenario,
	download_level_data,
	download_lobby_data,
	none,
};

namespace gui2
{

namespace dialogs
{
class loading_screen
{
public:
	static void display(const std::function<void()> &f);
	static bool displaying() { return false; }

	static void progress(loading_stage stage = loading_stage::none);
};

} // namespace dialogs
} // namespace gui2
