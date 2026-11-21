#include "pch_script.h"
#include "console_registrator.h"
#include "../xrEngine/xr_ioconsole.h"

using namespace luabind;

CConsole* console() { return Console; }

int get_console_integer(CConsole* c, LPCSTR cmd) {
    int min = 0, max = 0;
    int val = c->GetInteger(cmd, min, max);
    return val;
}

float get_console_float(CConsole* c, LPCSTR cmd) {
    float min = 0.0f, max = 0.0f;
    float val = c->GetFloat(cmd, min, max);
    return val;
}

bool get_console_bool(CConsole* c, LPCSTR cmd) { return c->GetBool(cmd); }

void execute_console_command_deferred(CConsole* c, LPCSTR string_to_execute) {
    Engine.Event.Defer("KERNEL:console", size_t(xr_strdup(string_to_execute)));
}

static void console_execute(lua_State* L, CConsole* c, LPCSTR cmd) {
    string256 src = "";
    lua_Debug ar;
    for (int level = 1; level <= 8 && lua_getstack(L, level, &ar); ++level) {
        if (!lua_getinfo(L, "Sl", &ar))
            break;

        if (1 == level)
            xr_sprintf(src, "%s:%d", ar.short_src, ar.currentline);

        if ('C' == ar.what[0])
            continue;

        const u32 tail = sizeof("_g.script") - 1;
        const u32 len = xr_strlen(ar.short_src);
        if (len >= tail && 0 == xr_strcmp(ar.short_src + len - tail, "_g.script") &&
            (len == tail || '\\' == ar.short_src[len - tail - 1] ||
             '/' == ar.short_src[len - tail - 1]))
            continue;

        xr_sprintf(src, "%s:%d", ar.short_src, ar.currentline);
        break;
    }

    CConsole::ScriptCallerScope scope(src);
    c->Execute(cmd);
}

#pragma optimize("s", on)
void console_registrator::script_register(lua_State* L) {
    module(L)[def("get_console", &console),

              class_<CConsole>("CConsole")
                  .def("execute", &console_execute, raw<1>())
                  .def("execute_script", &CConsole::ExecuteScript)
                  .def("show", &CConsole::Show)
                  .def("hide", &CConsole::Hide)

                  .def("get_string", &CConsole::GetString)
                  .def("get_integer", &get_console_integer)
                  .def("get_bool", &get_console_bool)
                  .def("get_float", &get_console_float)
                  .def("get_token", &CConsole::GetToken)
                  .def("execute_deferred", &execute_console_command_deferred)];
}