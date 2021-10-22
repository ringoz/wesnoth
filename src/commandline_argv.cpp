/*
	Copyright (C) 2020 - 2021
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

#include "commandline_argv.hpp"

std::vector<std::string> read_argv([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
	std::vector<std::string> args;
	for(int i = 0; i < argc; ++i) {
		args.push_back(std::string(argv[i]));
	}
	return args;
}
