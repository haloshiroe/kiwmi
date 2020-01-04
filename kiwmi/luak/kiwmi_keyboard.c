/* Copyright (c), Niclas Meyer <niclas@countingsort.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "luak/kiwmi_keyboard.h"

#include <lauxlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>

#include "input/keyboard.h"
#include "luak/kiwmi_lua_callback.h"

static int
l_kiwmi_keyboard_modifiers(lua_State *L)
{
    struct kiwmi_keyboard *keyboard =
        *(struct kiwmi_keyboard **)luaL_checkudata(L, 1, "kiwmi_keyboard");

    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->device->keyboard);

    lua_newtable(L);

    lua_pushboolean(L, modifiers & WLR_MODIFIER_SHIFT);
    lua_setfield(L, -2, "shift");

    lua_pushboolean(L, modifiers & WLR_MODIFIER_CAPS);
    lua_setfield(L, -2, "caps");

    lua_pushboolean(L, modifiers & WLR_MODIFIER_CTRL);
    lua_setfield(L, -2, "ctrl");

    lua_pushboolean(L, modifiers & WLR_MODIFIER_ALT);
    lua_setfield(L, -2, "alt");

    lua_pushboolean(L, modifiers & WLR_MODIFIER_MOD2);
    lua_setfield(L, -2, "mod2");

    lua_pushboolean(L, modifiers & WLR_MODIFIER_MOD3);
    lua_setfield(L, -2, "mod3");

    lua_pushboolean(L, modifiers & WLR_MODIFIER_LOGO);
    lua_setfield(L, -2, "super");

    lua_pushboolean(L, modifiers & WLR_MODIFIER_MOD5);
    lua_setfield(L, -2, "mod5");

    return 1;
}

static const luaL_Reg kiwmi_keyboard_methods[] = {
    {"modifiers", l_kiwmi_keyboard_modifiers},
    {"on", luaK_callback_register_dispatch},
    {NULL, NULL},
};

static void
kiwmi_keyboard_on_key_down_or_up_notify(
    struct wl_listener *listener,
    void *data)
{
    struct kiwmi_lua_callback *lc = wl_container_of(listener, lc, listener);
    struct kiwmi_server *server   = lc->server;
    lua_State *L                  = server->lua->L;
    struct kiwmi_keyboard_key_event *event = data;
    struct kiwmi_keyboard *keyboard        = event->keyboard;

    const xkb_keysym_t *syms = event->syms;
    int nsyms                = event->nsyms;

    char keysym_name[64];

    for (int i = 0; i < nsyms; ++i) {
        xkb_keysym_t sym = syms[i];

        int namelen =
            xkb_keysym_get_name(sym, keysym_name, sizeof(keysym_name));

        lua_rawgeti(L, LUA_REGISTRYINDEX, lc->callback_ref);

        lua_newtable(L);

        lua_pushlstring(L, keysym_name, namelen);
        lua_setfield(L, -2, "key");

        lua_pushcfunction(L, luaK_kiwmi_keyboard_new);
        lua_pushlightuserdata(L, keyboard);
        if (lua_pcall(L, 1, 1, 0)) {
            wlr_log(WLR_ERROR, "%s", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        lua_setfield(L, -2, "keyboard");

        if (lua_pcall(L, 1, 1, 0)) {
            wlr_log(WLR_ERROR, "%s", lua_tostring(L, -1));
            lua_pop(L, 1);
        }

        event->handled |= lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
}

static int
l_kiwmi_keyboard_on_key_down(lua_State *L)
{
    struct kiwmi_keyboard *keyboard =
        *(struct kiwmi_keyboard **)luaL_checkudata(L, 1, "kiwmi_keyboard");
    luaL_checktype(L, 2, LUA_TFUNCTION);

    struct kiwmi_server *server = keyboard->server;

    lua_pushcfunction(L, luaK_kiwmi_lua_callback_new);
    lua_pushlightuserdata(L, server);
    lua_pushvalue(L, 2);
    lua_pushlightuserdata(L, kiwmi_keyboard_on_key_down_or_up_notify);
    lua_pushlightuserdata(L, &keyboard->events.key_down);

    if (lua_pcall(L, 4, 1, 0)) {
        wlr_log(WLR_ERROR, "%s", lua_tostring(L, -1));
        return 0;
    }

    return 1;
}

static int
l_kiwmi_keyboard_on_key_up(lua_State *L)
{
    struct kiwmi_keyboard *keyboard =
        *(struct kiwmi_keyboard **)luaL_checkudata(L, 1, "kiwmi_keyboard");
    luaL_checktype(L, 2, LUA_TFUNCTION);

    struct kiwmi_server *server = keyboard->server;

    lua_pushcfunction(L, luaK_kiwmi_lua_callback_new);
    lua_pushlightuserdata(L, server);
    lua_pushvalue(L, 2);
    lua_pushlightuserdata(L, kiwmi_keyboard_on_key_down_or_up_notify);
    lua_pushlightuserdata(L, &keyboard->events.key_up);

    if (lua_pcall(L, 4, 1, 0)) {
        wlr_log(WLR_ERROR, "%s", lua_tostring(L, -1));
        return 0;
    }

    return 1;
}

static const luaL_Reg kiwmi_keyboard_events[] = {
    {"key_down", l_kiwmi_keyboard_on_key_down},
    {"key_up", l_kiwmi_keyboard_on_key_up},
    {NULL, NULL},
};

int
luaK_kiwmi_keyboard_new(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TLIGHTUSERDATA); // kiwmi_keyboard

    struct kiwmi_keyboard *keyboard = lua_touserdata(L, 1);

    struct kiwmi_keyboard **keyboard_ud =
        lua_newuserdata(L, sizeof(*keyboard_ud));
    luaL_getmetatable(L, "kiwmi_keyboard");
    lua_setmetatable(L, -2);

    *keyboard_ud = keyboard;

    return 1;
}

int
luaK_kiwmi_keyboard_register(lua_State *L)
{
    luaL_newmetatable(L, "kiwmi_keyboard");

    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, kiwmi_keyboard_methods, 0);

    luaL_newlib(L, kiwmi_keyboard_events);
    lua_setfield(L, -2, "__events");

    return 0;
}
