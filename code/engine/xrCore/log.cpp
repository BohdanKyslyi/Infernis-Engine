#include "stdafx.h"
#pragma hdrstop

#include <time.h>
#include "resource.h"
#include "log.h"
#include "malloc.h"

extern BOOL LogExecCB = TRUE;
static string_path logFName = "engine.log";
static string_path log_file_name = "engine.log";
static BOOL no_log = TRUE;
static std::recursive_mutex logCS;
static IWriter* logWriter = nullptr;
static size_t flushedLineCount = 0;

xr_vector<std::string>* LogFile = nullptr;
static LogCallback LogCB = nullptr;

static bool OpenLogWriter() {
    const BOOL previousNoLog = no_log;
    no_log = TRUE;
    logWriter = FS.w_open(logFName);
    no_log = previousNoLog;

    if (logWriter && logWriter->valid())
        return true;

    FS.w_close(logWriter);
    return false;
}

static void FlushLogLocked() {
    if (no_log || !LogFile)
        return;

    // LogFile may be cleared by the console. Recreate the file in that case.
    if (LogFile->size() < flushedLineCount) {
        FS.w_close(logWriter);
        flushedLineCount = 0;
    }

    if (!logWriter && !OpenLogWriter())
        return;

    while (flushedLineCount < LogFile->size()) {
        const char* line = (*LogFile)[flushedLineCount].c_str();
        logWriter->w_string(line ? line : "");
        ++flushedLineCount;
    }

    logWriter->flush();
}

void FlushLog() {
    std::lock_guard<decltype(logCS)> lock(logCS);
    FlushLogLocked();
}

void AddOne(const char* split) {
    std::lock_guard<decltype(logCS)> lock(logCS);
    if (!LogFile)
        return;

#ifdef DEBUG
    OutputDebugString(split);
    OutputDebugString("\n");
#endif

    LogFile->push_back(split);

    // exec CallBack
    if (LogExecCB && LogCB)
        LogCB(split);

    FlushLogLocked();
}

void Log(const char* s) {
    int i, j;

    u32 length = xr_strlen(s);
#ifndef _EDITOR
    PSTR split = (PSTR)_alloca((length + 1) * sizeof(char));
#else
    PSTR split = (PSTR)alloca((length + 1) * sizeof(char));
#endif
    for (i = 0, j = 0; s[i] != 0; i++) {
        if (s[i] == '\n') {
            split[j] = 0; // end of line
            if (split[0] == 0) {
                split[0] = ' ';
                split[1] = 0;
            }
            AddOne(split);
            j = 0;
        } else {
            split[j++] = s[i];
        }
    }
    split[j] = 0;
    AddOne(split);
}

void Log(const char* msg, const char* dop) {
    if (!dop) {
        Log(msg);
        return;
    }

    u32 buffer_size = (xr_strlen(msg) + 1 + xr_strlen(dop) + 1) * sizeof(char);
    PSTR buf = (PSTR)_alloca(buffer_size);
    strconcat(buffer_size, buf, msg, " ", dop);
    Log(buf);
}

void Log(const char* msg, u32 dop) {
    u32 buffer_size = (xr_strlen(msg) + 1 + 10 + 1) * sizeof(char);
    PSTR buf = (PSTR)_alloca(buffer_size);

    xr_sprintf(buf, buffer_size, "%s %d", msg, dop);
    Log(buf);
}

void Log(const char* msg, const u64 dop) {
    const auto buffer_size = (std::strlen(msg) + 1 + 10 + 1) * sizeof(char);
    char* buf = static_cast<char*>(_alloca(buffer_size));

    xr_sprintf(buf, buffer_size, "%s %" PRIu64, msg, dop);
    Log(buf);
}

void Log(const char* msg, int dop) {
    u32 buffer_size = (xr_strlen(msg) + 1 + 11 + 1) * sizeof(char);
    PSTR buf = (PSTR)_alloca(buffer_size);

    xr_sprintf(buf, buffer_size, "%s %i", msg, dop);
    Log(buf);
}

void Log(const char* msg, float dop) {
    // actually, float string representation should be no more, than 40 characters,
    // but we will count with slight overhead
    u32 buffer_size = (xr_strlen(msg) + 1 + 64 + 1) * sizeof(char);
    PSTR buf = (PSTR)_alloca(buffer_size);

    xr_sprintf(buf, buffer_size, "%s %f", msg, dop);
    Log(buf);
}

void Log(const char* msg, const Fvector& dop) {
    u32 buffer_size = (xr_strlen(msg) + 2 + 3 * (64 + 1) + 1) * sizeof(char);
    PSTR buf = (PSTR)_alloca(buffer_size);

    xr_sprintf(buf, buffer_size, "%s (%f,%f,%f)", msg, VPUSH(dop));
    Log(buf);
}

void Log(const char* msg, const Fmatrix& dop) {
    u32 buffer_size = (xr_strlen(msg) + 2 + 4 * (4 * (64 + 1) + 1) + 1) * sizeof(char);
    PSTR buf = (PSTR)_alloca(buffer_size);

    xr_sprintf(buf, buffer_size, "%s:\n%f,%f,%f,%f\n%f,%f,%f,%f\n%f,%f,%f,%f\n%f,%f,%f,%f\n", msg,
               dop.i.x, dop.i.y, dop.i.z, dop._14_, dop.j.x, dop.j.y, dop.j.z, dop._24_, dop.k.x,
               dop.k.y, dop.k.z, dop._34_, dop.c.x, dop.c.y, dop.c.z, dop._44_);
    Log(buf);
}

void LogWinErr(const char* msg, long err_code) { Msg("%s: %s", msg, Debug.error2string(err_code)); }

LogCallback SetLogCB(LogCallback cb) {
    std::lock_guard<decltype(logCS)> lock(logCS);
    LogCallback result = LogCB;
    LogCB = cb;
    return (result);
}

LPCSTR log_name() { return (log_file_name); }

void InitLog() {
    std::lock_guard<decltype(logCS)> lock(logCS);
    R_ASSERT(LogFile == NULL);
    LogFile = xr_new<xr_vector<std::string>>();
    LogFile->reserve(1000);
}

void CreateLog(BOOL nl) {
    std::lock_guard<decltype(logCS)> lock(logCS);

    no_log = TRUE;
    FS.w_close(logWriter);
    flushedLineCount = 0;

    strconcat(sizeof(log_file_name), log_file_name, Core.ApplicationName, "_", Core.UserName,
              ".log");
    if (FS.path_exist("$logs$"))
        FS.update_path(logFName, "$logs$", log_file_name);

    if (!nl) {
        if (!OpenLogWriter()) {
            MessageBox(NULL, "Can't create log file.", "Error", MB_ICONERROR);
            abort();
        }

        no_log = FALSE;
        FlushLogLocked();
    }
}

void CloseLog(void) {
    std::lock_guard<decltype(logCS)> lock(logCS);
    FlushLogLocked();
    FS.w_close(logWriter);
    flushedLineCount = 0;
    no_log = TRUE;

    if (LogFile)
        LogFile->clear();
    xr_delete(LogFile);
}
