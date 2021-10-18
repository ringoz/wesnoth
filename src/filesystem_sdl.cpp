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
#include <boost/iostreams/stream.hpp>

static lg::log_domain log_filesystem("filesystem");
#define LOG_FS LOG_STREAM(info, log_filesystem)
#define ERR_FS LOG_STREAM(err, log_filesystem)

namespace filesystem {
	
void rwops_deleter::operator()(SDL_RWops* context)
{
	if (context)
		SDL_RWclose(context);
}

rwops_ptr make_read_RWops(const std::string &path) {
	rwops_ptr rw(SDL_RWFromFile(path.c_str(), "r"));
	if(!rw) {
		ERR_FS << "make_read_RWops: istream_file returned NULL on " << path << '\n';
	}
	return rw;
}

rwops_ptr make_write_RWops(const std::string &path) {
	rwops_ptr rw(SDL_RWFromFile(path.c_str(), "w"));
	if(!rw) {
		ERR_FS << "make_write_RWops: ostream_file returned NULL on " << path << '\n';
	}
	return rw;
}

class rwops_device
{
	using rwops_ptr = std::shared_ptr<SDL_RWops>;
	rwops_ptr m_rwops;

public:
	typedef char char_type;
	typedef boost::iostreams::seekable_device_tag category;

	rwops_device(const char* file, const char* mode)
		: rwops_device(rwops_ptr(SDL_RWFromFile(file, mode), rwops_deleter()))
	{
	}

	rwops_device(rwops_ptr rwops)
		: m_rwops(std::move(rwops))
	{
		if (!m_rwops)
			boost::throw_exception(BOOST_IOSTREAMS_FAILURE("bad rwops"));
	}

	std::streamsize read(char* s, std::streamsize n)
	{
		return SDL_RWread(m_rwops.get(), s, 1, n);
	}

	std::streamsize write(const char* s, std::streamsize n)
	{
		return SDL_RWwrite(m_rwops.get(), s, 1, n);
	}

	boost::iostreams::stream_offset seek(boost::iostreams::stream_offset off, std::ios_base::seekdir way)
	{
		return SDL_RWseek(m_rwops.get(), off, way);
	}
};

filesystem::scoped_istream istream_file(const std::string& fname, bool treat_failure_as_error)
{
	LOG_FS << "Streaming " << fname << " for reading.\n";

	if(fname.empty()) {
		ERR_FS << "Trying to open file with empty name.\n";
		filesystem::scoped_istream s(new std::ifstream());
		s->clear(std::ios_base::failbit);
		return s;
	}

	try {
		return std::make_unique<boost::iostreams::stream<rwops_device>>(fname.c_str(), "r");
	} catch(const std::exception&) {
		if(treat_failure_as_error) {
			ERR_FS << "Could not open '" << fname << "' for reading.\n";
		}

		filesystem::scoped_istream s(new std::ifstream());
		s->clear(std::ios_base::failbit);
		return s;
	}
}

filesystem::scoped_ostream ostream_file(const std::string& fname, std::ios_base::openmode mode, bool create_directory)
{
	assert(mode == std::ios_base::binary);
	LOG_FS << "streaming " << fname << " for writing.\n";
	try {
		return std::make_unique<boost::iostreams::stream<rwops_device>>(fname.c_str(), "w");
	} catch(const BOOST_IOSTREAMS_FAILURE& e) {
		// If this operation failed because the parent directory didn't exist, create the parent directory and
		// retry.
		if(create_directory && create_directory_if_missing_recursive(directory_name(fname))) {
			return ostream_file(fname, mode, false);
		}

		throw filesystem::io_exception(e.what());
	}
}

}
