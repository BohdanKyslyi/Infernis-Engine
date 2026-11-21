#include "pch_script.h"

#ifdef _MSC_VER
#include <luabind/detail/class_registry.hpp>
#endif

#include "LuaWorkbench.h"

extern "C" {
#include <lua/lauxlib.h>
#include "../../../3rd-party/luajit/src/luajit.h"
}

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string_view>

namespace lua_workbench {
namespace {

constexpr char WrapperName[] = "=lua_workbench_runner";
constexpr char Wrapper[] = "local chunk, guard, handler = ...; return guard(chunk, handler)";
constexpr int HookInterval = 1000;
constexpr unsigned MaxBlocks = 500;
constexpr unsigned MaxOutput = 256;
constexpr unsigned MaxFrames = 32;
constexpr unsigned MaxVariables = 48;
thread_local Session* running = nullptr;

unsigned long long NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void RawGet(lua_State* state, int table, const std::string& key) {
    if (table < 0 && table > LUA_REGISTRYINDEX)
        table = lua_gettop(state) + table + 1;
    lua_pushlstring(state, key.data(), key.size());
    lua_rawget(state, table);
}

std::string Value(lua_State* state, int index, bool expand = false) {
    const int type = lua_type(state, index);
    switch (type) {
    case LUA_TSTRING:
    case LUA_TNUMBER: {
        size_t size = 0;
        const char* text = lua_tolstring(state, index, &size);
        const bool quoted = type == LUA_TSTRING;
        std::string value = quoted ? "\"" : "";
        for (size_t i = 0; i < (std::min)(size, size_t(120)); ++i) {
            switch (text[i]) {
            case '\n': value += "\\n"; break;
            case '\r': value += "\\r"; break;
            case '\t': value += "\\t"; break;
            case '\0': value += "\\0"; break;
            default: value += text[i]; break;
            }
        }
        if (size > 120)
            value += "...";
        return quoted ? value + "\"" : value;
    }
    case LUA_TBOOLEAN: return lua_toboolean(state, index) ? "true" : "false";
    case LUA_TNIL: return "nil";
    case LUA_TTABLE:
        if (expand && lua_checkstack(state, 3)) {
            if (index < 0)
                index = lua_gettop(state) + index + 1;
            std::string value = "{";
            lua_pushnil(state);
            for (unsigned i = 0; lua_next(state, index); ++i) {
                if (i == 5) {
                    value += ", ...";
                    lua_pop(state, 2);
                    break;
                }
                if (i)
                    value += ", ";
                // Keep the key intact for lua_next. Converting numbers changes their type.
                lua_pushvalue(state, -2);
                value += Value(state, -1) + " = ";
                lua_pop(state, 1);
                value += Value(state, -1);
                lua_pop(state, 1);
            }
            return value + "}";
        }
        return "<table>";
    default: return std::string("<") + lua_typename(state, type) + ">";
    }
}

std::string ErrorText(lua_State* state, int index) {
    if (lua_type(state, index) != LUA_TSTRING)
        return Value(state, index, true);
    size_t size = 0;
    const char* text = lua_tolstring(state, index, &size);
    return std::string(text, (std::min)(size, size_t(4096)));
}

bool SameFile(std::string_view source, std::string_view name) {
    if (source.empty() || source.front() != '@')
        return false;
    source.remove_prefix(1);
    const auto slash = source.find_last_of("/\\");
    if (slash != std::string_view::npos)
        source.remove_prefix(slash + 1);
    return source.size() == name.size() && std::equal(source.begin(), source.end(), name.begin(),
        [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
}

bool JitEnabled(lua_State* state) {
    const int top = lua_gettop(state);
    bool enabled = false;
    RawGet(state, LUA_GLOBALSINDEX, "jit");
    if (lua_istable(state, -1)) {
        RawGet(state, -1, "status");
        if (lua_isfunction(state, -1) && lua_pcall(state, 0, 1, 0) == 0)
            enabled = lua_toboolean(state, -1) != 0;
    }
    lua_settop(state, top);
    return enabled;
}

int StackDepth(lua_State* thread, bool& nativeFrame) {
    int depth = 0;
    lua_Debug frame{};
    for (int i = 0; lua_getstack(thread, i, &frame); ++i) {
        lua_getinfo(thread, "f", &frame);
        nativeFrame |= lua_tocfunction(thread, -1) != nullptr;
        if (!lua_iscfunction(thread, -1))
            ++depth;
        lua_pop(thread, 1);
    }
    return depth;
}

bool IsIdentifierChar(unsigned char c) {
    return std::isalnum(c) != 0 || c == '_';
}

bool IsIdentifier(const std::string& word) {
    return !word.empty() && !std::isdigit(static_cast<unsigned char>(word[0])) &&
        std::all_of(word.begin(), word.end(), IsIdentifierChar);
}


void Snapshot(std::vector<Frame>& frames, lua_State* thread, int firstFrame,
                     const std::string& chunk, bool live) {
    frames.clear();
    if (!lua_checkstack(thread, 12))
        return;

    lua_Debug debug{};
    for (int level = firstFrame; frames.size() < MaxFrames && lua_getstack(thread, level, &debug);
         ++level) {
        if (!lua_getinfo(thread, "nSlf", &debug))
            continue;
        const bool wrapper = debug.source && !std::strcmp(debug.source, WrapperName);
        if (!wrapper) {
            Frame frame;
            frame.source = debug.source && debug.source[0] == '@' ? debug.source + 1 : debug.short_src;
            frame.line = debug.currentline;
            frame.function = debug.name ? debug.name :
                (debug.what && !std::strcmp(debug.what, "main") ? "<chunk>" : "<anonymous>");
            frame.editorSource = debug.source && (live ? SameFile(debug.source, chunk) : chunk == debug.source);
            for (int i = 1; i <= static_cast<int>(MaxVariables); ++i) {
                const char* name = lua_getlocal(thread, &debug, i);
                if (!name)
                    break;
                if (*name != '(')
                    frame.variables.push_back(std::string("local ") + name + " = " + Value(thread, -1, true));
                lua_pop(thread, 1);
            }
            for (int i = 1; frame.variables.size() < MaxVariables; ++i) {
                const char* name = lua_getupvalue(thread, -1, i);
                if (!name)
                    break;
                frame.variables.push_back(std::string("upvalue ") + name + " = " + Value(thread, -1, true));
                lua_pop(thread, 1);
            }
            frames.push_back(std::move(frame));
        }
        lua_pop(thread, 1); // function pushed by lua_getinfo
    }
}

} // namespace

Session::~Session() { ReleaseThread(); }

void Session::ReleaseThread() {
    if (m_host)
        luaL_unref(m_host, LUA_REGISTRYINDEX, m_reference);
    m_host = nullptr;
    m_thread = nullptr;
    m_reference = LUA_NOREF;
}

void Session::ResetOutput() {
    m_output.clear();
    m_frames.clear();
    m_errorLine = 0;
    m_state = State::Idle;
}

void Session::Write(std::string text) {
    if (m_output.size() < MaxOutput)
        m_output.push_back(std::move(text));
    else if (m_output.size() == MaxOutput)
        m_output.emplace_back("[Further output omitted]");
}

void Session::ReportError(const std::string& text) {
    m_state = State::Failed;
    for (const Frame& frame : m_frames) {
        if (frame.editorSource && frame.line > 0) {
            m_errorLine = frame.line;
            break;
        }
    }
    if (!m_errorLine && m_frames.empty() && text.compare(0, 8, "Syntax: ") == 0) {
        // Lua may abbreviate a long chunk name. The first colon followed by a
        // line number and another colon is the location in its diagnostic.
        for (size_t i = text.find(':'); i != std::string::npos; i = text.find(':', i + 1)) {
            const char* first = text.c_str() + i + 1;
            if (!std::isdigit(static_cast<unsigned char>(*first)))
                continue;
            char* end = nullptr;
            const long line = std::strtol(first, &end, 10);
            if (*end == ':' && line > 0 && line <= static_cast<long>(MaxSource)) {
                m_errorLine = static_cast<int>(line);
                break;
            }
        }
    }
    // Keep the error visible even if the script filled the output buffer.
    if (m_output.size() >= MaxOutput)
        m_output.resize(MaxOutput - 1);
    Write(text);
}

bool Session::Compile(lua_State* target, const std::string& source, const std::string& name) {
    m_chunk = "@" + name;
    if (source.size() > MaxSource || source.find('\0') != std::string::npos) {
        ReportError("Source must be text and at most 256 KiB.");
        return false;
    }
    const size_t bom = source.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
    if (luaL_loadbufferx(target, source.data() + bom, source.size() - bom, m_chunk.c_str(), "t")) {
        ReportError("Syntax: " + ErrorText(target, -1));
        lua_pop(target, 1);
        return false;
    }
    return true;
}

bool Session::Check(lua_State* host, const std::string& source, const std::string& name) {
    if (m_thread)
        return false;
    ResetOutput();
    if (!Compile(host, source, name))
        return false;
    lua_pop(host, 1);
    Write("Syntax OK");
    return true;
}

void Session::Start(lua_State* host, const std::string& source, const std::string& name,
                    const std::set<int>& breakpoints, Step step) {
    Stop();
    ResetOutput();
    m_host = host;
    m_thread = lua_newthread(host);
    m_reference = luaL_ref(host, LUA_REGISTRYINDEX);
    m_breakpoints = breakpoints;
    m_started = false;
    m_depth = 0;

    if (luaL_loadbufferx(m_thread, Wrapper, sizeof(Wrapper) - 1, WrapperName, "t") ||
        !Compile(m_thread, source, name)) {
        if (m_state != State::Failed)
            ReportError("Could not load workbench runner: " + ErrorText(m_thread, -1));
        ReleaseThread();
        return;
    }

    luaJIT_setmode(m_thread, -1, LUAJIT_MODE_ALLFUNC | LUAJIT_MODE_OFF);

    // Assignments go into this run's environment. Engine globals and lazy
    // script namespaces are read through the original global table.
    lua_newtable(m_thread);
    lua_pushvalue(m_thread, -1);
    lua_setfield(m_thread, -2, "_G");
    lua_pushcfunction(m_thread, Print);
    lua_setfield(m_thread, -2, "print");
    lua_newtable(m_thread);
    lua_pushvalue(m_thread, LUA_GLOBALSINDEX);
    lua_setfield(m_thread, -2, "__index");
    lua_setmetatable(m_thread, -2);
    lua_setfenv(m_thread, -2);

    RawGet(m_thread, LUA_GLOBALSINDEX, "xpcall");
    if (!lua_isfunction(m_thread, -1)) {
        ReportError("The game Lua state has no xpcall function.");
        ReleaseThread();
        return;
    }
    lua_pushcfunction(m_thread, ErrorHandler);
    Resume(step);
}

void Session::Resume(Step step) {
    if (!m_thread || running)
        return;
    m_frames.clear();
    m_step = step;
    m_stepDepth = m_depth;
    m_skipResumedLine = m_hookPaused && m_started;
    m_hookPaused = false;
    m_budgetHit = false;
    m_instructionBlocks = 0;
    m_deadline = NowMs() + 150;

    // LuaJIT shares hooks across all coroutines of a VM. Restore the engine
    // hook before returning to the game, including on yield and error.
    const lua_Hook oldHook = lua_gethook(m_thread);
    const int oldMask = lua_gethookmask(m_thread);
    const int oldCount = lua_gethookcount(m_thread);
    const bool restoreJit = JitEnabled(m_host);

    // Existing machine-code traces may bypass line/count hooks. In -nojit
    // builds there may be no jit table, so only change the engine flag when
    // we can restore it. The workbench owns no persistent hooks.
    if (restoreJit)
        luaJIT_setmode(m_thread, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
    luaJIT_setmode(m_thread, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_FLUSH);
    running = this;
    lua_sethook(m_thread, Hook, LUA_MASKLINE | LUA_MASKCOUNT, HookInterval);
    const int status = lua_resume(m_thread, m_started ? 0 : 3);
    lua_sethook(m_thread, oldHook, oldMask, oldCount);
    running = nullptr;
    if (restoreJit)
        luaJIT_setmode(m_thread, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_ON);
    m_started = true;

    if (m_budgetHit) {
        ReportError("Stopped: Lua instruction or time budget exceeded. Check loops and recursion.");
    } else if (status == LUA_YIELD) {
        m_state = State::Paused;
        if (!m_hookPaused) {
            Snapshot(m_frames, m_thread, 0, m_chunk, false);
            bool nativeFrame = false;
            m_depth = StackDepth(m_thread, nativeFrame);
            lua_settop(m_thread, 0);
            Write("Paused by coroutine.yield()");
        }
        return;
    } else if (status != 0 || !lua_toboolean(m_thread, 1)) {
        ReportError("Runtime: " + ErrorText(m_thread, status ? -1 : 2));
    } else {
        m_state = State::Finished;
        const int results = (std::min)(lua_gettop(m_thread), 17);
        for (int i = 2; i <= results; ++i)
            Write("return[" + std::to_string(i - 1) + "] = " + Value(m_thread, i, true));
        Write("Finished");
    }
    ReleaseThread();
}

void Session::Stop() {
    if (!m_thread || running == this)
        return;
    ReleaseThread();
    m_state = State::Idle;
    m_frames.clear();
    Write("Stopped");
}

void Session::Hook(lua_State* thread, lua_Debug* debug) {
    Session* self = running;
    if (!self)
        return;

    if (debug->event == LUA_HOOKCOUNT) {
        if (++self->m_instructionBlocks < MaxBlocks && NowMs() < self->m_deadline)
            return;
        self->m_budgetHit = true;
        Snapshot(self->m_frames, thread, 0, self->m_chunk, false);
        if (thread != self->m_thread) {
            luaL_error(thread, "Lua workbench budget exceeded");
            return;
        }
        // A hook yield cannot be swallowed by a Lua pcall. Stop drops the
        // coroutine, so it will never resume after exceeding the budget.
        lua_yield(thread, 0);
        return;
    }

    if (thread != self->m_thread || !lua_getinfo(thread, "S", debug) || !debug->source ||
        !std::strcmp(debug->source, WrapperName))
        return;

    if (self->m_skipResumedLine) {
        self->m_skipResumedLine = false;
        if (self->m_pausedLine == debug->currentline && self->m_pausedSource == debug->source)
            return;
    }

    const bool editorSource = self->m_chunk == debug->source;
    const bool breakpoint = editorSource && self->m_breakpoints.count(debug->currentline);
    if (!breakpoint && self->m_step == Step::Continue)
        return;

    bool nativeFrame = false;
    const int depth = StackDepth(thread, nativeFrame);

    // LuaJIT can yield through Lua and fast builtins, but not while a
    // regular C function is invoking Lua through lua_call/lua_pcall.
    if (nativeFrame)
        return;
    if (!breakpoint &&
        ((self->m_step == Step::Over && depth > self->m_stepDepth) ||
         (self->m_step == Step::Out && depth >= self->m_stepDepth)))
        return;

    self->m_depth = depth;
    self->m_hookPaused = true;
    self->m_pausedLine = debug->currentline;
    self->m_pausedSource = debug->source;
    Snapshot(self->m_frames, thread, 0, self->m_chunk, false);
    lua_yield(thread, 0);
}

int Session::ErrorHandler(lua_State* thread) {
    if (running)
        Snapshot(running->m_frames, thread, 1, running->m_chunk, false);
    lua_settop(thread, 1);
    return 1;
}

int Session::Print(lua_State* thread) {
    if (running) {
        std::string text;
        const int count = (std::min)(lua_gettop(thread), 12);
        for (int i = 1; i <= count; ++i) {
            if (i > 1)
                text += "  ";
            text += Value(thread, i, true);
        }
        if (lua_gettop(thread) > count)
            text += "  ...";
        running->Write(std::move(text));
    }
    return 0;
}



} // namespace lua_workbench

namespace lua_workbench {

LiveDebugger& Live() {
    static LiveDebugger live;
    return live;
}

void LiveDebugger::Attach(lua_State* host, const std::string& file, const std::set<int>& breakpoints) {
    Detach();
    m_host = host;
    m_owner = std::this_thread::get_id();
    m_file = file;
    m_breakpoints = breakpoints;
    m_restoreJit = JitEnabled(host);
    if (m_restoreJit)
        luaJIT_setmode(host, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
    luaJIT_setmode(host, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_FLUSH);
    m_lastTrace = 0;
    m_message = "Watching " + file;
    ++m_revision;
    RefreshHook();
}

void LiveDebugger::Detach() {
    if (!m_host)
        return;
    if (lua_gethook(m_host) == Hook)
        lua_sethook(m_host, m_previous, m_previousMask, m_previousCount);
    if (m_restoreJit)
        luaJIT_setmode(m_host, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_ON);
    m_host = m_scheduled = m_target = nullptr;
    m_previous = nullptr;
    m_paused = m_skip = false;
    m_step = Session::Step::Continue;
    m_frames.clear();
    m_message.clear();
    ++m_revision;
}

void LiveDebugger::RefreshHook() {
    if (!m_host || m_owner != std::this_thread::get_id() || lua_gethook(m_host) == Hook)
        return;
    m_previous = lua_gethook(m_host);
    m_previousMask = lua_gethookmask(m_host);
    m_previousCount = lua_gethookcount(m_host);
    lua_sethook(m_host, Hook, m_previousMask | LUA_MASKLINE, m_previousCount);
}

bool LiveDebugger::EnterScheduled(lua_State* thread) {
    if (!m_host || m_owner != std::this_thread::get_id())
        return true;
    RefreshHook();
    if (m_paused && thread == m_target)
        return false;
    m_scheduled = thread;
    return true;
}

void LiveDebugger::LeaveScheduled(lua_State* thread, int result) {
    if (!m_host || m_owner != std::this_thread::get_id())
        return;
    if (m_scheduled == thread)
        m_scheduled = nullptr;
    const bool capturedError = result != 0 && result != LUA_YIELD && CaptureError(thread, 0);
    if (thread == m_target && result != LUA_YIELD) {
        if (capturedError)
            m_step = Session::Step::Continue;
        ForgetThread(thread);
    }
}

void LiveDebugger::ForgetThread(lua_State* thread) {
    if (!m_host || m_owner != std::this_thread::get_id())
        return;
    if (thread == m_scheduled)
        m_scheduled = nullptr;
    if (thread && thread == m_target) {
        const bool wasStepping = m_paused || m_step != Session::Step::Continue;
        m_target = nullptr;
        m_paused = m_skip = false;
        m_step = Session::Step::Continue;
        if (wasStepping) {
            m_message = "Game script thread ended.";
            ++m_revision;
        }
    }
}

void LiveDebugger::Continue(Session::Step step) {
    if (!m_paused)
        return;
    m_paused = false;
    m_step = step;
    m_message = step == Session::Step::Continue ? "Game script resumed." : "Waiting for the next step...";
    ++m_revision;
}

void LiveDebugger::Capture(lua_State* thread, int firstFrame, std::string message) {
    Snapshot(m_frames, thread, firstFrame, m_file, true);
    m_message = std::move(message);
    ++m_revision;
}

bool LiveDebugger::CaptureError(lua_State* thread, int firstFrame) {
    if (!m_host || m_owner != std::this_thread::get_id() || running || m_paused || lua_gettop(thread) < 1)
        return false;
    lua_Debug frame{};
    bool relevant = thread == m_target;
    for (int i = firstFrame; !relevant && i < 64 && lua_getstack(thread, i, &frame); ++i)
        if (lua_getinfo(thread, "S", &frame) && frame.source)
            relevant = SameFile(frame.source, m_file);

    const std::string error = ErrorText(thread, -1);
    const auto file = std::search(error.begin(), error.end(), m_file.begin(), m_file.end(),
        [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
    int line = 0;
    if (file != error.end()) {
        const size_t colon = static_cast<size_t>(file - error.begin()) + m_file.size();
        if (colon + 1 < error.size() && error[colon] == ':') {
            char* end = nullptr;
            const long parsed = std::strtol(error.c_str() + colon + 1, &end, 10);
            if (*end == ':' && parsed > 0 && parsed <= static_cast<long>(Session::MaxSource))
                line = static_cast<int>(parsed);
        }
    }
    if (!relevant && !line)
        return false;
    Capture(thread, firstFrame, "Game Lua error: " + error);
    // LuaJIT can lose the active PC when lua_resume returns with an error.
    // Preserve the location from Lua's own message when that happens.
    if (line) {
        auto match = std::find_if(m_frames.begin(), m_frames.end(), [](const Frame& f) {
            return f.editorSource;
        });
        if (match == m_frames.end())
            m_frames.insert(m_frames.begin(), Frame{m_file, "<error>", line, true, {}});
        else if (match->line <= 0)
            match->line = line;
    }
    return true;
}

void LiveDebugger::Hook(lua_State* thread, lua_Debug* debug) {
    auto& self = Live();
    const int eventMask = debug->event == LUA_HOOKTAILRET ? LUA_MASKRET : (1 << debug->event);
    if (self.m_previous && (self.m_previousMask & eventMask))
        self.m_previous(thread, debug);
    if (debug->event == LUA_HOOKLINE && self.m_owner == std::this_thread::get_id())
        self.OnLine(thread, debug);
}

void LiveDebugger::OnLine(lua_State* thread, lua_Debug* debug) {
    if (m_paused || (m_target && m_step != Session::Step::Continue && thread != m_target))
        return;
    const bool step = thread == m_target && m_step != Session::Step::Continue;
    const bool skip = thread == m_target && m_skip;
    if (!step && !skip && !m_breakpoints.count(debug->currentline))
        return;
    if (!lua_getinfo(thread, "S", debug) || !debug->source ||
        !std::strcmp(debug->source, "@_thread_main"))
        return;

    if (skip) {
        m_skip = false;
        if (m_step == Session::Step::Continue)
            m_target = nullptr;
        if (m_line == debug->currentline && m_lastSource == debug->source)
            return;
    }

    const bool breakpoint = m_breakpoints.count(debug->currentline) && SameFile(debug->source, m_file);
    if (!breakpoint && !step)
        return;
    bool native = false;
    const int depth = StackDepth(thread, native);
    if (thread != m_scheduled || native) {
        const auto now = NowMs();
        if (breakpoint && (!m_lastTrace || now - m_lastTrace >= 150)) {
            m_lastTrace = now;
            Capture(thread, 0, "Tracepoint hit in a synchronous game callback.");
        }
        return;
    }
    if (!breakpoint && ((m_step == Session::Step::Over && depth > m_depth) ||
                        (m_step == Session::Step::Out && depth >= m_depth)))
        return;

    m_target = thread;
    m_paused = m_skip = true;
    m_depth = depth;
    m_line = debug->currentline;
    m_lastSource = debug->source;
    Capture(thread, 0, "Game script paused.");
    lua_yield(thread, 0);
}

} // namespace lua_workbench

namespace lua_workbench {
namespace {

struct Api {
    const char* owner;
    const char* name;
    const char* signature;
    const char* arguments;
    const char* result;
};

constexpr Api ApiEntries[] = {
    {"", "vector", "vector() -> vector", "", "vector"},
    {"", "ini_file", "ini_file(path: string) -> ini_file", "$path$", "ini_file"},
    {"", "device", "device() -> render_device", "", "render_device"},
    {"", "time_global", "time_global() -> number (ms)", "", "number"},
    {"", "print", "print(...: any)", "$value$", ""},
    {"", "log", "log(message: string)", "$message$", ""},
    {"", "pairs", "pairs(tbl: table) -> iterator", "$tbl$", ""},
    {"", "ipairs", "ipairs(tbl: table) -> iterator", "$tbl$", ""},
    {"", "type", "type(value: any) -> string", "$value$", "string"},
    {"", "tonumber", "tonumber(value[, base]) -> number?", "$value$", "number"},
    {"", "tostring", "tostring(value: any) -> string", "$value$", "string"},
    {"string", "format", "string.format(format: string, ...) -> string", "$format$, value", "string"},
    {"string", "sub", "string.sub(text: string, first: number[, last]) -> string", "$text$, 1", "string"},
    {"table", "insert", "table.insert(tbl: table[, index], value)", "$tbl$, value", ""},
    {"math", "floor", "math.floor(value: number) -> number", "$value$", "number"},
    {"db", "actor", "db.actor: game_object?", nullptr, "game_object"},
    {"level", "name", "level.name() -> string", "", "string"},
    {"level", "object_by_id", "level.object_by_id(id: number) -> game_object?", "$id$", "game_object"},
    {"level", "get_weather", "level.get_weather() -> string", "", "string"},
    {"level", "set_weather", "level.set_weather(name: string, forced: boolean)", "$name$, false", ""},
    {"level", "rain_factor", "level.rain_factor() -> number", "", "number"},
    {"game_object", "id", "game_object:id() -> number", "", "number"},
    {"game_object", "name", "game_object:name() -> string", "", "string"},
    {"game_object", "section", "game_object:section() -> string", "", "string"},
    {"game_object", "position", "game_object:position() -> vector", "", "vector"},
    {"game_object", "alive", "game_object:alive() -> boolean", "", "boolean"},
    {"game_object", "object", "game_object:object(id: number | name: string) -> game_object?", "$id$", "game_object"},
    {"game_object", "has_info", "game_object:has_info(info_id: string) -> boolean", "$info_id$", "boolean"},
    {"game_object", "give_info_portion", "game_object:give_info_portion(info_id: string)", "$info_id$", ""},
    {"vector", "x", "vector.x: number", nullptr, "number"},
    {"vector", "y", "vector.y: number", nullptr, "number"},
    {"vector", "z", "vector.z: number", nullptr, "number"},
    {"vector", "set", "vector:set(x: number, y: number, z: number) -> vector", "$0$, 0, 0", "vector"},
    {"vector", "normalize", "vector:normalize() -> vector", "", "vector"},
    {"vector", "magnitude", "vector:magnitude() -> number", "", "number"},
    {"ini_file", "section_exist", "ini_file:section_exist(section: string) -> boolean", "$section$", "boolean"},
    {"ini_file", "line_exist", "ini_file:line_exist(section: string, key: string) -> boolean", "$section$, key", "boolean"},
    {"ini_file", "r_string", "ini_file:r_string(section: string, key: string) -> string", "$section$, key", "string"},
    {"ini_file", "r_float", "ini_file:r_float(section: string, key: string) -> number", "$section$, key", "number"},
    {"ini_file", "r_bool", "ini_file:r_bool(section: string, key: string) -> boolean", "$section$, key", "boolean"},
    {"render_device", "time_global", "render_device:time_global() -> number (ms)", "", "number"},
};

constexpr std::string_view Types[] = {
    "any", "boolean", "function", "game_object", "ini_file", "nil", "number", "render_device",
    "string", "table", "thread", "userdata", "vector"
};

constexpr struct Snippet {
    const char* name;
    const char* pattern;
} Snippets[] = {
    {"if", "if $condition$ then\n    \nend"},
    {"elseif", "elseif $condition$ then\n    "},
    {"for", "for $i$ = 1, limit do\n    \nend"},
    {"forpairs", "for key, value in pairs($items$) do\n    \nend"},
    {"while", "while $condition$ do\n    \nend"},
    {"repeat", "repeat\n    \nuntil $condition$"},
    {"function", "function $name$(...)\n    \nend"},
    {"local", "local $name$ = nil"},
    {"localfunction", "local function $name$(...)\n    \nend"},
    {"actor_guard", "local $actor$ = db and db.actor\nif not actor then return end"},
};

bool Starts(std::string_view value, std::string_view prefix) {
    return value.substr(0, prefix.size()) == prefix;
}

std::string_view Strip(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

std::string_view Word(std::string_view text) {
    size_t end = 0;
    while (end < text.size() && IsIdentifierChar(text[end]))
        ++end;
    return text.substr(0, end);
}

size_t BracketLevel(std::string_view text, size_t pos) {
    if (text[pos] != '[')
        return std::string_view::npos;
    size_t end = pos + 1;
    while (end < text.size() && text[end] == '=')
        ++end;
    return end < text.size() && text[end] == '[' ? end - pos - 1 : std::string_view::npos;
}

// Keep comments and literals out of the declaration index, including Lua's
// long-bracket forms. An unfinished literal also suppresses automatic tips.
std::vector<std::string_view> Tokens(std::string_view text, bool& inCode) {
    std::vector<std::string_view> tokens;
    for (size_t i = 0; i < text.size();) {
        const size_t start = i;
        const char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        const bool comment = text.substr(i, 2) == "--";
        const size_t bracket = i + (comment ? 2 : 0);
        const size_t level = bracket < text.size() ? BracketLevel(text, bracket) : std::string_view::npos;
        if (level != std::string_view::npos) {
            const std::string close = "]" + std::string(level, '=') + "]";
            const auto end = text.find(close, bracket + level + 2);
            if (end == std::string_view::npos) {
                inCode = false;
                break;
            }
            i = end + close.size();
            if (!comment)
                tokens.push_back(text.substr(start, i - start));
        } else if (comment) {
            const auto end = text.find('\n', i + 2);
            if (end == std::string_view::npos) {
                inCode = false;
                break;
            }
            i = end + 1;
        } else if (c == '\'' || c == '"') {
            bool closed = false;
            while (++i < text.size()) {
                if (text[i] == c) {
                    closed = true;
                    ++i;
                    break;
                }
                if (text[i] == '\\' && i + 1 < text.size())
                    ++i;
            }
            if (!closed) {
                inCode = false;
                break;
            }
            tokens.push_back(text.substr(start, i - start));
        } else {
            ++i;
            if (IsIdentifierChar(c))
                while (i < text.size() && IsIdentifierChar(text[i]))
                    ++i;
            tokens.push_back(text.substr(start, i - start));
        }
    }
    return tokens;
}

const Api* ApiFor(std::string_view owner, std::string_view name) {
    for (const auto& entry : ApiEntries)
        if (entry.owner == owner && entry.name == name)
            return &entry;
    return nullptr;
}

struct Symbol { std::string type; std::string args; };
using Symbols = std::map<std::string, Symbol>;

Symbols Declarations(std::string_view source, const std::vector<std::string_view>& tokens) {
    Symbols symbols;
    size_t annotationAt = 0;
    std::string annotatedType;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const auto token = [&](size_t j) { return j < tokens.size() ? tokens[j] : std::string_view{}; };
        const auto at = static_cast<size_t>(tokens[i].data() - source.data());
        // Apply annotations in source order before inferring the following declaration.
        for (size_t nl = source.find('\n', annotationAt); nl < at; nl = source.find('\n', annotationAt)) {
            const auto line = Strip(source.substr(annotationAt, nl - annotationAt));
            annotationAt = nl + 1;
            if (Starts(line, "---@type "))
                annotatedType = Word(Strip(line.substr(9)));
            else if (Starts(line, "---@param ")) {
                const auto rest = Strip(line.substr(10));
                const auto name = Word(rest);
                const auto type = Word(Strip(rest.substr(name.size())));
                if (!name.empty() && !type.empty())
                    symbols[std::string(name)] = {std::string(type), {}};
            }
        }
        if (tokens[i] == "function" && token(i + 2) == "(" &&
            IsIdentifier(std::string(token(i + 1)))) {
            std::string args;
            size_t j = i + 3;
            for (; j < tokens.size() && tokens[j] != ")" && j < i + 35; ++j) {
                if (tokens[j] == ",")
                    args += ", ";
                else
                    args += tokens[j];
            }
            if (token(j) == ")")
                symbols[std::string(token(i + 1))] = {"function", std::move(args)};
        }
        if (tokens[i] != "local" || !IsIdentifier(std::string(token(i + 1))) || token(i + 1) == "function")
            continue;
        std::string type = std::move(annotatedType);
        annotatedType.clear();
        if (type.empty() && token(i + 2) == "=" && !token(i + 3).empty()) {
            size_t valueIndex = i + 3;
            if (token(i + 4) == "and" && token(i + 3) == token(i + 5))
                valueIndex += 2; // db and db.actor
            const auto value = token(valueIndex);
            const auto existing = symbols.find(std::string(value));
            const Api* api = nullptr;
            if (token(valueIndex + 1) == "(")
                api = ApiFor("", value);
            else if (token(valueIndex + 1) == "." || token(valueIndex + 1) == ":")
                api = ApiFor(existing == symbols.end() ? value : std::string_view(existing->second.type),
                             token(valueIndex + 2));
            if (api)
                type = api->result;
            else if (value.front() == '"' || value.front() == '\'' || BracketLevel(value, 0) != std::string_view::npos)
                type = "string";
            else if (value == "true" || value == "false")
                type = "boolean";
            else if (std::isdigit(static_cast<unsigned char>(value.front())))
                type = "number";
            else if (value == "{")
                type = "table";
            else if (value == "nil")
                type = "nil";
            else if (existing != symbols.end())
                type = existing->second.type;
        }
        symbols[std::string(token(i + 1))] = {type.empty() ? "local" : std::move(type), {}};
    }
    return symbols;
}

std::string ReceiverType(std::string_view receiver, const Symbols& symbols) {
    const auto dot = receiver.find('.');
    const auto root = receiver.substr(0, dot);
    const auto it = symbols.find(std::string(root));
    std::string type = it == symbols.end() ? std::string(root) : it->second.type;
    if (dot != std::string_view::npos) {
        const Api* entry = ApiFor(type, receiver.substr(dot + 1));
        type = entry ? entry->result : "";
    }
    return type;
}

using Suggestions = std::map<std::string, Completion>;

void TableSymbols(lua_State* host, std::string_view prefix, Suggestions& matches,
                  bool methods = false) {
    if (!lua_istable(host, -1))
        return;
    const int top = lua_gettop(host);
    lua_pushnil(host);
    for (unsigned i = 0; i < 12000 && lua_next(host, -2); ++i) {
        const int type = lua_type(host, -1);
        if (lua_type(host, -2) == LUA_TSTRING && (!methods || type == LUA_TFUNCTION)) {
            size_t size = 0;
            const char* key = lua_tolstring(host, -2, &size);
            std::string_view name(key, size);
            if (size <= 100 && Starts(name, prefix) && IsIdentifier(std::string(name))) {
                const std::string word(name);
                matches.try_emplace(word, Completion{word,
                    word + (methods ? "(...): engine method" : std::string(": ") + lua_typename(host, type)),
                    type == LUA_TFUNCTION ? word + "($$)" : word});
            }
        }
        lua_pop(host, 1);
    }
    lua_settop(host, top);
}

void RuntimeSymbols(lua_State* host, std::string_view receiver, std::string_view prefix,
                    Suggestions& matches) {
    if (!host)
        return;
    const int top = lua_gettop(host);
    lua_pushvalue(host, LUA_GLOBALSINDEX);
    while (!receiver.empty() && lua_istable(host, -1)) {
        const auto dot = receiver.find('.');
        RawGet(host, -1, std::string(receiver.substr(0, dot)));
        lua_remove(host, -2);
        receiver = dot == std::string_view::npos ? std::string_view{} : receiver.substr(dot + 1);
    }
    if (receiver.empty())
        TableSymbols(host, prefix, matches);
    lua_settop(host, top);
}

#ifdef _MSC_VER
void BoundMembers(lua_State* host, std::string_view owner, std::string_view prefix, bool method,
                  Suggestions& matches) {
    if (!host || owner.empty())
        return;
    auto* registry = luabind::detail::class_registry::get_registry(host);
    if (!registry)
        return;
    registry->iterate_classes(host, [&](lua_State* state, luabind::detail::class_rep* cls) {
        if (!cls->name() || owner != cls->name())
            return;
        if (method) {
            cls->get_table(state);
            TableSymbols(state, prefix, matches, true);
            lua_pop(state, 1);
        } else {
            for (const auto& entry : cls->properties()) {
                const std::string name(entry.first);
                if (Starts(name, prefix) && IsIdentifier(name))
                    matches.try_emplace(name, Completion{name, name + ": engine property", name});
            }
        }
    });
}

void BoundTypes(lua_State* host, std::string_view prefix, std::vector<Completion>& types) {
    if (!host)
        return;
    auto* registry = luabind::detail::class_registry::get_registry(host);
    if (!registry)
        return;
    registry->iterate_classes(host, [&](lua_State*, luabind::detail::class_rep* cls) {
        const char* name = cls->name();
        if (name && types.size() < 48 && Starts(name, prefix) && IsIdentifier(name) &&
            std::none_of(types.begin(), types.end(), [&](const Completion& c) { return c.name == name; }))
            types.push_back({name, std::string(name) + ": engine class", name});
    });
}
#endif

} // namespace

std::vector<Completion> Suggest(lua_State* host, const std::string& source,
                                const std::string& expression, bool statementStart) {
    const auto nl = source.find_last_of('\n');
    const auto line = Strip(std::string_view(source).substr(nl == std::string::npos ? 0 : nl + 1));
    const bool typeAnnotation = line == "---@type" || line == "---@return" ||
        (Starts(line, "---@param ") && IsIdentifier(std::string(Strip(line.substr(10)))));
    if (typeAnnotation) {
        std::vector<Completion> types;
        for (const auto type : Types)
            if (Starts(type, expression))
                types.push_back({std::string(type), std::string(type) + ": annotation type", std::string(type)});
#ifdef _MSC_VER
        BoundTypes(host, expression, types);
#endif
        return types;
    }

    bool inCode = true;
    const auto tokens = Tokens(source, inCode);
    if (!inCode)
        return {};
    const auto symbols = Declarations(source, tokens);
    const size_t dot = expression.find_last_of(".:");
    const std::string_view receiver = dot == std::string::npos ? std::string_view{} :
        std::string_view(expression).substr(0, dot);
    const std::string_view prefix = std::string_view(expression).substr(dot == std::string::npos ? 0 : dot + 1);
    const std::string owner = ReceiverType(receiver, symbols);

    std::map<std::string, Completion> matches;
    for (const auto& entry : ApiEntries) {
        if (owner != entry.owner || !Starts(entry.name, prefix))
            continue;
        const std::string name(entry.name);
        const bool method = std::string_view(entry.owner) == "game_object" ||
            std::string_view(entry.owner) == "vector" || std::string_view(entry.owner) == "ini_file" ||
            std::string_view(entry.owner) == "render_device";
        const bool colon = dot != std::string::npos && expression[dot] == ':';
        if (colon && (!method || !entry.arguments))
            continue;
        if (method && !colon && entry.arguments)
            continue;
        matches.emplace(name, Completion{name, entry.signature,
            entry.arguments ? name + "(" + entry.arguments + ")" : name});
    }
    if (receiver.empty()) {
        for (const auto& item : symbols) {
            const auto& name = item.first;
            const auto& symbol = item.second;
            if (!Starts(name, prefix))
                continue;
            if (symbol.type != "function") {
                matches.insert_or_assign(name, Completion{name, name + ": " + symbol.type, name});
                continue;
            }
            std::string args = symbol.args;
            if (!args.empty()) {
                const auto comma = args.find(',');
                const auto end = comma == std::string::npos ? args.size() : comma;
                if (args.substr(0, end) == "...")
                    args.replace(0, end, "$$");
                else {
                    args.insert(end, "$");
                    args.insert(0, "$");
                }
            }
            matches.insert_or_assign(name, Completion{name, name + "(" + symbol.args + ")", name + "(" + args + ")"});
        }
    }
#ifdef _MSC_VER
    if (dot != std::string::npos)
        BoundMembers(host, owner, prefix, expression[dot] == ':', matches);
#endif
    RuntimeSymbols(host, receiver, prefix, matches);

    std::vector<Completion> result;
    if (statementStart)
        for (const auto& snippet : Snippets)
            if (Starts(snippet.name, prefix)) {
                result.push_back({snippet.name, std::string(snippet.name) + " [snippet]", snippet.pattern});
                matches.erase(snippet.name);
            }
    if (receiver.empty())
        for (const std::string_view word : {"and","break","do","else","elseif","end","false","for",
                "function","if","in","local","nil","not","or","repeat","return","then","true","until","while"})
            if (Starts(word, prefix))
                matches.try_emplace(std::string(word), Completion{std::string(word), std::string(word) + ": keyword", std::string(word)});
    for (auto& item : matches) {
        if (result.size() >= 48)
            break;
        if (std::none_of(result.begin(), result.end(), [&](const Completion& c) { return c.name == item.first; }))
            result.push_back(std::move(item.second));
    }
    return result;
}

} // namespace lua_workbench
