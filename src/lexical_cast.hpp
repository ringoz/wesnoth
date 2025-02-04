/*
	Copyright (C) 2009 - 2021
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

/**
 * @file
 * New lexcical_cast header.
 *
 * For debugging you can include this header _in_ a namespace (to honor ODR)
 * and have a set of functions that throws exceptions instead of doing the
 * real job. This is done for the unit tests but should normally not be done.
 */

#ifdef LEXICAL_CAST_DEBUG
#undef LEXICAL_CAST_HPP_INCLUDED
#endif

#ifndef LEXICAL_CAST_HPP_INCLUDED
#define LEXICAL_CAST_HPP_INCLUDED

#ifdef LEXICAL_CAST_DEBUG

#undef DEBUG_THROW
/**
 * Throws an exception for debugging.
 *
 * @param id                      The unique name to identify the function.
 *                                @note this name is a user defined string and
 *                                should not be modified once used!
 */
#define DEBUG_THROW(id) throw id;
#else

#ifdef __FreeBSD__
#define __LONG_LONG_SUPPORTED
#endif

#include <optional>

#include <cstdlib>
#include <limits>
#include <string>
#include <sstream>
#include <type_traits>

#define DEBUG_THROW(id)
#endif

/**
 * @namespace implementation
 * Contains the implementation details for lexical_cast and shouldn't be used
 * directly.
 */
namespace implementation {

	template<
		  typename To
		, typename From
		, typename ToEnable = void
		, typename FromEnable = void
	>
	struct lexical_caster;

} // namespace implementation

/**
 * Lexical cast converts one type to another.
 *
 * @tparam To                     The type to convert to.
 * @tparam From                   The type to convert from.
 *
 * @param value                   The value to convert.
 *
 * @returns                       The converted value.
 *
 * @throw                         bad_lexical_cast if the cast was unsuccessful.
 */
template<typename To, typename From>
inline To lexical_cast(From value)
{
	return implementation::lexical_caster<To, From>().operator()(value, std::nullopt);
}

/**
 * Lexical cast converts one type to another with a fallback.
 *
 * @tparam To                     The type to convert to.
 * @tparam From                   The type to convert from.
 *
 * @param value                   The value to convert.
 * @param fallback                The fallback value to return if the cast fails.
 *
 * @returns                       The converted value.
 */
template<typename To, typename From>
inline To lexical_cast_default(From value, To fallback = To())
{
	return implementation::lexical_caster<To, From>().operator()(value, fallback);
}

/** Thrown when a lexical_cast fails. */
struct bad_lexical_cast : std::exception
{
	const char* what() const noexcept
	{
		return "bad_lexical_cast";
	}
};

namespace implementation {

/**
 * Base class for the conversion.
 *
 * Since functions can't be partially specialized we use a class, which can be
 * partially specialized for the conversion.
 *
 * @tparam To                     The type to convert to.
 * @tparam From                   The type to convert from.
 * @tparam ToEnable               Filter to enable the To type.
 * @tparam FromEnable             Filter to enable the From type.
 */
template<
	  typename To
	, typename From
	, typename ToEnable
	, typename FromEnable
>
struct lexical_caster
{
	To operator()(From value, std::optional<To> fallback) const
	{
		DEBUG_THROW("generic");

		To result = To();
		std::stringstream sstr;

		if(!(sstr << value && sstr >> result)) {
			if(fallback) { return *fallback; }

			throw bad_lexical_cast();
		} else {
			return result;
		}
	}
};

/**
 * Specialized conversion class.
 *
 * Specialized for returning strings from an integral type or a pointer to an
 * integral type.
 */
template <typename From>
struct lexical_caster<
	  std::string
	, From
	, void
	, std::enable_if_t<std::is_integral_v<std::remove_pointer_t<From>>>
>
{
	std::string operator()(From value, std::optional<std::string>) const
	{
		DEBUG_THROW("specialized - To std::string - From integral (pointer)");

		std::stringstream sstr;
		sstr << value;
		return sstr.str();
	}
};

/**
 * Specialized conversion class.
 *
 * Specialized for returning a long long from a (const) char*.
 * @note is separate from the other signed types since a long long has a
 * performance penalty at 32 bit systems.
 */
template <class From>
struct lexical_caster<
	  long long
	, From
	, void
	, std::enable_if_t<std::is_same_v<From, const char*> || std::is_same_v<From, char*>>
	>
{
	long long operator()(From value, std::optional<long long> fallback) const
	{
		DEBUG_THROW("specialized - To long long - From (const) char*");

		if(fallback) {
			return lexical_cast_default<long long>(std::string(value), *fallback);
		} else {
			return lexical_cast<long long>(std::string(value));
		}
	}
};

/**
 * Specialized conversion class.
 *
 * Specialized for returning a long long from a std::string.
 * @note is separate from the other signed types since a long long has a
 * performance penalty at 32 bit systems.
 */
template <>
struct lexical_caster<
	  long long
	, std::string
	>
{
	long long operator()(const std::string& value, std::optional<long long> fallback) const
	{
		DEBUG_THROW("specialized - To long long - From std::string");

		try {
			return std::stoll(value);
		} catch(const std::invalid_argument&) {
		} catch(const std::out_of_range&) {
		}

		if(fallback) {
			return *fallback;
		} else {
			throw bad_lexical_cast();
		}
	}
};

/**
 * Specialized conversion class.
 *
 * Specialized for returning a signed type from a (const) char*.
 */
template <class To, class From>
struct lexical_caster<
	  To
	, From
	, std::enable_if_t<std::is_integral_v<To> && std::is_signed_v<To> && !std::is_same_v<To, long long>>
	, std::enable_if_t<std::is_same_v<From, const char*> || std::is_same_v<From, char*>>
	>
{
	To operator()(From value, std::optional<To> fallback) const
	{
		DEBUG_THROW("specialized - To signed - From (const) char*");

		if(fallback) {
			return lexical_cast_default<To>(std::string(value), *fallback);
		} else {
			return lexical_cast<To>(std::string(value));
		}
	}
};

/**
 * Specialized conversion class.
 *
 * Specialized for returning a signed type from a std::string.
 */
template <class To>
struct lexical_caster<
	  To
	, std::string
	, std::enable_if_t<std::is_integral_v<To> && std::is_signed_v<To> && !std::is_same_v<To, long long>>
	>
{
	To operator()(const std::string& value, std::optional<To> fallback) const
	{
		DEBUG_THROW("specialized - To signed - From std::string");

    int& _Errno_ref = errno; _Errno_ref = 0;
    const char* _Ptr = value.c_str(); char* _Eptr;

		long res = std::strtol(_Ptr, &_Eptr, 10);
		if (_Ptr != _Eptr && _Errno_ref != ERANGE) {
			if(std::numeric_limits<To>::lowest() <= res && std::numeric_limits<To>::max() >= res) {
				return static_cast<To>(res);
			}
		}

		if(fallback) {
			return *fallback;
		} else {
			throw bad_lexical_cast();
		}
	}
};

/**
 * Specialized conversion class.
 *
 * Specialized for returning a floating point type from a (const) char*.
 */
template <class To, class From>
struct lexical_caster<
	  To
	, From
	, std::enable_if_t<std::is_floating_point_v<To>>
	, std::enable_if_t<std::is_same_v<From, const char*> || std::is_same_v<From, char*>>
	>
{
	To operator()(From value, std::optional<To> fallback) const
	{
		DEBUG_THROW("specialized - To floating point - From (const) char*");

		if(fallback) {
			return lexical_cast_default<To>(std::string(value), *fallback);
		} else {
			return lexical_cast<To>(std::string(value));
		}
	}
};

/**
 * Specialized conversion class.
 *
 * Specialized for returning a floating point type from a std::string.
 */
template <class To>
struct lexical_caster<
	  To
	, std::string
	, std::enable_if_t<std::is_floating_point_v<To>>
	>
{
	To operator()(const std::string& value, std::optional<To> fallback) const
	{
		DEBUG_THROW("specialized - To floating point - From std::string");

		// Explicitly reject hexadecimal values. Unit tests of the config class require that.
		if(value.find_first_of("Xx") != std::string::npos) {
			if(fallback) {
				return *fallback;
			} else {
				throw bad_lexical_cast();
			}
		}

		try {
			long double res = std::stold(value);
			if((static_cast<long double>(std::numeric_limits<To>::lowest()) <= res) && (static_cast<long double>(std::numeric_limits<To>::max()) >= res)) {
				return static_cast<To>(res);
			}
		} catch(const std::invalid_argument&) {
		} catch(const std::out_of_range&) {
		}

		if(fallback) {
			return *fallback;
		} else {
			throw bad_lexical_cast();
		}
	}
};

/**
 * Specialized conversion class.
 *
 * Specialized for returning a unsigned long long from a (const) char*.
 * @note is separate from the other unsigned types since a unsigned long long
 * has a performance penalty at 32 bit systems.
 */
template <class From>
struct lexical_caster<
	  unsigned long long
	, From
	, void
	, std::enable_if_t<std::is_same_v<From, const char*> || std::is_same_v<From, char*>>
	>
{
	unsigned long long operator()(From value, std::optional<unsigned long long> fallback) const
	{
		DEBUG_THROW(
				"specialized - To unsigned long long - From (const) char*");

		if(fallback) {
			return lexical_cast_default<unsigned long long>(std::string(value), *fallback);
		} else {
			return lexical_cast<unsigned long long>(std::string(value));
		}
	}
};

/**
 * Specialized conversion class.
 *
 * Specialized for returning a unsigned long long from a std::string.
 * @note is separate from the other unsigned types since a unsigned long long
 * has a performance penalty at 32 bit systems.
 */
template <>
struct lexical_caster<
	  unsigned long long
	, std::string
	>
{
	unsigned long long operator()(const std::string& value, std::optional<unsigned long long> fallback) const
	{
		DEBUG_THROW("specialized - To unsigned long long - From std::string");

		try {
			return std::stoull(value);
		} catch(const std::invalid_argument&) {
		} catch(const std::out_of_range&) {
		}

		if(fallback) {
			return *fallback;
		} else {
			throw bad_lexical_cast();
		}
	}
};

/**
 * Specialized conversion class.
 *
 * Specialized for returning a unsigned type from a (const) char*.
 */
template <class To, class From>
struct lexical_caster<
	  To
	, From
	, std::enable_if_t<std::is_unsigned_v<To> && !std::is_same_v<To, unsigned long long>>
	, std::enable_if_t<std::is_same_v<From, const char*> || std::is_same_v<From, char*>>
	>
{
	To operator()(From value, std::optional<To> fallback) const
	{
		DEBUG_THROW("specialized - To unsigned - From (const) char*");

		if(fallback) {
			return lexical_cast_default<To>(std::string(value), *fallback);
		} else {
			return lexical_cast<To>(std::string(value));
		}
	}
};

/**
 * Specialized conversion class.
 *
 * Specialized for returning a unsigned type from a std::string.
 */
template <class To>
struct lexical_caster<
	  To
	, std::string
	, std::enable_if_t<std::is_unsigned_v<To>>
	>
{
	To operator()(const std::string& value, std::optional<To> fallback) const
	{
		DEBUG_THROW("specialized - To unsigned - From std::string");

    int& _Errno_ref = errno; _Errno_ref = 0;
    const char* _Ptr = value.c_str(); char* _Eptr;
		
		unsigned long res = std::strtoul(_Ptr, &_Eptr, 10);
		if (_Ptr != _Eptr && _Errno_ref != ERANGE) {
			// No need to check the lower bound, it's zero for all unsigned types.
			if(std::numeric_limits<To>::max() >= res) {
				return static_cast<To>(res);
			}
		}

		if(fallback) {
			return *fallback;
		} else {
			throw bad_lexical_cast();
		}
	}
};

/**
 * Specialized conversion class.
 *
 * Specialized for returning a bool from a std::string.
 * @note is specialized to silence C4804 from MSVC.
 */
template <>
struct lexical_caster<bool, std::string>
{
	bool operator()(const std::string& value, std::optional<bool>) const
	{
		DEBUG_THROW("specialized - To bool - From std::string");

		return value == "1";
	}
};

/**
 * Specialized conversion class.
 *
 * Specialized for returning a bool from a (const) char*.
 * @note is specialized to silence C4804 from MSVC.
 */
template <class From>
struct lexical_caster<
	  bool
	, From
	, void
	, std::enable_if_t<std::is_same_v<From, const char*> || std::is_same_v<From, char*>>
	>
{
	bool operator()(From value, std::optional<bool>) const
	{
		DEBUG_THROW("specialized - To bool - From (const) char*");

		return lexical_cast<bool>(std::string(value));
	}
};

} // namespace implementation

#endif
