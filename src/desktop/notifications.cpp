/*
	Copyright (C) 2003 - 2021
	by David White <dave@whitevine.net>
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

#include "desktop/notifications.hpp"

#include "preferences/game.hpp"
#include "gettext.hpp"

#include "video.hpp" //CVideo::get_singleton().window_state()

namespace desktop {

namespace notifications {

bool available() { return false; }

void send(const std::string& /*owner*/, const std::string& /*message*/, type /*t*/)
{}

} //end namespace notifications

} //end namespace desktop
