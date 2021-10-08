/*
	Copyright (C) 2017 - 2021
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

#include <SDL2/SDL.h>
#include <SDL2/SDL_rwops.h>

#include "filesystem.hpp"
#include "log.hpp"

#include <algorithm>
#include <cassert>

static lg::log_domain log_filesystem("filesystem");
#define ERR_FS LOG_STREAM(err, log_filesystem)

namespace filesystem {

rwops_ptr make_read_RWops(const std::string &path) {
	rwops_ptr rw(SDL_RWFromFile(path.c_str(), "r"), &SDL_RWclose);
	if(!rw) {
		ERR_FS << "make_read_RWops: istream_file returned NULL on " << path << '\n';
	}
	return rw;
}

rwops_ptr make_write_RWops(const std::string &path) {
	rwops_ptr rw(SDL_RWFromFile(path.c_str(), "w"), &SDL_RWclose);
	if(!rw) {
		ERR_FS << "make_write_RWops: ostream_file returned NULL on " << path << '\n';
	}
	return rw;
}

}
