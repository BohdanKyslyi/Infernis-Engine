#include "stdafx.h"

#include "xrdebug.h"
#include "os_clipboard.h"

#include <sal.h>

#pragma warning(push)
#pragma warning(disable : 4995)
#include <malloc.h>
#include <direct.h>
#pragma warning(pop)

extern bool shared_str_initialized;

#define DEBUG_INVOKE __debugbreak()
static BOOL bException = FALSE;

#include <dbghelp.h> // MiniDump flags

#include <new.h>    // for _set_new_mode
#include <signal.h> // for signals

#ifdef DEBUG
#define USE_OWN_ERROR_MESSAGE_WINDOW
#else // DEBUG
#define USE_OWN_MINI_DUMP
#endif // DEBUG

XRCORE_API xrDebug Debug;

static bool error_after_dialog = false;

#include "stacktrace_collector.h"

static thread_local StackTraceInfo stackTrace;

void LogStackTrace(const char* header) {
    if (!shared_str_initialized)
        return;

    BuildStackTrace(stackTrace);

    Msg("%s", header);

    for (size_t i = 1; i < stackTrace.count; ++i)
        LogMsg("{}", stackTrace[i]);
}

void xrDebug::gather_info(const std::string_view expression, const std::string_view description,
                          const std::string_view file, const int line, const std::string_view function,
                          char* assertion_info, const size_t assertion_info_size,
                          std::initializer_list<const std::string_view> args) {
    auto endline = "\n"sv;
    auto prefix = "[error]"sv;

    fmt::ArrayWriter writer{ assertion_info, assertion_info_size };
    const bool extended_description = !description.empty() && (args.size() == 0) &&
                                      (description.find('\n') != std::string_view::npos);
    for (size_t i = 0; i < 2; ++i) {
        if (!i)
            writer.write("{0}FATAL ERROR{0}{0}", endline);
        writer.write("{0}Expression    : {1}{2}", prefix, expression, endline);
        writer.write("{0}Function      : {1}{2}", prefix, function, endline);
        writer.write("{0}File          : {1}{2}", prefix, file, endline);
        writer.write("{0}Line          : {1}{2}", prefix, line, endline);

        if (extended_description) {
            writer.write("{0}{1}{0}", endline, description);
        } else {
            writer.write("{0}Description   : {1}{2}", prefix, description, endline);
            size_t index = 0;
            for (const auto arg : args) {
                writer.write("{0}Argument {1}    : {2}{3}", prefix, index++, arg, endline);
            }
        }

        writer.write("{0}", endline);
        if (!i) {
            if (shared_str_initialized) {
                LogMsg("{}", std::string_view(assertion_info, assertion_info_size));
                FlushLog();
            }
            writer.clear();
            endline = "\r\n"sv;
            prefix = ""sv;
        }
    }

#ifdef USE_MEMORY_MONITOR
    memory_monitor::flush_each_time(true);
    memory_monitor::flush_each_time(false);
#endif // USE_MEMORY_MONITOR

    if (!IsDebuggerPresent() && !strstr(GetCommandLine(), "-no_call_stack_assert")) {
        if (shared_str_initialized)
            Log("stack trace:\n");

#ifdef USE_OWN_ERROR_MESSAGE_WINDOW
        writer.write("stack trace:{0}{0}", endline);
#endif // USE_OWN_ERROR_MESSAGE_WINDOW

        BuildStackTrace(stackTrace);

        for (size_t i = 2; i < stackTrace.count; ++i) {
            if (shared_str_initialized)
                LogMsg("{}", stackTrace[i]);

#ifdef USE_OWN_ERROR_MESSAGE_WINDOW
            writer.write("{0}{1}", stackTrace[i], endline);
#endif // USE_OWN_ERROR_MESSAGE_WINDOW
        }

        if (shared_str_initialized)
            FlushLog();

        // TODO: [imdex] use string_view
        os_clipboard::copy_to_clipboard(writer.c_str());
    }

    if (writer.size() < assertion_info_size) {
        assertion_info[writer.size()] = '\0';
    } else {
        assertion_info[assertion_info_size - 1] = '\0';
    }
}

[[noreturn]] void xrDebug::do_exit(const std::string& message) {
    FlushLog();
    MessageBox(nullptr, message.c_str(), "Error", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
    TerminateProcess(GetCurrentProcess(), 1);
}

void xrDebug::backend(const std::string_view expression, const std::string_view description,
                      std::initializer_list<const std::string_view> args,
                      const std::string_view file, const int line, const std::string_view function,
                      bool& ignore_always) {
    static std::mutex CS;
    std::lock_guard<decltype(CS)> lock(CS);

    error_after_dialog = true;

    string4096 assertion_info;

    gather_info(expression, description, file, line, function, assertion_info,
                sizeof(assertion_info), args);

#ifdef USE_OWN_ERROR_MESSAGE_WINDOW
    constexpr auto endline = "\r\n"sv;

    const auto used = std::strlen(assertion_info);
    char* buffer = assertion_info + used;
    fmt::ArrayWriter writer{ buffer, sizeof(assertion_info) - used };

    writer.write("{0}Press CANCEL to abort execution{0}", endline);
    writer.write("Press TRY AGAIN to continue execution{0}", endline);
    writer.write("Press CONTINUE to continue execution and ignore all the errors of this type{0}{0}", endline);
#endif // USE_OWN_ERROR_MESSAGE_WINDOW

    if (handler)
        handler();

    if (get_on_dialog())
        get_on_dialog()(true);

    FlushLog();

#ifdef USE_OWN_ERROR_MESSAGE_WINDOW
    const auto result = MessageBox(
        /*GetTopWindow(NULL)*/ nullptr, writer.c_str(), "Fatal Error",
        MB_CANCELTRYCONTINUE | MB_ICONERROR | MB_SYSTEMMODAL);

    switch (result) {
    case IDCANCEL: {
        DEBUG_INVOKE;
        break;
    }
    case IDTRYAGAIN: {
        error_after_dialog = false;
        break;
    }
    case IDCONTINUE: {
        error_after_dialog = false;
        ignore_always = true;
        break;
    }
    default:
        NODEFAULT;
    }
#else  // USE_OWN_ERROR_MESSAGE_WINDOW
    DEBUG_INVOKE;
#endif // USE_OWN_ERROR_MESSAGE_WINDOW

    if (get_on_dialog())
        get_on_dialog()(false);
}

const char* xrDebug::error2string(const DWORD code) const {
    const char* result = nullptr;
    static string1024 desc_storage;

    if (nullptr == result) {
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, code, 0, desc_storage,
                      sizeof(desc_storage) - 1, nullptr);
        result = desc_storage;
    }
    return result;
}

void xrDebug::error(const DWORD code, const std::string_view expr, std::initializer_list<const std::string_view> args,
                    const std::string_view file, const int line, const std::string_view function, bool& ignore_always) {
    backend(error2string(code), expr, args, file, line, function, ignore_always);
}

void xrDebug::fail(const std::string_view expr, const std::string_view file, const int line,
                   const std::string_view function, bool& ignore_always) {
    backend("assertion failed"sv, expr, {}, file, line, function, ignore_always);
}

void xrDebug::fail(const std::string_view reason, const std::string_view expr, const std::string_view file,
                   const int line, const std::string_view function, bool& ignore_always) {
    backend(reason, expr, {}, file, line, function, ignore_always);
}

void xrDebug::fail(const std::string_view reason, const std::string_view expr,
                   std::initializer_list<const std::string_view> args, const std::string_view file,
                   const int line, const std::string_view function, bool& ignore_always) {
    backend(reason, expr, args, file, line, function, ignore_always);
}

typedef void (*full_memory_stats_callback_type)();
XRCORE_API full_memory_stats_callback_type g_full_memory_stats_callback = 0;

int out_of_memory_handler(size_t size) {
    if (g_full_memory_stats_callback)
        g_full_memory_stats_callback();
    else {
        Memory.mem_compact();
        u32 process_heap = mem_usage_impl(nullptr, nullptr);
        int eco_strings = (int)g_pStringContainer->stat_economy();
        int eco_smem = (int)g_pSharedMemoryContainer->stat_economy();
        LogMsg("* [x-ray]: process heap[{} K]", process_heap / 1024);
        LogMsg("* [x-ray]: economy: strings[{0} K], smem[{1} K]", eco_strings / 1024, eco_smem);
    }

    Debug.fatal(DEBUG_INFO, "Out of memory. Memory request: %d K", size / 1024);
    return 1;
}

extern LPCSTR log_name();

XRCORE_API string_path g_bug_report_file;

extern void BuildStackTrace(struct _EXCEPTION_POINTERS* pExceptionInfo);
typedef LONG WINAPI UnhandledExceptionFilterType(struct _EXCEPTION_POINTERS* pExceptionInfo);
typedef LONG(__stdcall* PFNCHFILTFN)(EXCEPTION_POINTERS* pExPtrs);
extern "C" BOOL __stdcall SetCrashHandlerFilter(PFNCHFILTFN pFn);

static UnhandledExceptionFilterType* previous_filter = nullptr;

#ifdef USE_OWN_MINI_DUMP
typedef BOOL(WINAPI* MINIDUMPWRITEDUMP)(HANDLE hProcess, DWORD dwPid, HANDLE hFile,
                                        MINIDUMP_TYPE DumpType,
                                        CONST PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
                                        CONST PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
                                        CONST PMINIDUMP_CALLBACK_INFORMATION CallbackParam);

void save_mini_dump(_EXCEPTION_POINTERS* pExceptionInfo) {
    // firstly see if dbghelp.dll is around and has the function we need
    // look next to the EXE first, as the one in System32 might be old
    // (e.g. Windows 2000)
    HMODULE hDll = nullptr;
    string_path szDbgHelpPath;

    if (GetModuleFileName(nullptr, szDbgHelpPath, _MAX_PATH)) {
        char* pSlash = strchr(szDbgHelpPath, '\\');
        if (pSlash) {
            xr_strcpy(pSlash + 1, sizeof(szDbgHelpPath) - (pSlash - szDbgHelpPath), "DBGHELP.DLL");
            hDll = ::LoadLibrary(szDbgHelpPath);
        }
    }

    if (!hDll) {
        // load any version we can
        hDll = ::LoadLibrary("DBGHELP.DLL");
    }

    LPCTSTR szResult = NULL;

    if (hDll) {
        MINIDUMPWRITEDUMP pDump = (MINIDUMPWRITEDUMP)::GetProcAddress(hDll, "MiniDumpWriteDump");
        if (pDump) {
            string_path szDumpPath;
            string_path szScratch;
            string64 t_stemp;

            timestamp(t_stemp);
            xr_strcpy(szDumpPath, Core.ApplicationName);
            xr_strcat(szDumpPath, "_");
            xr_strcat(szDumpPath, Core.UserName);
            xr_strcat(szDumpPath, "_");
            xr_strcat(szDumpPath, t_stemp);
            xr_strcat(szDumpPath, ".mdmp");

            __try {
                if (FS.path_exist("$logs$"))
                    FS.update_path(szDumpPath, "$logs$", szDumpPath);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                string_path temp;
                xr_strcpy(temp, szDumpPath);
                xr_strcpy(szDumpPath, "logs/");
                xr_strcat(szDumpPath, temp);
            }

            // create the file
            HANDLE hFile = ::CreateFile(szDumpPath, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
                                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (INVALID_HANDLE_VALUE == hFile) {
                // try to place into current directory
                MoveMemory(szDumpPath, szDumpPath + 5, strlen(szDumpPath));
                hFile = ::CreateFile(szDumpPath, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
                                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            }
            if (hFile != INVALID_HANDLE_VALUE) {
                _MINIDUMP_EXCEPTION_INFORMATION ExInfo;

                ExInfo.ThreadId = ::GetCurrentThreadId();
                ExInfo.ExceptionPointers = pExceptionInfo;
                ExInfo.ClientPointers = NULL;

                // write the dump
                MINIDUMP_TYPE dump_flags =
                    MINIDUMP_TYPE(MiniDumpNormal | MiniDumpFilterMemory | MiniDumpScanMemory);

                BOOL bOK = pDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, dump_flags,
                                 &ExInfo, nullptr, nullptr);
                if (bOK) {
                    xr_sprintf(szScratch, "Saved dump file to '%s'", szDumpPath);
                    szResult = szScratch;
                    //					retval = EXCEPTION_EXECUTE_HANDLER;
                } else {
                    xr_sprintf(szScratch, "Failed to save dump file to '%s' (error %d)", szDumpPath,
                               GetLastError());
                    szResult = szScratch;
                }
                ::CloseHandle(hFile);
            } else {
                xr_sprintf(szScratch, "Failed to create dump file '%s' (error %d)", szDumpPath,
                           GetLastError());
                szResult = szScratch;
            }
        } else {
            szResult = "DBGHELP.DLL too old";
        }
    } else {
        szResult = "DBGHELP.DLL not found";
    }
}
#endif // USE_OWN_MINI_DUMP

void format_message(LPSTR buffer, const u32& buffer_size) {
    LPVOID message;
    DWORD error_code = GetLastError();

    if (!error_code) {
        *buffer = 0;
        return;
    }

    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, error_code,
                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&message, 0, NULL);

    xr_sprintf(buffer, buffer_size, "[error][%8d]    : %s", error_code, message);
    LocalFree(message);
}

#include <errorrep.h>

LONG WINAPI UnhandledFilter(_EXCEPTION_POINTERS* pExceptionInfo) {
    string256 error_message;
    format_message(error_message, sizeof(error_message));

    if (!error_after_dialog && !strstr(GetCommandLine(), "-no_call_stack_assert")) {
        CONTEXT save = *pExceptionInfo->ContextRecord;
        BuildStackTrace(pExceptionInfo, stackTrace);
        *pExceptionInfo->ContextRecord = save;

        if (shared_str_initialized)
            Log("stack trace:\n");

        if (!IsDebuggerPresent()) {
            os_clipboard::copy_to_clipboard("stack trace:\r\n\r\n");
        }

        string4096 buffer;
        for (int i = 0; i < stackTrace.count; ++i) {
            if (shared_str_initialized)
                LogMsg("{}", stackTrace[i]);
            xr_sprintf(buffer, sizeof(buffer), "%s\r\n", stackTrace[i]);
#ifdef DEBUG
            if (!IsDebuggerPresent())
                os_clipboard::update_clipboard(buffer);
#endif // #ifdef DEBUG
        }

        if (*error_message) {
            if (shared_str_initialized)
                LogMsg("\n{}", error_message);

            xr_strcat(error_message, sizeof(error_message), "\r\n");
#ifdef DEBUG
            if (!IsDebuggerPresent())
                os_clipboard::update_clipboard(buffer);
#endif // #ifdef DEBUG
        }
    }

    if (shared_str_initialized)
        FlushLog();

#ifndef USE_OWN_ERROR_MESSAGE_WINDOW
#ifdef USE_OWN_MINI_DUMP
    save_mini_dump(pExceptionInfo);
#endif // USE_OWN_MINI_DUMP
#else  // USE_OWN_ERROR_MESSAGE_WINDOW
    if (!error_after_dialog) {
        if (Debug.get_on_dialog())
            Debug.get_on_dialog()(true);

        MessageBox(NULL, "Fatal error occured\n\nPress OK to abort program execution",
                   "Fatal error", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
    }
#endif // USE_OWN_ERROR_MESSAGE_WINDOW

    ReportFault(pExceptionInfo, 0);

#ifdef USE_OWN_ERROR_MESSAGE_WINDOW
    if (Debug.get_on_dialog())
        Debug.get_on_dialog()(false);
#endif // USE_OWN_ERROR_MESSAGE_WINDOW

    return EXCEPTION_EXECUTE_HANDLER;
}

//////////////////////////////////////////////////////////////////////
typedef int(__cdecl* _PNH)(size_t);

void _terminate() {
    if (strstr(GetCommandLine(), "-silent_error_mode"))
        exit(-1);

    string4096 assertion_info;

    Debug.gather_info("<no expression>", "Unexpected application termination", DEBUG_INFO,
                      assertion_info, {});

    LPCSTR endline = "\r\n";
    LPSTR buffer = assertion_info + xr_strlen(assertion_info);
    buffer += xr_sprintf(buffer, xr_strlen(assertion_info), "Press OK to abort execution%s", endline);

    MessageBox(GetTopWindow(nullptr), assertion_info, "Fatal Error",
               MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);

    exit(-1);
}

static void handler_base(const char* reason_string) {
    bool ignore_always = false;
    Debug.backend("error handler is invoked!", reason_string, {}, DEBUG_INFO, ignore_always);
}

static void invalid_parameter_handler(const wchar_t* expression, const wchar_t* function,
                                      const wchar_t* file, unsigned int line, uintptr_t reserved) {
    bool ignore_always = false;

    string4096 expression_;
    string4096 function_;
    string4096 file_;
    size_t converted_chars = 0;

    if (expression)
        wcstombs_s(&converted_chars, expression_, sizeof(expression_), expression,
                   (wcslen(expression) + 1) * 2 * sizeof(char));
    else
        xr_strcpy(expression_, "");

    if (function)
        wcstombs_s(&converted_chars, function_, sizeof(function_), function,
                   (wcslen(function) + 1) * 2 * sizeof(char));
    else
        xr_strcpy(function_, __FUNCTION__);

    if (file)
        wcstombs_s(&converted_chars, file_, sizeof(file_), file,
                   (wcslen(file) + 1) * 2 * sizeof(char));
    else {
        line = __LINE__;
        xr_strcpy(file_, __FILE__);
    }

    Debug.backend("error handler is invoked!", expression_, {}, file_, line, function_,
                  ignore_always);
}

static void pure_call_handler() { handler_base("pure virtual function call"); }

#ifdef XRAY_USE_EXCEPTIONS
static void unexpected_handler() { handler_base("unexpected program termination"); }
#endif // XRAY_USE_EXCEPTIONS

static void abort_handler(int signal) { handler_base("application is aborting"); }

static void floating_point_handler(int signal) { handler_base("floating point error"); }

static void illegal_instruction_handler(int signal) { handler_base("illegal instruction"); }

//	static void storage_access_handler		(int signal)
//	{
//		handler_base					("illegal storage access");
//	}

static void termination_handler(int signal) { handler_base("termination with exit code 3"); }

void debug_on_thread_spawn() {
    //std::set_terminate				(_terminate);

    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    signal(SIGABRT, abort_handler);
    signal(SIGABRT_COMPAT, abort_handler);
    signal(SIGFPE, floating_point_handler);
    signal(SIGILL, illegal_instruction_handler);
    signal(SIGINT, 0);
    //		signal							(SIGSEGV,
    //storage_access_handler);
    signal(SIGTERM, termination_handler);

    _set_invalid_parameter_handler(&invalid_parameter_handler);

    _set_new_mode(1);
    _set_new_handler(&out_of_memory_handler);
    //		std::set_new_handler			(&std_out_of_memory_handler);

    _set_purecall_handler(&pure_call_handler);
}

// Logging part C ( 2026-11-10, my design, not the donor's - Monolith has no such handler ): a C++
// exception thrown by the game's own code goes to the log at the moment it is thrown, before LuaJIT
// turns it into the bare "C++ exception" or a crash loses its type. One record: type, text, C++ stack
// from the throw on, Lua stack ( through the hook script_engine.cpp sets ). The handler only reads:
// it always lets the exception go on, nothing is caught or hidden.
// - only code 0xE06D7363 ( MSVC C++ throw ) and only when a game module threw it: xrEngine.exe,
//   xr*.dll, luabind.dll ( drivers, OpenAL and Windows throw and catch such exceptions themselves );
// - at most 16 places per run ( place = type + address of the throw ), each place written once.
static void (*volatile cpp_exception_lua_hook)() = nullptr;
// script_engine.cpp: prints the Lua stack of the running script; nullptr = none
XRCORE_API void SetCppExceptionLuaHook(void (*hook)()) { cpp_exception_lua_hook = hook; }
#if defined(_M_X64)
namespace {
constexpr DWORD cpp_exception_code = 0xE06D7363;
constexpr int cpp_exception_places = 16;
volatile LONG64 cpp_exception_place_keys[cpp_exception_places] = {};
volatile LONG cpp_exception_limit_written = 0;
thread_local bool cpp_exception_busy = false;

// MSVC x64 throw description ( ehdata.h ): the links are offsets from the image base of the thrower
struct cpp_throw_info { unsigned attributes; int unwind; int forward_compat; int catchable_types; };
struct cpp_catchable_types { int count; int types[1]; };
struct cpp_catchable_type { unsigned properties; int type; int mdisp; int pdisp; int vdisp; int size; int copy; };
struct cpp_type_descriptor { const void* vftable; void* spare; char name[1]; };
enum class cpp_place_state { fresh, seen, full };

bool cpp_name_starts(const wchar_t* name, const wchar_t* prefix) {
    return !_wcsnicmp(name, prefix, wcslen(prefix));
}

// 1 = game ( xrEngine.exe, xr*.dll, luabind.dll ), 0 = Windows / C++ runtime ( skipped ), -1 = other / none
int cpp_frame_owner(const void* address) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(address), &module))
        return -1;
    wchar_t path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (!length || length >= MAX_PATH)
        return -1;
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    if (!_wcsicmp(name, L"ntdll.dll") || !_wcsicmp(name, L"kernelbase.dll") || !_wcsicmp(name, L"kernel32.dll") ||
        cpp_name_starts(name, L"vcruntime") || cpp_name_starts(name, L"msvcp") || cpp_name_starts(name, L"ucrtbase"))
        return 0;
    if (cpp_name_starts(name, L"xr") || !_wcsicmp(name, L"luabind.dll"))
        return 1;
    return -1;
}

// the frame that threw: unwinds from the exception's own context ( RaiseException first ) past Windows / runtime
void* cpp_exception_thrower(const CONTEXT* thrown) {
    CONTEXT context = *thrown;
    for (int frame = 0; frame < 32 && context.Rip; ++frame) {
        void* const address = reinterpret_cast<void*>(context.Rip);
        const int owner = cpp_frame_owner(address);
        if (owner)
            return owner > 0 ? address : nullptr;
        DWORD64 image_base = 0;
        const PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
        if (function) {
            void* handler_data = nullptr;
            DWORD64 establisher_frame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, function, &context, &handler_data,
                             &establisher_frame, nullptr);
        } else { // leaf function: the return address is on the top of the stack
            context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
            context.Rsp += 8;
        }
    }
    return nullptr;
}

cpp_place_state cpp_exception_take_place(LONG64 key) {
    for (int i = 0; i < cpp_exception_places; ++i) {
        const LONG64 previous = InterlockedCompareExchange64(&cpp_exception_place_keys[i], key, 0);
        if (!previous)
            return cpp_place_state::fresh;
        if (previous == key)
            return cpp_place_state::seen;
    }
    return cpp_place_state::full;
}

// ".?AVout_of_range@std@@" -> "std::out_of_range"; other forms ( templates, pointers ) stay as they are
void cpp_type_name(const char* raw, char* out, size_t out_size) {
    std::snprintf(out, out_size, "%s", raw);
    if (std::strncmp(raw, ".?AV", 4) && std::strncmp(raw, ".?AU", 4))
        return;
    const char* const begin = raw + 4;
    const char* const end = std::strstr(begin, "@@");
    if (!end || end == begin)
        return;
    for (const char* c = begin; c < end; ++c)
        if (*c == '?' || *c == '$')
            return;
    char result[256];
    size_t length = 0;
    const char* part_end = end;
    while (part_end > begin) {
        const char* part_begin = part_end;
        while (part_begin > begin && part_begin[-1] != '@')
            --part_begin;
        const size_t part_length = static_cast<size_t>(part_end - part_begin);
        if (!part_length || length + part_length + 3 > sizeof(result))
            return;
        if (length) {
            result[length++] = ':';
            result[length++] = ':';
        }
        std::memcpy(result + length, part_begin, part_length);
        length += part_length;
        part_end = part_begin > begin ? part_begin - 1 : begin;
    }
    result[length] = 0;
    std::snprintf(out, out_size, "%s", result);
}

// what() of the std::exception part ( virtual slot 1 of the MSVC std::exception ); false = none
bool cpp_exception_text(const void* object, char* out, size_t out_size) {
    out[0] = 0;
    __try {
        using what_function = const char* (*)(const void*);
        const what_function* const table = *static_cast<const what_function* const*>(object);
        const char* const text = table[1](object);
        if (!text)
            return false;
        size_t i = 0;
        for (; i + 1 < out_size && text[i]; ++i)
            out[i] = text[i];
        out[i] = 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = 0;
        return false;
    }
}

void cpp_exception_write(const EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* const record = info->ExceptionRecord;
    void* const thrower = cpp_exception_thrower(info->ContextRecord);
    if (!thrower)
        return;
    const char* const base = reinterpret_cast<const char*>(record->ExceptionInformation[3]);
    const auto* const throw_info = reinterpret_cast<const cpp_throw_info*>(record->ExceptionInformation[2]);
    const char* const object = reinterpret_cast<const char*>(record->ExceptionInformation[1]);
    const char* raw_name = "?";
    const void* std_exception = nullptr;
    if (throw_info->catchable_types) {
        const auto* const types = reinterpret_cast<const cpp_catchable_types*>(base + throw_info->catchable_types);
        for (int i = 0; i < types->count && i < 64; ++i) {
            const auto* const type = reinterpret_cast<const cpp_catchable_type*>(base + types->types[i]);
            if (!type->type)
                continue;
            const auto* const descriptor = reinterpret_cast<const cpp_type_descriptor*>(base + type->type);
            if (!i)
                raw_name = descriptor->name;
            if (object && !std_exception && type->pdisp < 0 && !std::strcmp(descriptor->name, ".?AVexception@std@@"))
                std_exception = object + type->mdisp;
        }
    }
    unsigned long long hash = 0xcbf29ce484222325ULL; // FNV-1a of the type, then the address of the throw
    for (const char* c = raw_name; *c; ++c) {
        hash ^= static_cast<unsigned char>(*c);
        hash *= 0x100000001b3ULL;
    }
    hash ^= reinterpret_cast<unsigned long long>(thrower);
    const cpp_place_state state = cpp_exception_take_place(static_cast<LONG64>(hash | 1));
    if (state == cpp_place_state::seen)
        return;
    if (state == cpp_place_state::full) {
        if (!InterlockedExchange(&cpp_exception_limit_written, 1)) {
            Msg("! C++ exceptions: %d places written, further ones are not logged", cpp_exception_places);
            FlushLog();
        }
        return;
    }
    char type_name[256];
    cpp_type_name(raw_name, type_name, sizeof(type_name));
    char text[512];
    if (std_exception && cpp_exception_text(std_exception, text, sizeof(text)) && text[0])
        Msg("! C++ exception: %s \"%s\"", type_name, text);
    else
        Msg("! C++ exception: %s", type_name);
    BuildStackTrace(stackTrace);
    size_t first = 0; // the stack from the throw on: the frames up to _CxxThrowException are the handler's own
    for (size_t i = 0; i < stackTrace.count && i < 32; ++i)
        if (std::strstr(stackTrace[i], "CxxThrowException"))
            first = i + 1;
    Msg("! C++ stack:");
    for (size_t i = first; i < stackTrace.count; ++i)
        LogMsg("{}", stackTrace[i]);
    void (*const hook)() = cpp_exception_lua_hook;
    if (hook)
        hook();
    FlushLog();
}

LONG NTAPI cpp_exception_logger(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* const record = info ? info->ExceptionRecord : nullptr;
    if (!record || record->ExceptionCode != cpp_exception_code || record->NumberParameters < 4)
        return EXCEPTION_CONTINUE_SEARCH;
    const ULONG_PTR magic = record->ExceptionInformation[0];
    if (magic < 0x19930520 || magic > 0x19930522 || !record->ExceptionInformation[2] ||
        !record->ExceptionInformation[3])
        return EXCEPTION_CONTINUE_SEARCH;
    if (cpp_exception_busy || cpp_exception_limit_written || !shared_str_initialized)
        return EXCEPTION_CONTINUE_SEARCH;
    cpp_exception_busy = true;
    __try {
        cpp_exception_write(info);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    cpp_exception_busy = false;
    return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace
#endif // _M_X64

void xrDebug::_initialize() {
    *g_bug_report_file = 0;

    debug_on_thread_spawn();

    previous_filter = ::SetUnhandledExceptionFilter(
        UnhandledFilter); // exception handler to all "unhandled" exceptions
#if defined(_M_X64)
    // logging part C ( see cpp_exception_logger ): first in the chain, only reads
    AddVectoredExceptionHandler(1, cpp_exception_logger);
#endif // _M_X64
}
