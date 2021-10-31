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
#include "gettext.hpp"
#include "log.hpp"

#include <algorithm>
#include <iomanip>
#include <libintl.h>

#define DBG_G LOG_STREAM(debug, lg::general())
#define LOG_G LOG_STREAM(info, lg::general())
#define WRN_G LOG_STREAM(warn, lg::general())
#define ERR_G LOG_STREAM(err, lg::general())

namespace 
{
	std::string current_language_;

	// Converts ASCII letters to lowercase. Ignores Unicode letters.
	std::string ascii_to_lowercase(const std::string& str)
	{
		std::string result;
		result.reserve(str.length());
		std::transform(str.begin(), str.end(), std::back_inserter(result), [](char c)
		{
			return c >= 'A' && c <= 'Z' ? c | 0x20 : c;
		});
		return result;
	}
}

namespace translation
{

std::string dgettext(const char* domain, const char* msgid)
{
	return ::dgettext(domain, msgid);
}
std::string egettext(char const *msgid)
{
	return msgid[0] == '\0' ? msgid : ::gettext(msgid);
}

std::string dsgettext (const char * domainname, const char *msgid)
{
	std::string msgval = ::dgettext (domainname, msgid);
	if (msgval == msgid) {
		const char* firsthat = std::strchr (msgid, '^');
		if (firsthat == nullptr)
			msgval = msgid;
		else
			msgval = firsthat + 1;
	}
	return msgval;
}

namespace {

inline const char* is_unlocalized_string2(const std::string& str, const char* singular, const char* plural)
{
	if (str == singular) {
		return singular;
	}

	if (str == plural) {
		return plural;
	}

	return nullptr;
}

}

std::string dsngettext (const char * domainname, const char *singular, const char *plural, int n)
{
	//TODO: only the next line needs to be in the lock.
	std::string msgval = ::dngettext(domainname, singular, plural, n);
	auto original = is_unlocalized_string2(msgval, singular, plural);
	if (original) {
		const char* firsthat = std::strchr (original, '^');
		if (firsthat == nullptr)
			msgval = original;
		else
			msgval = firsthat + 1;
	}
	return msgval;
}

void bind_textdomain(const char* domain, const char* directory, const char* /*encoding*/)
{
	LOG_G << "adding textdomain '" << domain << "' in directory '" << directory << "'\n";
	::bindtextdomain(domain, directory);
}

void set_default_textdomain(const char* domain)
{
	LOG_G << "set_default_textdomain: '" << domain << "'\n";
	::textdomain(domain);
}


void set_language(const std::string& language, const std::vector<std::string>* /*alternates*/)
{
	// why should we need alternates? which languages we support should only be related
	// to which languages we ship with and not which the os supports
	LOG_G << "setting language to  '" << language << "' \n";
	current_language_ = language;
}

int compare(const std::string& s1, const std::string& s2)
{
	return s1.compare(s2);
}

int icompare(const std::string& s1, const std::string& s2)
{
	return compare(ascii_to_lowercase(s1), ascii_to_lowercase(s2));
}

std::string strftime(const std::string& format, const std::tm* time)
{
	std::basic_ostringstream<char> dummy;
	dummy << std::put_time(time, format.c_str());
	return dummy.str();
}

bool ci_search(const std::string& s1, const std::string& s2)
{
	std::string ls1 = ascii_to_lowercase(s1);
	std::string ls2 = ascii_to_lowercase(s2);

	return std::search(ls1.begin(), ls1.end(),
	                   ls2.begin(), ls2.end()) != ls1.end();
}

const std::string& get_effective_locale_info()
{
	return current_language_;
}
}
