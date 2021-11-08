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

/**
 * @file
 * File-IO
 */
#define GETTEXT_DOMAIN "wesnoth-lib"

#include "filesystem.hpp"

#include "config.hpp"
#include "deprecation.hpp"
#include "game_config.hpp"
#include "gettext.hpp"
#include "log.hpp"
#include "serialization/string_utils.hpp"
#include "serialization/unicode.hpp"
#include "serialization/unicode_cast.hpp"

#include <boost/algorithm/string.hpp>
#include "game_config_view.hpp"

#include <algorithm>
#include <set>

// Copied from boost::predef, as it's there only since 1.55.
#include <SDL2/SDL_filesystem.h>


static lg::log_domain log_filesystem("filesystem");
#define DBG_FS LOG_STREAM(debug, log_filesystem)
#define LOG_FS LOG_STREAM(info, log_filesystem)
#define WRN_FS LOG_STREAM(warn, log_filesystem)
#define ERR_FS LOG_STREAM(err, log_filesystem)

#ifdef NANOHEX
#include <OS/filesystem>
namespace bfs = OS::filesystem;
#else
#include <filesystem>
namespace bfs = std::filesystem;
#endif
using std::error_code;
using std::errc;

namespace
{
// These are the filenames that get special processing
const std::string maincfg_filename = "_main.cfg";
const std::string finalcfg_filename = "_final.cfg";
const std::string initialcfg_filename = "_initial.cfg";
} // namespace

namespace filesystem
{

static void push_if_exists(std::vector<std::string>* vec, const bfs::path& file, bool full)
{
	if(vec != nullptr) {
		if(full) {
			vec->push_back(file.generic_string());
		} else {
			vec->push_back(file.filename().generic_string());
		}
	}
}

static inline bool error_except_not_found(const error_code& ec)
{
	return ec && ec != errc::no_such_file_or_directory;
}

static bool is_directory_internal(const bfs::path& fpath)
{
	error_code ec;
	bool is_dir = bfs::is_directory(fpath, ec);
	if(error_except_not_found(ec)) {
		LOG_FS << "Failed to check if " << fpath.string() << " is a directory: " << ec.message() << '\n';
	}

	return is_dir;
}

static bool file_exists(const bfs::path& fpath)
{
	error_code ec;
	bool exists = bfs::exists(fpath, ec);
	if(error_except_not_found(ec)) {
		ERR_FS << "Failed to check existence of file " << fpath.string() << ": " << ec.message() << '\n';
	}

	return exists;
}

static bfs::path get_dir(const bfs::path& dirpath)
{
	bool is_dir = is_directory_internal(dirpath);
	if(!is_dir) {
		error_code ec;
		bfs::create_directory(dirpath, ec);

		if(ec) {
			ERR_FS << "Failed to create directory " << dirpath.string() << ": " << ec.message() << '\n';
		}

		// This is probably redundant
		is_dir = is_directory_internal(dirpath);
	}

	if(!is_dir) {
		ERR_FS << "Could not open or create directory " << dirpath.string() << '\n';
		return std::string();
	}

	return dirpath;
}

static bool create_directory_if_missing(const bfs::path& dirpath)
{
	error_code ec;
	bfs::file_status fs = bfs::status(dirpath, ec);

	if(error_except_not_found(ec)) {
		ERR_FS << "Failed to retrieve file status for " << dirpath.string() << ": " << ec.message() << '\n';
		return false;
	} else if(bfs::is_directory(fs)) {
		DBG_FS << "directory " << dirpath.string() << " exists, not creating\n";
		return true;
	} else if(bfs::exists(fs)) {
		ERR_FS << "cannot create directory " << dirpath.string() << "; file exists\n";
		return false;
	}

	bool created = bfs::create_directory(dirpath, ec);
	if(ec) {
		ERR_FS << "Failed to create directory " << dirpath.string() << ": " << ec.message() << '\n';
	}

	return created;
}

static bool create_directory_if_missing_recursive(const bfs::path& dirpath)
{
	DBG_FS << "creating recursive directory: " << dirpath.string() << '\n';

	if(dirpath.empty()) {
		return false;
	}

	error_code ec;
	bfs::file_status fs = bfs::status(dirpath, ec);

	if(error_except_not_found(ec)) {
		ERR_FS << "Failed to retrieve file status for " << dirpath.string() << ": " << ec.message() << '\n';
		return false;
	} else if(bfs::is_directory(fs)) {
		return true;
	} else if(bfs::exists(fs)) {
		return false;
	}

	if(!dirpath.has_parent_path() || create_directory_if_missing_recursive(dirpath.parent_path())) {
		return create_directory_if_missing(dirpath);
	} else {
		ERR_FS << "Could not create parents to " << dirpath.string() << '\n';
		return false;
	}
}

void get_files_in_dir(const std::string& dir,
		std::vector<std::string>* files,
		std::vector<std::string>* dirs,
		name_mode mode,
		filter_mode filter,
		reorder_mode reorder,
		file_tree_checksum* checksum)
{
	if(bfs::path(dir).is_relative() && !game_config::path.empty()) {
		bfs::path absolute_dir(game_config::path);
		absolute_dir /= dir;

		if(is_directory_internal(absolute_dir)) {
			get_files_in_dir(absolute_dir.string(), files, dirs, mode, filter, reorder, checksum);
			return;
		}
	}

	const bfs::path dirpath(dir);

	if(reorder == reorder_mode::DO_REORDER) {
		LOG_FS << "searching for _main.cfg in directory " << dir << '\n';
		const bfs::path maincfg = dirpath / maincfg_filename;

		if(file_exists(maincfg)) {
			LOG_FS << "_main.cfg found : " << maincfg << '\n';
			push_if_exists(files, maincfg, mode == name_mode::ENTIRE_FILE_PATH);
			return;
		}
	}

	error_code ec;
	bfs::directory_iterator di(dirpath, ec);
	bfs::directory_iterator end;

	// Probably not a directory, let the caller deal with it.
	if(ec) {
		return;
	}

	for(; di != end; ++di) {
		bfs::file_status st = di->status(ec);
		if(ec) {
			LOG_FS << "Failed to get file status of " << di->path().string() << ": " << ec.message() << '\n';
			continue;
		}

		if(st.type() == bfs::file_type::regular) {
			{
				std::string basename = di->path().filename().string();
				if(filter == filter_mode::SKIP_PBL_FILES && looks_like_pbl(basename))
					continue;
				if(!basename.empty() && basename[0] == '.')
					continue;
			}

			push_if_exists(files, di->path(), mode == name_mode::ENTIRE_FILE_PATH);

			if(checksum != nullptr) {
				std::time_t mtime = std::chrono::duration_cast<std::chrono::seconds>(bfs::last_write_time(di->path(), ec).time_since_epoch()).count();
				if(ec) {
					LOG_FS << "Failed to read modification time of " << di->path().string() << ": " << ec.message()
						   << '\n';
				} else if(mtime > checksum->modified) {
					checksum->modified = mtime;
				}

				uintmax_t size = bfs::file_size(di->path(), ec);
				if(ec) {
					LOG_FS << "Failed to read filesize of " << di->path().string() << ": " << ec.message() << '\n';
				} else {
					checksum->sum_size += size;
				}

				checksum->nfiles++;
			}
		} else if(st.type() == bfs::file_type::directory) {
			std::string basename = di->path().filename().string();

			if(!basename.empty() && basename[0] == '.') {
				continue;
			}

			if(filter == filter_mode::SKIP_MEDIA_DIR && (basename == "images" || basename == "sounds")) {
				continue;
			}

			const bfs::path inner_main(di->path() / maincfg_filename);
			bfs::file_status main_st = bfs::status(inner_main, ec);

			if(error_except_not_found(ec)) {
				LOG_FS << "Failed to get file status of " << inner_main.string() << ": " << ec.message() << '\n';
			} else if(reorder == reorder_mode::DO_REORDER && main_st.type() == bfs::file_type::regular) {
				LOG_FS << "_main.cfg found : "
					   << (mode == name_mode::ENTIRE_FILE_PATH ? inner_main.string() : inner_main.filename().string()) << '\n';
				push_if_exists(files, inner_main, mode == name_mode::ENTIRE_FILE_PATH);
			} else {
				push_if_exists(dirs, di->path(), mode == name_mode::ENTIRE_FILE_PATH);
			}
		}
	}

	if(files != nullptr) {
		std::sort(files->begin(), files->end());
	}

	if(dirs != nullptr) {
		std::sort(dirs->begin(), dirs->end());
	}

	if(files != nullptr && reorder == reorder_mode::DO_REORDER) {
		// move finalcfg_filename, if present, to the end of the vector
		for(unsigned int i = 0; i < files->size(); i++) {
			if(ends_with((*files)[i], "/" + finalcfg_filename)) {
				files->push_back((*files)[i]);
				files->erase(files->begin() + i);
				break;
			}
		}

		// move initialcfg_filename, if present, to the beginning of the vector
		int foundit = -1;
		for(unsigned int i = 0; i < files->size(); i++)
			if(ends_with((*files)[i], "/" + initialcfg_filename)) {
				foundit = i;
				break;
			}
		if(foundit > 0) {
			std::string initialcfg = (*files)[foundit];
			for(unsigned int i = foundit; i > 0; i--)
				(*files)[i] = (*files)[i - 1];
			(*files)[0] = initialcfg;
		}
	}
}

std::string get_dir(const std::string& dir)
{
	return get_dir(bfs::path(dir)).string();
}

std::string get_next_filename(const std::string& name, const std::string& extension)
{
	std::string next_filename;
	int counter = 0;

	do {
		std::stringstream filename;

		filename << name;
		filename.width(3);
		filename.fill('0');
		filename.setf(std::ios_base::right);
		filename << counter << extension;

		counter++;
		next_filename = filename.str();
	} while(file_exists(next_filename) && counter < 1000);

	return next_filename;
}

static bfs::path user_data_dir, user_config_dir, cache_dir;

const std::string get_version_path_suffix(const version_info& version)
{
	std::ostringstream s;
	s << version.major_version() << '.' << version.minor_version();
	return s.str();
}

const std::string& get_version_path_suffix()
{
	static std::string suffix;

	// We only really need to generate this once since
	// the version number cannot change during runtime.

	if(suffix.empty()) {
		suffix = get_version_path_suffix(game_config::wesnoth_version);
	}

	return suffix;
}

static void setup_user_data_dir()
{
	if(!file_exists(user_data_dir)) {
		game_config::check_migration = true;
	}

	if(!create_directory_if_missing_recursive(user_data_dir)) {
		ERR_FS << "could not open or create user data directory at " << user_data_dir.string() << '\n';
		return;
	}
	// TODO: this may not print the error message if the directory exists but we don't have the proper permissions

	// Create user data and add-on directories
	create_directory_if_missing(user_data_dir / "editor");
	create_directory_if_missing(user_data_dir / "editor" / "maps");
	create_directory_if_missing(user_data_dir / "editor" / "scenarios");
	create_directory_if_missing(user_data_dir / "data");
	create_directory_if_missing(user_data_dir / "data" / "add-ons");
	create_directory_if_missing(user_data_dir / "saves");
	create_directory_if_missing(user_data_dir / "persist");
}

void set_user_data_dir(std::string newprefdir)
{
	[[maybe_unused]] bool relative_ok = false;

#ifdef PREFERENCES_DIR
	if(newprefdir.empty()) {
		newprefdir = PREFERENCES_DIR;
		relative_ok = true;
	}
#endif

	std::string backupprefdir = ".wesnoth" + get_version_path_suffix();

	char *sdl_pref_path = SDL_GetPrefPath("wesnoth.org", "iWesnoth");
	if(sdl_pref_path) {
		backupprefdir = std::string(sdl_pref_path) + backupprefdir;
		SDL_free(sdl_pref_path);
	}

	if(newprefdir.empty()) {
		newprefdir = backupprefdir;
	}

	const char* home_str = getenv("HOME");
	bfs::path home = home_str ? home_str : ".";

	user_data_dir = newprefdir;
	setup_user_data_dir();
	user_data_dir = normalize_path(user_data_dir.string(), true, true);
}

static void set_user_config_path(bfs::path newconfig)
{
	user_config_dir = newconfig;
	if(!create_directory_if_missing_recursive(user_config_dir)) {
		ERR_FS << "could not open or create user config directory at " << user_config_dir.string() << '\n';
	}
}

void set_user_config_dir(const std::string& newconfigdir)
{
	set_user_config_path(newconfigdir);
}

static const bfs::path& get_user_data_path()
{
	if(user_data_dir.empty()) {
		set_user_data_dir(std::string());
	}

	return user_data_dir;
}

std::string get_user_config_dir()
{
	if(user_config_dir.empty()) {
		user_config_dir = get_user_data_path();
	}

	return user_config_dir.string();
}

std::string get_user_data_dir()
{
	return get_user_data_path().string();
}

std::string get_cache_dir()
{
	if(cache_dir.empty()) {
		cache_dir = get_dir(get_user_data_path() / "cache");
	}

	return cache_dir.string();
}

std::vector<other_version_dir> find_other_version_saves_dirs()
{
	return {};
}

std::string get_cwd()
{
	error_code ec;
	bfs::path cwd = bfs::current_path(ec);

	if(ec) {
		ERR_FS << "Failed to get current directory: " << ec.message() << '\n';
		return "";
	}

	return cwd.generic_string();
}

bool set_cwd(const std::string& dir)
{
	error_code ec;
	bfs::current_path(bfs::path{dir}, ec);

	if(ec) {
		ERR_FS << "Failed to set current directory: " << ec.message() << '\n';
		return false;
	} else {
		LOG_FS << "Process working directory set to " << dir << '\n';
	}

	return true;
}

std::string get_exe_dir()
{
	return get_cwd();
}

bool make_directory(const std::string& dirname)
{
	error_code ec;
	bool created = bfs::create_directory(bfs::path(dirname), ec);
	if(ec) {
		ERR_FS << "Failed to create directory " << dirname << ": " << ec.message() << '\n';
	}

	return created;
}

bool delete_directory(const std::string& dirname, const bool keep_pbl)
{
	bool ret = true;
	std::vector<std::string> files;
	std::vector<std::string> dirs;
	error_code ec;

	get_files_in_dir(dirname, &files, &dirs, name_mode::ENTIRE_FILE_PATH, keep_pbl ? filter_mode::SKIP_PBL_FILES : filter_mode::NO_FILTER);

	if(!files.empty()) {
		for(const std::string& f : files) {
			bfs::remove(bfs::path(f), ec);
			if(ec) {
				LOG_FS << "remove(" << f << "): " << ec.message() << '\n';
				ret = false;
			}
		}
	}

	if(!dirs.empty()) {
		for(const std::string& d : dirs) {
			// TODO: this does not preserve any other PBL files
			// filesystem.cpp does this too, so this might be intentional
			if(!delete_directory(d))
				ret = false;
		}
	}

	if(ret) {
		bfs::remove(bfs::path(dirname), ec);
		if(ec) {
			LOG_FS << "remove(" << dirname << "): " << ec.message() << '\n';
			ret = false;
		}
	}

	return ret;
}

bool delete_file(const std::string& filename)
{
	error_code ec;
	bool ret = bfs::remove(bfs::path(filename), ec);
	if(ec) {
		ERR_FS << "Could not delete file " << filename << ": " << ec.message() << '\n';
	}

	return ret;
}

std::string read_file(const std::string& fname)
{
	scoped_istream is = istream_file(fname);
	std::stringstream ss;
	ss << is->rdbuf();
	return ss.str();
}

// Throws io_exception if an error occurs
void write_file(const std::string& fname, const std::string& data)
{
	scoped_ostream os = ostream_file(fname);
	os->exceptions(std::ios_base::goodbit);

	const std::size_t block_size = 4096;
	char buf[block_size];

	for(std::size_t i = 0; i < data.size(); i += block_size) {
		const std::size_t bytes = std::min<std::size_t>(block_size, data.size() - i);
		std::copy(data.begin() + i, data.begin() + i + bytes, buf);

		os->write(buf, bytes);
		if(os->bad()) {
			throw io_exception("Error writing to file: '" + fname + "'");
		}
	}
}

void copy_file(const std::string& src, const std::string& dest)
{
	write_file(dest, read_file(src));
}

bool create_directory_if_missing(const std::string& dirname)
{
	return create_directory_if_missing(bfs::path(dirname));
}

bool create_directory_if_missing_recursive(const std::string& dirname)
{
	return create_directory_if_missing_recursive(bfs::path(dirname));
}

bool is_directory(const std::string& fname)
{
	return is_directory_internal(bfs::path(fname));
}

bool file_exists(const std::string& name)
{
	return file_exists(bfs::path(name));
}

std::time_t file_modified_time(const std::string& fname)
{
	error_code ec;
	std::time_t mtime = std::chrono::duration_cast<std::chrono::seconds>(bfs::last_write_time(bfs::path(fname), ec).time_since_epoch()).count();
	if(ec) {
		LOG_FS << "Failed to read modification time of " << fname << ": " << ec.message() << '\n';
	}

	return mtime;
}

bool is_gzip_file(const std::string& filename)
{
	return bfs::path(filename).extension() == ".gz";
}

bool is_bzip2_file(const std::string& filename)
{
	return bfs::path(filename).extension() == ".bz2";
}

int file_size(const std::string& fname)
{
	error_code ec;
	uintmax_t size = bfs::file_size(bfs::path(fname), ec);
	if(ec) {
		LOG_FS << "Failed to read filesize of " << fname << ": " << ec.message() << '\n';
		return -1;
	} else if(size > INT_MAX) {
		return INT_MAX;
	} else {
		return size;
	}
}

int dir_size(const std::string& pname)
{
	bfs::path p(pname);
	uintmax_t size_sum = 0;
	error_code ec;
	for(bfs::recursive_directory_iterator i(p), end; i != end && !ec; ++i) {
		if(bfs::is_regular_file(i->path())) {
			size_sum += bfs::file_size(i->path(), ec);
		}
	}

	if(ec) {
		LOG_FS << "Failed to read directorysize of " << pname << ": " << ec.message() << '\n';
		return -1;
	} else if(size_sum > INT_MAX) {
		return INT_MAX;
	} else {
		return size_sum;
	}
}

std::string base_name(const std::string& file, const bool remove_extension)
{
	if(!remove_extension) {
		return bfs::path(file).filename().string();
	} else {
		return bfs::path(file).stem().string();
	}
}

std::string directory_name(const std::string& file)
{
	return bfs::path(file).parent_path().string();
}

std::string nearest_extant_parent(const std::string& file)
{
	if(file.empty()) {
		return "";
	}

	bfs::path p{file};
	error_code ec;

	do {
		p = p.parent_path();
		bfs::path q = canonical(p, ec);
		if(!ec) {
			p = q;
		}
	} while(ec && !is_root(p.string()));

	return ec ? "" : p.string();
}

bool is_path_sep(char c)
{
	static const bfs::path sep = bfs::path("/").make_preferred();
	const std::string s = std::string(1, c);
	return sep == bfs::path(s).make_preferred();
}

char path_separator()
{
	return bfs::path::preferred_separator;
}

bool is_root(const std::string& path)
{
	error_code ec;
	const bfs::path& p = bfs::canonical(path, ec);
	return ec ? false : !p.has_parent_path();
}

std::string root_name(const std::string& path)
{
	return bfs::path{path}.root_name().string();
}

bool is_relative(const std::string& path)
{
	return bfs::path{path}.is_relative();
}

std::string normalize_path(const std::string& fpath, bool normalize_separators, bool resolve_dot_entries)
{
	if(fpath.empty()) {
		return fpath;
	}

	error_code ec;
	bfs::path p = resolve_dot_entries ? bfs::canonical(fpath, ec) : bfs::absolute(fpath, ec);

	if(ec) {
		return "";
	}

	if(normalize_separators) {
		return p.make_preferred().string();
	} else {
		return p.string();
	}
}

/**
 *  The paths manager is responsible for recording the various paths
 *  that binary files may be located at.
 *  It should be passed a config object which holds binary path information.
 *  This is in the format
 *@verbatim
 *    [binary_path]
 *      path=<path>
 *    [/binary_path]
 *  Binaries will be searched for in [wesnoth-path]/data/<path>/images/
 *@endverbatim
 */
namespace
{
std::set<std::string> binary_paths;

typedef std::map<std::string, std::vector<std::string>> paths_map;
paths_map binary_paths_cache;

} // namespace

static void init_binary_paths()
{
	if(binary_paths.empty()) {
		binary_paths.insert("");
	}
}

binary_paths_manager::binary_paths_manager()
	: paths_()
{
}

binary_paths_manager::binary_paths_manager(const game_config_view& cfg)
	: paths_()
{
	set_paths(cfg);
}

binary_paths_manager::~binary_paths_manager()
{
	cleanup();
}

void binary_paths_manager::set_paths(const game_config_view& cfg)
{
	cleanup();
	init_binary_paths();

	for(const config& bp : cfg.child_range("binary_path")) {
		std::string path = bp["path"].str();
		if(path.find("..") != std::string::npos) {
			ERR_FS << "Invalid binary path '" << path << "'\n";
			continue;
		}

		if(!path.empty() && path.back() != '/')
			path += "/";
		if(binary_paths.count(path) == 0) {
			binary_paths.insert(path);
			paths_.push_back(path);
		}
	}
}

void binary_paths_manager::cleanup()
{
	binary_paths_cache.clear();

	for(const std::string& p : paths_) {
		binary_paths.erase(p);
	}
}

void clear_binary_paths_cache()
{
	binary_paths_cache.clear();
}

static bool is_legal_file(const std::string& filename_str)
{
	DBG_FS << "Looking for '" << filename_str << "'.\n";

	if(filename_str.empty()) {
		LOG_FS << "  invalid filename\n";
		return false;
	}

	if(filename_str.find("..") != std::string::npos) {
		ERR_FS << "Illegal path '" << filename_str << "' (\"..\" not allowed).\n";
		return false;
	}

	if(filename_str.find('\\') != std::string::npos) {
		ERR_FS << "Illegal path '" << filename_str
			   << R"end(' ("\" not allowed, for compatibility with GNU/Linux and macOS).)end" << std::endl;
		return false;
	}

	bfs::path filepath(filename_str);

	if(default_blacklist.match_file(filepath.filename().string())) {
		ERR_FS << "Illegal path '" << filename_str << "' (blacklisted filename)." << std::endl;
		return false;
	}

	if(std::any_of(filepath.begin(), filepath.end(),
			   [](const bfs::path& dirname) { return default_blacklist.match_dir(dirname.string()); })) {
		ERR_FS << "Illegal path '" << filename_str << "' (blacklisted directory name)." << std::endl;
		return false;
	}

	return true;
}

/**
 * Returns a vector with all possible paths to a given type of binary,
 * e.g. 'images', 'sounds', etc,
 */
const std::vector<std::string>& get_binary_paths(const std::string& type)
{
	const paths_map::const_iterator itor = binary_paths_cache.find(type);
	if(itor != binary_paths_cache.end()) {
		return itor->second;
	}

	if(type.find("..") != std::string::npos) {
		// Not an assertion, as language.cpp is passing user data as type.
		ERR_FS << "Invalid WML type '" << type << "' for binary paths\n";
		static std::vector<std::string> dummy;
		return dummy;
	}

	std::vector<std::string>& res = binary_paths_cache[type];

	init_binary_paths();

	for(const std::string& path : binary_paths) {
#ifndef NANOHEX		
		res.push_back(get_user_data_dir() + "/" + path + type + "/");
#endif		
		res.push_back(game_config::path + "/" + path + type + "/");
	}

	// not found in "/type" directory, try main directory
#ifndef NANOHEX		
	res.push_back(get_user_data_dir() + "/");
#endif
	res.push_back(game_config::path + "/");

	return res;
}

std::string get_binary_file_location(const std::string& type, const std::string& filename)
{
	// We define ".." as "remove everything before" this is needed because
	// on the one hand allowing ".." would be a security risk but
	// especially for terrains the c++ engine puts a hardcoded "terrain/" before filename
	// and there would be no way to "escape" from "terrain/" otherwise. This is not the
	// best solution but we cannot remove it without another solution (subtypes maybe?).

	{
		std::string::size_type pos = filename.rfind("../");
		if(pos != std::string::npos) {
			return get_binary_file_location(type, filename.substr(pos + 3));
		}
	}

	if(!is_legal_file(filename)) {
		return std::string();
	}

	std::string result;
	for(const std::string& bp : get_binary_paths(type)) {
		bfs::path bpath(bp);
		bpath /= filename;

		if(bpath.extension() == ".wav" || bpath.extension() == ".ogg")
			bpath.replace_extension(".aac");

		DBG_FS << "  checking '" << bp << "'\n";

		if(file_exists(bpath)) {
			DBG_FS << "  found at '" << bpath.string() << "'\n";
			if(result.empty()) {
				result = bpath.string();
			} else {
				WRN_FS << "Conflicting files in binary_path: '" << sanitize_path(result)
					   << "' and '" << sanitize_path(bpath.string()) << "'\n";
			}
		}
	}

	DBG_FS << "  not found\n";
	return result;
}

std::string get_binary_dir_location(const std::string& type, const std::string& filename)
{
	if(!is_legal_file(filename)) {
		return std::string();
	}

	for(const std::string& bp : get_binary_paths(type)) {
		bfs::path bpath(bp);
		bpath /= filename;
		DBG_FS << "  checking '" << bp << "'\n";
		if(is_directory_internal(bpath)) {
			DBG_FS << "  found at '" << bpath.string() << "'\n";
			return bpath.string();
		}
	}

	DBG_FS << "  not found\n";
	return std::string();
}

std::string get_wml_location(const std::string& filename, const std::string& current_dir)
{
	if(!is_legal_file(filename)) {
		return std::string();
	}

	bfs::path fpath(filename[0] == '/' ? filename.substr(1) : filename);
	bfs::path result;

	if(filename[0] == '~') {
		result /= get_user_data_path() / "data" / filename.substr(1);
		DBG_FS << "  trying '" << result.string() << "'\n";
	} else if(*fpath.begin() == ".") {
		if(!current_dir.empty()) {
			result /= bfs::path(current_dir);
		} else {
			result /= bfs::path(game_config::path) / "data";
		}

		result /= fpath;
	} else if(fpath.parent_path() == "assets") {
		result = fpath;
	} else {
		result /= bfs::path(game_config::path) / "data" / fpath;
	}

	if(result.empty() || !file_exists(result)) {
		DBG_FS << "  not found\n";
		result.clear();
	} else {
		DBG_FS << "  found: '" << result.string() << "'\n";
	}

	return result.generic_string();
}

static bfs::path subtract_path(const bfs::path& full, const bfs::path& prefix)
{
	bfs::path::iterator fi = full.begin(), fe = full.end(), pi = prefix.begin(), pe = prefix.end();
	while(fi != fe && pi != pe && *fi == *pi) {
		++fi;
		++pi;
	}

	bfs::path rest;
	if(pi == pe) {
		while(fi != fe) {
			rest /= *fi;
			++fi;
		}
	}

	return rest;
}

std::string get_short_wml_path(const std::string& filename)
{
	bfs::path full_path(filename);

	bfs::path partial = subtract_path(full_path, get_user_data_path() / "data");
	if(!partial.empty()) {
		return "~" + partial.generic_string();
	}

	partial = subtract_path(full_path, bfs::path(game_config::path) / "data");
	if(!partial.empty()) {
		return partial.generic_string();
	}

	return filename;
}

std::string get_independent_binary_file_path(const std::string& type, const std::string& filename)
{
	bfs::path full_path(get_binary_file_location(type, filename));

	if(full_path.empty()) {
		return full_path.generic_string();
	}

	bfs::path partial = subtract_path(full_path, get_user_data_path());
	if(!partial.empty()) {
		return partial.generic_string();
	}

	partial = subtract_path(full_path, game_config::path);
	if(!partial.empty()) {
		return partial.generic_string();
	}

	return full_path.generic_string();
}

std::string get_program_invocation(const std::string& program_name)
{
	const std::string real_program_name(program_name
#ifdef DEBUG
										+ "-debug"
#endif
	);

	return (bfs::path(game_config::wesnoth_program_dir) / real_program_name).string();
}

std::string sanitize_path(const std::string& path)
{
#ifdef _WIN32
	const char* user_name = getenv("USERNAME");
#else
	const char* user_name = getenv("USER");
#endif

	std::string canonicalized = filesystem::normalize_path(path, true, false);
	if(user_name != nullptr) {
		boost::replace_all(canonicalized, user_name, "USER");
	}

	return canonicalized;
}

// Return path to localized counterpart of the given file, if any, or empty string.
// Localized counterpart may also be requested to have a suffix to base name.
std::string get_localized_path(const std::string& file, const std::string& suff)
{
	std::string dir = filesystem::directory_name(file);
	std::string base = filesystem::base_name(file);

	const std::size_t pos_ext = base.rfind(".");

	std::string loc_base;
	if(pos_ext != std::string::npos) {
		loc_base = base.substr(0, pos_ext) + suff + base.substr(pos_ext);
	} else {
		loc_base = base + suff;
	}

	// TRANSLATORS: This is the language code which will be used
	// to store and fetch localized non-textual resources, such as images,
	// when they exist. Normally it is just the code of the PO file itself,
	// e.g. "de" of de.po for German. But it can also be a comma-separated
	// list of language codes by priority, when the localized resource
	// found for first of those languages will be used. This is useful when
	// two languages share sufficient commonality, that they can use each
	// other's resources rather than duplicating them. For example,
	// Swedish (sv) and Danish (da) are such, so Swedish translator could
	// translate this message as "sv,da", while Danish as "da,sv".
	std::vector<std::string> langs = utils::split(_("language code for localized resources^en_US"));

	// In case even the original image is split into base and overlay,
	// add en_US with lowest priority, since the message above will
	// not have it when translated.
	langs.push_back("en_US");
	for(const std::string& lang : langs) {
		std::string loc_file = dir + "/" + "l10n" + "/" + lang + "/" + loc_base;
		if(filesystem::file_exists(loc_file)) {
			return loc_file;
		}
	}

	return "";
}

} // namespace filesystem
