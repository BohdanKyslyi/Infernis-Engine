#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <thread>
#include <vector>

struct lua_State;
struct lua_Debug;

namespace lua_workbench {

struct Frame {
    std::string source;
    std::string function;
    int line = 0;
    bool editorSource = false;
    std::vector<std::string> variables;
};

struct Completion {
    std::string name;
    std::string signature;
    // A pair of '$' characters delimits the first editable placeholder.
    std::string insertion;
};

std::vector<Completion> Suggest(lua_State* host, const std::string& sourceBeforeCursor,
                                const std::string& expression, bool statementStart);

class Session {
public:
    enum class Step { Continue, Into, Over, Out };
    enum class State { Idle, Paused, Finished, Failed };
    static constexpr std::size_t MaxSource = 256 * 1024;

    Session() = default;
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    bool Check(lua_State* host, const std::string& source, const std::string& name);
    void Start(lua_State* host, const std::string& source, const std::string& name,
               const std::set<int>& breakpoints, Step step);
    void Resume(Step step);
    void Stop();

    State GetState() const { return m_state; }
    int ErrorLine() const { return m_errorLine; }
    const std::vector<Frame>& Frames() const { return m_frames; }
    const std::vector<std::string>& Output() const { return m_output; }

private:
    static void Hook(lua_State* thread, lua_Debug* debug);
    static int ErrorHandler(lua_State* thread);
    static int Print(lua_State* thread);
    bool Compile(lua_State* target, const std::string& source, const std::string& name);
    void ReportError(const std::string& text);
    void Write(std::string text);
    void ResetOutput();
    void ReleaseThread();

    lua_State* m_host = nullptr;
    lua_State* m_thread = nullptr;
    int m_reference = -2;
    int m_errorLine = 0;
    int m_depth = 0;
    int m_pausedLine = 0;
    bool m_skipResumedLine = false;
    std::string m_pausedSource;
    int m_stepDepth = 0;
    unsigned m_instructionBlocks = 0;
    unsigned long long m_deadline = 0;
    bool m_hookPaused = false;
    bool m_budgetHit = false;
    bool m_started = false;
    Step m_step = Step::Continue;
    State m_state = State::Idle;
    std::string m_chunk;
    std::set<int> m_breakpoints;
    std::vector<Frame> m_frames;
    std::vector<std::string> m_output;
};

// The live debugger observes the game VM. Only a scheduler-owned
// lua_resume can be suspended safely. Other callbacks produce snapshots.
class LiveDebugger {
public:
    void Attach(lua_State* host, const std::string& file, const std::set<int>& breakpoints);
    void Detach();
    void RefreshHook();
    void SetBreakpoints(const std::set<int>& breakpoints) { m_breakpoints = breakpoints; }
    bool EnterScheduled(lua_State* thread);
    void LeaveScheduled(lua_State* thread, int result);
    void ForgetThread(lua_State* thread);
    void Continue(Session::Step step);
    bool CaptureError(lua_State* thread, int firstFrame);

    bool Attached() const { return m_host != nullptr; }
    bool Paused() const { return m_paused; }
    unsigned Revision() const { return m_revision; }
    const std::vector<Frame>& Frames() const { return m_frames; }
    const std::string& Message() const { return m_message; }

private:
    static void Hook(lua_State* thread, lua_Debug* debug);
    void OnLine(lua_State* thread, lua_Debug* debug);
    void Capture(lua_State* thread, int firstFrame, std::string message);

    std::thread::id m_owner;
    lua_State* m_host = nullptr;
    lua_State* m_scheduled = nullptr;
    lua_State* m_target = nullptr;
    void (*m_previous)(lua_State*, lua_Debug*) = nullptr;
    int m_previousMask = 0;
    int m_previousCount = 0;
    bool m_restoreJit = false;
    bool m_paused = false;
    bool m_skip = false;
    int m_depth = 0;
    int m_line = 0;
    unsigned m_revision = 0;
    unsigned long long m_lastTrace = 0;
    Session::Step m_step = Session::Step::Continue;
    std::string m_file;
    std::string m_lastSource;
    std::string m_message;
    std::set<int> m_breakpoints;
    std::vector<Frame> m_frames;
};

LiveDebugger& Live();

} // namespace lua_workbench
