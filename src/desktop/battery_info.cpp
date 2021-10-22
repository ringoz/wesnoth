/*
	Copyright (C) 2018 - 2021
	by Martin Hrubý <hrubymar10@gmail.com>
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

#include "battery_info.hpp"

namespace desktop {
namespace battery_info {

bool does_device_have_battery()
{
	return false;
}

double get_battery_percentage()
{
	return -1;
}

} // end namespace battery_info
} // end namespace desktop
