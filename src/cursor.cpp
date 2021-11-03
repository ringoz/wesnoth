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
 * Support for different cursors-shapes.
 */

#include "cursor.hpp"

#include "picture.hpp"
#include "preferences/game.hpp"
#include "sdl/utils.hpp"

#include <boost/logic/tribool.hpp>

#include <array>
#include <memory>

namespace cursor
{
namespace
{

cursor::CURSOR_TYPE current_cursor = cursor::NORMAL;

bool have_focus = true;

SDL_Cursor* get_cursor(cursor::CURSOR_TYPE type)
{
	switch (type)
	{
		case NORMAL: {
			static SDL_Cursor* cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
			return cursor;
		}
		case WAIT: {
			static SDL_Cursor* cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
			return cursor;
		}
		case IBEAM: {
			static SDL_Cursor* cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
			return cursor;
		}
		case MOVE: {
			static SDL_Cursor* cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
			return cursor;
		}
		case ATTACK: {
			static SDL_Cursor* cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
			return cursor;
		}
		case HYPERLINK: {
			static SDL_Cursor* cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
			return cursor;
		}
		case MOVE_DRAG: {
			static SDL_Cursor* cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
			return cursor;
		}
		case ATTACK_DRAG: {
			static SDL_Cursor* cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
			return cursor;
		}
		default:
			return nullptr;
	}
}

} // end anon namespace

manager::manager()
{
	SDL_ShowCursor(SDL_ENABLE);
	set();
}

manager::~manager()
{
	SDL_ShowCursor(SDL_ENABLE);
}

void set(CURSOR_TYPE type)
{
	// Change only if it's a valid cursor
	if(type != NUM_CURSORS) {
		current_cursor = type;
	} else if(current_cursor == NUM_CURSORS) {
		// Except if the current one is also invalid.
		// In this case, change to a valid one.
		current_cursor = NORMAL;
	}

	SDL_Cursor* cursor_image = get_cursor(current_cursor);

	// Causes problem on Mac:
	// if (cursor_image != nullptr && cursor_image != SDL_GetCursor())
	SDL_SetCursor(cursor_image);

	SDL_ShowCursor(SDL_ENABLE);
}

void set_dragging(bool drag)
{
	switch(current_cursor) {
	case MOVE:
		if(drag) cursor::set(MOVE_DRAG);
		break;
	case ATTACK:
		if(drag) cursor::set(ATTACK_DRAG);
		break;
	case MOVE_DRAG:
		if(!drag) cursor::set(MOVE);
		break;
	case ATTACK_DRAG:
		if(!drag) cursor::set(ATTACK);
		break;
	default:
		break;
	}
}

CURSOR_TYPE get()
{
	return current_cursor;
}

void set_focus(bool focus)
{
	have_focus = focus;

	if(!focus) {
		set();
	}
}

setter::setter(CURSOR_TYPE type)
	: old_(current_cursor)
{
	set(type);
}

setter::~setter()
{
	set(old_);
}

} // end namespace cursor
