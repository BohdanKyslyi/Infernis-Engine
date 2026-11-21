#include "pch_script.h"

#include "UILuaWorkbench.h"
#include "LuaWorkbench.h"
#include "UIDialogWnd.h"
#include "UIStatic.h"
#include "../UIGameCustom.h"
#include "../ai_space.h"
#include "script_engine.h"
#include "../ui_base.h"
#include "../../xrEngine/xr_input.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace {
using Session = lua_workbench::Session;
using Step = Session::Step;
using State = Session::State;
using lua_workbench::Live;

constexpr float Left = 24, Right = 1000, CodeX = 80, CodeRight = 658, DebugX = 686;
constexpr float BodyY = 131, Row = 18;
constexpr int CodeRows = 30, DebugRows = 29;
constexpr u32 Text = 0xffe5edf5, Dim = 0xff98adbf, Accent = 0xff76dace;
constexpr u32 Red = 0xffff818d, Gold = 0xfff5cf81;
constexpr char DraftFile[] = "lua_workbench_draft.lua";
constexpr char DefaultSource[] =
    "-- Ctrl+Enter: run   |   F5: run/continue   |   F9: breakpoint\n"
    "-- Ctrl+Space: complete   |   F10/F11: step over/into\n\n"
    "local actor = db and db.actor\n"
    "if actor then\n"
    "    local name = actor:name()\n"
    "    print('Actor:', name)\n"
    "    return actor:id()\n"
    "end\n"
    "print('No actor on this level')\n";

enum class Action { None, Check, Run, Debug, Over, Into, Out, Stop, Save, Load, LiveMode, Close };
struct Button { float x, width; const char* label; Action action; };
constexpr Button Buttons[] = {
    {34, 62, "Check", Action::Check}, {101, 89, "Run / F5", Action::Run},
    {195, 72, "Debug", Action::Debug}, {272, 84, "Over/F10", Action::Over},
    {361, 82, "Into/F11", Action::Into}, {448, 54, "Out", Action::Out},
    {507, 58, "Stop", Action::Stop}, {570, 91, "Save draft", Action::Save},
    {666, 92, "Load draft", Action::Load}, {763, 96, "Live attach", Action::LiveMode},
    {864, 124, "Close / Esc", Action::Close},
};

bool Ident(unsigned char c) { return std::isalnum(c) != 0 || c == '_'; }
bool Continuation(unsigned char c) { return (c & 0xc0) == 0x80; }

size_t Next(const std::string& s, size_t i) {
    if (i < s.size())
        while (++i < s.size() && Continuation(s[i])) {}
    return i;
}

size_t Prev(const std::string& s, size_t i) {
    if (i)
        while (Continuation(s[--i]) && i) {}
    return i;
}

std::wstring ToWide(std::string_view text, UINT page, DWORD flags) {
    if (text.empty())
        return {};
    const int length = MultiByteToWideChar(page, flags, text.data(), static_cast<int>(text.size()),
                                           nullptr, 0);
    if (!length)
        return {};
    std::wstring wide(length, L'\0');
    MultiByteToWideChar(page, flags, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

std::string FromWide(std::wstring_view wide, UINT page, BOOL* substituted = nullptr) {
    if (wide.empty())
        return {};
    const int length = WideCharToMultiByte(page, page == 1251 ? WC_NO_BEST_FIT_CHARS : 0,
        wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, substituted);
    if (!length)
        return {};
    std::string text(length, '\0');
    WideCharToMultiByte(page, page == 1251 ? WC_NO_BEST_FIT_CHARS : 0,
        wide.data(), static_cast<int>(wide.size()), text.data(), length, nullptr, substituted);
    return text;
}

std::string Display(std::string_view value) {
    if (std::all_of(value.begin(), value.end(), [](unsigned char c) { return c < 128; }))
        return std::string(value);
    const auto wide = ToWide(value, CP_UTF8, MB_ERR_INVALID_CHARS);
    return wide.empty() ? std::string(value) : FromWide(wide, 1251);
}

std::string Normalize(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
            result += '\n';
        } else {
            result += c;
        }
    }
    return result;
}

bool Copy(std::string_view text) {
    const auto wide = ToWide(text, CP_UTF8, MB_ERR_INVALID_CHARS);
    if ((!text.empty() && wide.empty()) || !OpenClipboard(Device.m_hWnd))
        return false;
    bool copied = false;
    HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
    if (data) {
        if (auto* bytes = GlobalLock(data)) {
            std::memcpy(bytes, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(data);
            if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, data)) {
                data = nullptr;
                copied = true;
            }
        }
        if (data)
            GlobalFree(data);
    }
    CloseClipboard();
    return copied;
}

std::string Paste() {
    std::string text;
    if (!OpenClipboard(Device.m_hWnd))
        return text;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* wide = static_cast<const wchar_t*>(GlobalLock(data))) {
            const size_t available = static_cast<size_t>(GlobalSize(data) / sizeof(wchar_t));
            const size_t max = (std::min)(available, Session::MaxSource + 1);
            const size_t length = static_cast<size_t>(std::find(wide, wide + max, L'\0') - wide);
            if (length < max)
                text = Normalize(FromWide({wide, length}, CP_UTF8));
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

bool Held(int a, int b) {
    return pInput->iGetAsyncKeyState(a) || pInput->iGetAsyncKeyState(b);
}

std::string KeyText(int dik) {
    if (dik <= 0 || dik >= 128)
        return {};
    const HKL layout = GetKeyboardLayout(0);
    const UINT key = MapVirtualKeyExW(dik, MAPVK_VSC_TO_VK_EX, layout);
    if (!key)
        return {};
    BYTE state[256]{};
    if (!GetKeyboardState(state))
        return {};
    state[VK_SHIFT] = Held(DIK_LSHIFT, DIK_RSHIFT) ? 0x80 : 0;
    state[VK_CONTROL] = Held(DIK_LCONTROL, DIK_RCONTROL) ? 0x80 : 0;
    state[VK_MENU] = Held(DIK_LMENU, DIK_RMENU) ? 0x80 : 0;
    wchar_t chars[8]{};
    const int count = ToUnicodeEx(key, dik, state, chars, 8, 0, layout);
    if (count <= 0 || chars[0] < L' ')
        return {};
    return FromWide({chars, static_cast<size_t>(count)}, CP_UTF8);
}

bool ScriptName(std::string_view name) {
    const size_t dot = name.find('.');
    if (dot == 0 || dot == std::string_view::npos || name.size() > 120)
        return false;
    if (name.substr(dot) != ".script" && name.substr(dot) != ".lua")
        return false;
    return std::all_of(name.begin(), name.begin() + dot, [](unsigned char c) {
        return Ident(c) || c == '-';
    });
}

class Workbench final : public CUIDialogWnd {
public:
    Workbench() {
        m_bWorkInPause = true;
        SetWndPos(Fvector2().set(0, 0));
        SetWndSize(Fvector2().set(1024, 768));
        m_quad.InitTexture("ui\\ui_weather_editor_white");
        m_quad.SetStretchTexture(true);
        SetSource(DefaultSource, DraftFile, false);
    }

    void Open(const char* name) {
        if (m_executing)
            return;
        if (!ScriptName(name)) {
            m_status = "Use a filename in gamedata/scripts, for example db.script.";
            return;
        }
        Read(name, false);
    }

    bool Executing() const { return m_executing; }

    void ResetRun() {
        Live().Detach();
        m_session.Stop();
        m_stale = true;
        m_diagnostics.clear();
        m_debugTop = 0;
    }

    void Show(bool show) override {
        if (!show) {
            if (m_session.GetState() == State::Paused) {
                m_session.Stop();
                RefreshDiagnostics(true);
            }
            m_action = Action::None;
            m_completion.clear();
            m_suggestPending = false;
            m_dragging = false;
        }
        CUIDialogWnd::Show(show);
    }

    void Update() override {
        const Action action = m_action;
        m_action = Action::None;
        if (action != Action::None)
            DispatchAction(action);
        if (Live().Attached() && m_liveRevision != Live().Revision()) {
            m_liveRevision = Live().Revision();
            m_frame = 0;
            RefreshDiagnostics(true);
            if (!Frames().empty() && Frames()[0].editorSource)
                Jump(Frames()[0].line);
        }
        if (m_suggestPending && static_cast<int>(Device.dwTimeContinual - m_suggestAt) >= 0) {
            m_suggestPending = false;
            Complete(false);
        }
    }

    void Draw() override {
        CGameFont& font = *UI().Font().pFontStat;
        const float oldHeight = font.GetHeight();
        font.SetHeight(UI().ClientToScreenScaledY(13));
        font.SetAligment(CGameFont::alLeft);
        m_font = &font;
        m_cell = 1.f;
        for (char c = '!'; c <= '~'; ++c)
            m_cell = (std::max)(m_cell, font.SizeOf_(c) / UI().ClientToScreenScaledX(1.f) + .5f);
        m_columns = (std::max)(8, static_cast<int>((CodeRight - CodeX - 8) / m_cell));

        Rect(Left, 24, Right - Left, 720, 0xf5161e28);
        Rect(Left, 24, Right - Left, 3, Accent);
        Rect(32, 115, 630, 565, 0xff101821);
        Rect(674, 115, 318, 565, 0xff111b26);
        Label(36, 39, 640, "ADCU735(Noir Engine)  /  LUA WORKBENCH", Accent);
        Label(36, 59, 952, m_name + (m_dirty ? "  *" : "") + "  |  " + StateName(), Text);
        for (const Button& button : Buttons) {
            const bool attached = button.action == Action::LiveMode && Live().Attached();
            Rect(button.x, 81, button.width, 27, attached ? 0xff32625c :
                Available(button.action) ? 0xff284456 : 0xff202c36);
            Label(button.x + 6, 87, button.width - 10, attached ? "Live detach" : button.label,
                  Available(button.action) ? Text : Dim);
        }
        Label(38, 116, 620, "SOURCE   |   Click gutter or F9 to toggle a breakpoint", Dim);
        Label(DebugX, 116, 300, "DIAGNOSTICS   |   Click frame to inspect", Accent);
        DrawCode();
        DrawDiagnostics();
        Rect(32, 686, 960, 50, 0xff202f3d);
        Label(40, 690, 942, m_status, Gold);
        Label(40, 711, 942, Hint(), Dim);
        font.OnRender();
        if (!m_completion.empty()) {
            DrawCompletion();
            font.OnRender();
        }
        font.SetHeight(oldHeight);
        m_font = nullptr;
    }

    bool OnMouseAction(float x, float y, EUIMessages action) override {
        if (action == WINDOW_MOUSE_WHEEL_UP || action == WINDOW_MOUSE_WHEEL_DOWN) {
            const int delta = action == WINDOW_MOUSE_WHEEL_UP ? -3 : 3;
            if (x >= 674) {
                const int last = (std::max)(0, static_cast<int>(m_diagnostics.size()) - DebugRows);
                m_debugTop = std::clamp(m_debugTop + delta, 0, last);
            } else {
                m_top = std::clamp(m_top + delta, 0, (std::max)(0, LineCount() - CodeRows));
            }
            return true;
        }
        if (action == WINDOW_LBUTTON_UP) {
            m_dragging = false;
            return true;
        }
        if (action != WINDOW_LBUTTON_DOWN && !(action == WINDOW_MOUSE_MOVE && m_dragging))
            return true;
        if (action == WINDOW_LBUTTON_DOWN) {
            m_completion.clear();
            m_suggestPending = false;
            if (y >= 81 && y < 108) {
                for (const auto& button : Buttons)
                    if (x >= button.x && x < button.x + button.width && Available(button.action))
                        m_action = button.action;
                return true;
            }
        }
        if (y < BodyY || y >= BodyY + CodeRows * Row)
            return true;
        const int row = static_cast<int>((y - BodyY) / Row);
        if (x >= DebugX) {
            const size_t selected = static_cast<size_t>(m_debugTop + row);
            if (action == WINDOW_LBUTTON_DOWN && row < DebugRows && selected < m_diagnostics.size() &&
                m_diagnostics[selected].frame >= 0) {
                m_frame = m_diagnostics[selected].frame;
                const auto& frame = Frames()[m_frame];
                if ((!m_stale || Live().Attached()) && frame.editorSource && frame.line > 0)
                    Jump(frame.line);
                RefreshDiagnostics(false);
            }
        } else if (x >= 33 && x < CodeRight && m_top + row < LineCount()) {
            if (x < CodeX) {
                if (action == WINDOW_LBUTTON_DOWN)
                    Breakpoint(m_top + row + 1);
            } else {
                const size_t at = Position(m_top + row, m_left +
                    (std::max)(0, static_cast<int>((x - CodeX) / m_cell + .5f)));
                SetCursor(at, action == WINDOW_MOUSE_MOVE || Held(DIK_LSHIFT, DIK_RSHIFT));
                m_dragging = true;
            }
        }
        return true;
    }

    bool OnKeyboardAction(int dik, EUIMessages action) override {
        if (action == WINDOW_KEY_PRESSED) {
            m_repeatKey = dik;
            m_repeatAt = Device.dwTimeContinual + 350;
            Key(dik);
        } else if (action == WINDOW_KEY_RELEASED && dik == m_repeatKey) {
            m_repeatKey = 0;
        }
        return true;
    }

    bool OnKeyboardHold(int dik) override {
        if (dik == m_repeatKey && Device.dwTimeContinual >= m_repeatAt && !Held(DIK_LCONTROL, DIK_RCONTROL) &&
            (dik == DIK_BACK || dik == DIK_DELETE || dik == DIK_LEFT || dik == DIK_RIGHT ||
             dik == DIK_UP || dik == DIK_DOWN || (dik > 0 && dik < DIK_F1 &&
             dik != DIK_ESCAPE && dik != DIK_RETURN && dik != DIK_TAB))) {
            m_repeatAt = Device.dwTimeContinual + 45;
            Key(dik);
        }
        return true;
    }

private:
    struct Diagnostic { std::string text; int frame; u32 color; };
    struct Edit { std::string source; size_t cursor; size_t anchor; std::set<int> breakpoints; };

    const std::vector<lua_workbench::Frame>& Frames() const {
        return Live().Attached() ? Live().Frames() : m_session.Frames();
    }

    const char* StateName() const {
        if (Live().Attached())
            return Live().Paused() ? "LIVE: game thread paused" : "LIVE: watching game scripts";
        if (m_stale)
            return "source changed";
        switch (m_session.GetState()) {
        case State::Paused: return "paused";
        case State::Finished: return "finished";
        case State::Failed: return "error";
        default: return "ready";
        }
    }

    bool Available(Action action) const {
        if (Live().Attached()) {
            switch (action) {
            case Action::Check: case Action::Debug: case Action::Load: return false;
            case Action::Run: case Action::Into: case Action::Over: case Action::Out: return Live().Paused();
            default: return true;
            }
        }
        const bool paused = m_session.GetState() == State::Paused;
        switch (action) {
        case Action::Over: case Action::Out: case Action::Stop: return paused;
        case Action::Check: case Action::Debug: case Action::Load: case Action::LiveMode: return !paused;
        default: return true;
        }
    }

    void SetSource(std::string source, std::string name, bool legacy, bool gameSource = false) {
        m_session.Stop();
        m_source = std::move(source);
        m_name = std::move(name);
        m_legacy = legacy;
        m_gameSource = gameSource;
        m_sourceEdited = false;
        m_top = m_left = 0;
        m_cursor = m_anchor = 0;
        m_breakpoints.clear();
        m_completion.clear();
        m_suggestPending = false;
        m_undo.clear();
        m_redo.clear();
        m_diagnostics.clear();
        m_frame = m_debugTop = 0;
        m_dirty = false;
        m_stale = true;
        IndexLines();
    }

    void Read(const char* name, bool draft) {
        if (Live().Attached()) {
            m_status = "Detach the live debugger before opening another script.";
            return;
        }
        if (m_session.GetState() == State::Paused) {
            m_status = "Stop the current test before loading another script.";
            return;
        }
        if (m_dirty) {
            m_status = "Save the current draft before loading another source.";
            return;
        }
        std::string text;
        if (draft) {
            string_path path;
            FS.update_path(path, "$app_data_root$", name);
            FILE* file = nullptr;
            if (fopen_s(&file, path, "rb") != 0 || !file) {
                m_status = std::string("Cannot open ") + name;
                return;
            }
            text.resize(Session::MaxSource + 1);
            const size_t bytes = fread(text.data(), 1, text.size(), file);
            const bool failed = ferror(file) != 0;
            fclose(file);
            if (failed) {
                m_status = "Could not read the draft.";
                return;
            }
            text.resize(bytes);
        } else {
            IReader* file = FS.r_open("$game_scripts$", name);
            if (!file) {
                m_status = std::string("Cannot open ") + name;
                return;
            }
            const size_t length = static_cast<size_t>(file->length());
            if (length <= Session::MaxSource && length != 0)
                text.assign(static_cast<const char*>(file->pointer()), length);
            const bool oversized = length > Session::MaxSource;
            FS.r_close(file);
            if (oversized) {
                m_status = "Script exceeds the 256 KiB editor limit.";
                return;
            }
        }
        if (text.size() > Session::MaxSource) {
            m_status = "Script exceeds the 256 KiB editor limit.";
            return;
        }
        if (text.find('\0') != std::string::npos || (!text.empty() && text[0] == '\x1b')) {
            m_status = "Expected a Lua source file. Binary files cannot be opened.";
            return;
        }
        if (text.compare(0, 3, "\xef\xbb\xbf") == 0)
            text.erase(0, 3);
        const bool legacy = !text.empty() && ToWide(text, CP_UTF8, MB_ERR_INVALID_CHARS).empty();
        if (legacy)
            text = FromWide(ToWide(text, 1251, 0), CP_UTF8);
        if (text.size() > Session::MaxSource) {
            m_status = "Converted script exceeds the 256 KiB editor limit.";
            return;
        }
        SetSource(Normalize(text), name, legacy, !draft);
        m_status = std::string("Loaded ") + name + (legacy ? " (Windows-1251)" : " (UTF-8)");
    }

    bool Encode(std::string& text) {
        if (!m_legacy) {
            text = m_source;
            return true;
        }
        BOOL loss = FALSE;
        text = FromWide(ToWide(m_source, CP_UTF8, MB_ERR_INVALID_CHARS), 1251, &loss);
        if (loss)
            m_status = "This Windows-1251 script contains characters its encoding cannot represent.";
        return !loss;
    }

    void Save() {
        std::string bytes;
        if (!Encode(bytes))
            return;
        string_path path;
        FS.update_path(path, "$app_data_root$", DraftFile);
        const std::string temp = std::string(path) + ".tmp";
        FILE* file = nullptr;
        if (fopen_s(&file, temp.c_str(), "wb") != 0 || !file) {
            m_status = "Could not open the draft for writing.";
            return;
        }
        const bool written = fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
        const bool closed = fclose(file) == 0;
        if (!written || !closed || !MoveFileExA(temp.c_str(), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileA(temp.c_str());
            m_status = "Could not save the draft.";
            return;
        }
        m_dirty = false;
        m_status = std::string("Draft saved to ") + DraftFile;
    }

    void DispatchAction(Action action) {
        if (m_executing)
            return;
        if (action == Action::Close) {
            HideDialog();
            return;
        }
        if (!Available(action))
            return;
        if (action == Action::Save) {
            Save();
            return;
        }
        if (action == Action::Load) {
            Read(DraftFile, true);
            return;
        }

        m_status.clear();
        m_completion.clear();
        m_suggestPending = false;
        if (action == Action::LiveMode || Live().Attached()) {
            if (action == Action::LiveMode && !Live().Attached()) {
                if (!m_gameSource || m_sourceEdited || m_breakpoints.empty()) {
                    m_status = "Open an unedited game script and set at least one breakpoint first.";
                    return;
                }
                lua_State* host = ai().script_engine().lua();
                if (!host)
                    return;
                Live().Attach(host, m_name, m_breakpoints);
                m_status = "Live attached. Esc hides this window so you can reproduce the event.";
            } else if (action == Action::LiveMode || action == Action::Stop) {
                Live().Detach();
                m_status = "Live debugger detached.";
            } else {
                const Step step = action == Action::Over ? Step::Over : action == Action::Into ? Step::Into :
                    action == Action::Out ? Step::Out : Step::Continue;
                Live().Continue(step);
            }
            m_frame = 0;
            RefreshDiagnostics(true);
            return;
        }
        std::string encoded;
        if (action == Action::Stop) {
            m_session.Stop();
        } else if (m_session.GetState() == State::Paused) {
            const Step step = action == Action::Over ? Step::Over : action == Action::Into ? Step::Into :
                action == Action::Out ? Step::Out : Step::Continue;
            m_executing = true;
            m_session.Resume(step);
            m_executing = false;
        } else if (Encode(encoded)) {
            lua_State* host = ai().script_engine().lua();
            if (!host) {
                m_status = "The game Lua state is unavailable.";
                return;
            }
            m_stale = false;
            if (action == Action::Check)
                m_session.Check(host, encoded, m_name);
            else {
                m_executing = true;
                m_session.Start(host, encoded, m_name, m_breakpoints,
                    action == Action::Debug || action == Action::Into ? Step::Into : Step::Continue);
                m_executing = false;
            }
        } else {
            return;
        }
        m_frame = 0;
        RefreshDiagnostics(true);
        if (m_session.ErrorLine())
            Jump(m_session.ErrorLine());
        else if (m_session.GetState() == State::Paused && !m_session.Frames().empty() &&
                 m_session.Frames()[0].editorSource)
            Jump(m_session.Frames()[0].line);
    }

    void IndexLines() {
        m_lines.assign(1, 0);
        for (size_t i = 0; i < m_source.size(); ++i)
            if (m_source[i] == '\n')
                m_lines.push_back(i + 1);
    }

    int LineCount() const { return static_cast<int>(m_lines.size()); }
    int LineOf(size_t pos) const {
        return static_cast<int>(std::upper_bound(m_lines.begin(), m_lines.end(), pos) - m_lines.begin()) - 1;
    }
    size_t End(int line) const {
        return line + 1 == LineCount() ? m_source.size() : m_lines[line + 1] - 1;
    }
    int Column(int line, size_t pos) const {
        int column = 0;
        for (size_t i = m_lines[line]; i < pos; i = Next(m_source, i))
            column += m_source[i] == '\t' ? 4 - column % 4 : 1;
        return column;
    }
    size_t Position(int line, int column) const {
        size_t i = m_lines[line];
        const size_t end = End(line);
        int col = 0;
        while (i < end && col < column) {
            col += m_source[i] == '\t' ? 4 - col % 4 : 1;
            if (col > column)
                break;
            i = Next(m_source, i);
        }
        return i;
    }

    void SetCursor(size_t pos, bool extend = false) {
        m_cursor = pos;
        if (!extend)
            m_anchor = pos;
        const int line = LineOf(pos);
        const int col = Column(line, pos);
        if (line < m_top)
            m_top = line;
        if (line >= m_top + CodeRows)
            m_top = line - CodeRows + 1;
        if (col < m_left)
            m_left = col;
        if (col >= m_left + m_columns)
            m_left = col - m_columns + 1;
    }

    void Jump(int line) {
        if (line >= 1 && line <= LineCount())
            SetCursor(m_lines[line - 1]);
    }

    void Breakpoint(int line) {
        if (m_session.GetState() == State::Paused) {
            m_status = "Stop the current run to change its breakpoints.";
            return;
        }
        if (!m_breakpoints.erase(line))
            m_breakpoints.insert(line);
        if (Live().Attached())
            Live().SetBreakpoints(m_breakpoints);
    }

    void Remember(std::vector<Edit>& history) {
        if (history.size() >= 24)
            history.erase(history.begin());
        history.push_back({m_source, m_cursor, m_anchor, m_breakpoints});
    }

    void Undo(std::vector<Edit>& from, std::vector<Edit>& to) {
        if (from.empty() || m_session.GetState() == State::Paused || Live().Attached())
            return;
        Remember(to);
        Edit edit = std::move(from.back());
        from.pop_back();
        m_source = std::move(edit.source);
        m_breakpoints = std::move(edit.breakpoints);
        IndexLines();
        m_anchor = edit.anchor;
        SetCursor(edit.cursor, true);
        MarkChanged();
    }

    void MarkChanged() {
        m_dirty = m_stale = m_sourceEdited = true;
        m_status.clear();
        m_completion.clear();
    }

    void Replace(size_t begin, size_t end, const std::string& value) {
        if (Live().Attached() || m_session.GetState() == State::Paused || (begin == end && value.empty()))
            return;
        if (m_source.size() - (end - begin) + value.size() > Session::MaxSource) {
            m_status = "Source exceeds the 256 KiB editor limit.";
            return;
        }
        Remember(m_undo);
        m_redo.clear();
        const int first = LineOf(begin) + 1;
        const int last = LineOf(end) + 1;
        const int inserted = static_cast<int>(std::count(value.begin(), value.end(), '\n'));
        if (first != last || inserted) {
            std::set<int> relocated;
            for (int point : m_breakpoints) {
                if (point < first)
                    relocated.insert(point);
                else if (point > last)
                    relocated.insert(point + inserted - (last - first));
                else if (point == first && (first == last || begin != m_lines[first - 1]))
                    relocated.insert(point + (begin == m_lines[first - 1] ? inserted : 0));
                else if (point == last && end == m_lines[last - 1])
                    relocated.insert(first + inserted);
            }
            m_breakpoints = std::move(relocated);
        }
        m_source.replace(begin, end - begin, value);
        IndexLines();
        SetCursor(begin + value.size());
        MarkChanged();
    }

    size_t TokenStart(size_t end, bool dotted) const {
        while (end) {
            const char c = m_source[end - 1];
            if (!Ident(c) && !(dotted && (c == '.' || c == ':')))
                break;
            --end;
        }
        return end;
    }

    void Complete(bool explicitRequest) {
        m_suggestPending = false;
        if (Live().Attached() || m_session.GetState() == State::Paused || m_cursor != m_anchor)
            return;
        const size_t start = TokenStart(m_cursor, true);
        const auto expression = m_source.substr(start, m_cursor - start);
        if (!explicitRequest && expression.size() < 2 && expression.find_first_of(".:") == std::string::npos)
            return;
        const auto lineStart = m_lines[LineOf(start)];
        const bool statementStart = m_source.find_first_not_of(" \t", lineStart) >= start;
        m_completion = lua_workbench::Suggest(ai().script_engine().lua(), m_source.substr(0, start),
                                              expression, statementStart);
        m_completionIndex = 0;
        if (explicitRequest && m_completion.empty())
            m_status = "No completions for this Lua expression.";
    }

    void AcceptCompletion() {
        const auto item = m_completion[m_completionIndex];
        const size_t start = TokenStart(m_cursor, false);
        size_t end = m_cursor;
        while (end < m_source.size() && Ident(m_source[end]))
            ++end;
        if (end < m_source.size() && m_source[end] == '(' &&
            item.insertion.compare(0, item.name.size() + 1, item.name + "(") == 0) {
            Replace(start, end, item.name);
            return;
        }

        const size_t lineStart = m_lines[LineOf(start)];
        const size_t contentStart = (std::min)(start, m_source.find_first_not_of(" \t", lineStart));
        const auto indent = m_source.substr(lineStart, contentStart - lineStart);
        std::string insertion;
        size_t anchor = std::string::npos, caret = std::string::npos;
        for (const char c : item.insertion) {
            if (c == '$') {
                if (anchor == std::string::npos)
                    anchor = insertion.size();
                else
                    caret = insertion.size();
                continue;
            }
            insertion += c;
            if (c == '\n')
                insertion += indent;
        }
        if (m_source.size() - (end - start) + insertion.size() > Session::MaxSource)
            return;
        Replace(start, end, insertion);
        if (anchor != std::string::npos && caret != std::string::npos) {
            m_anchor = start + anchor;
            SetCursor(start + caret, true);
        }
    }

    std::string Hint() const {
        if (!m_completion.empty())
            return m_completion[m_completionIndex].signature;
        if (Live().Attached())
            return "F5 continue | F10 over | F11 into | Shift+F11 out | Esc hide | Stop detach";
        return "Ctrl+Space complete | Ctrl+Z / Y undo / redo | Ctrl+S save | Ctrl+Shift+C copy diagnostics";
    }

    void Key(int dik) {
        m_suggestPending = false;
        const bool ctrl = Held(DIK_LCONTROL, DIK_RCONTROL);
        const bool shift = Held(DIK_LSHIFT, DIK_RSHIFT);
        if (!m_completion.empty()) {
            if (dik == DIK_ESCAPE) {
                m_completion.clear();
                return;
            }
            if (dik == DIK_UP || dik == DIK_DOWN) {
                const int count = static_cast<int>(m_completion.size());
                m_completionIndex = (m_completionIndex + (dik == DIK_UP ? count - 1 : 1)) % count;
                return;
            }
            if (!ctrl && (dik == DIK_TAB || dik == DIK_RETURN)) {
                AcceptCompletion();
                m_completion.clear();
                return;
            }
            m_completion.clear();
        }

        if (dik == DIK_ESCAPE) { m_action = Action::Close; return; }
        if (dik == DIK_F5 || (ctrl && dik == DIK_RETURN)) { m_action = Action::Run; return; }
        if (dik == DIK_F10) { m_action = Action::Over; return; }
        if (dik == DIK_F11) { m_action = shift ? Action::Out : Action::Into; return; }
        if (dik == DIK_F9) { Breakpoint(LineOf(m_cursor) + 1); return; }
        if (ctrl && dik == DIK_S) { m_action = Action::Save; return; }

        const size_t first = (std::min)(m_cursor, m_anchor);
        const size_t last = (std::max)(m_cursor, m_anchor);
        const int line = LineOf(m_cursor);
        const int col = Column(line, m_cursor);
        switch (dik) {
        case DIK_LEFT: SetCursor(!shift && first != last ? first : Prev(m_source, m_cursor), shift); return;
        case DIK_RIGHT: SetCursor(!shift && first != last ? last : Next(m_source, m_cursor), shift); return;
        case DIK_HOME: SetCursor(ctrl ? 0 : m_lines[line], shift); return;
        case DIK_END: SetCursor(ctrl ? m_source.size() : End(line), shift); return;
        case DIK_UP: case DIK_DOWN: case DIK_PRIOR: case DIK_NEXT: {
            const int delta = dik == DIK_UP ? -1 : dik == DIK_DOWN ? 1 : dik == DIK_PRIOR ? -CodeRows : CodeRows;
            SetCursor(Position(std::clamp(line + delta, 0, LineCount() - 1), col), shift);
            return;
        }
        }

        if (ctrl && !Held(DIK_RMENU, DIK_RMENU)) {
            switch (dik) {
            case DIK_A: m_anchor = 0; SetCursor(m_source.size(), true); break;
            case DIK_C: case DIK_X:
                if (dik == DIK_C && shift) {
                    std::string text;
                    for (const auto& row : m_diagnostics) {
                        const auto wide = ToWide(row.text, CP_UTF8, MB_ERR_INVALID_CHARS);
                        text += wide.empty() ? FromWide(ToWide(row.text, 1251, 0), CP_UTF8) : row.text;
                        text += "\r\n";
                    }
                    m_status = Copy(text) ? "Diagnostics copied." : "Clipboard is unavailable.";
                    break;
                }
                if (first != last) {
                    if (!Copy(std::string_view(m_source).substr(first, last - first)))
                        m_status = "Clipboard is unavailable.";
                    else if (dik == DIK_X)
                        Replace(first, last, "");
                }
                break;
            case DIK_V: {
                const auto text = Paste();
                if (!text.empty())
                    Replace(first, last, text);
                break;
            }
            case DIK_Z: Undo(m_undo, m_redo); break;
            case DIK_Y: Undo(m_redo, m_undo); break;
            case DIK_SPACE:
                if (m_session.GetState() != State::Paused)
                    Complete(true);
                break;
            }
            return;
        }

        switch (dik) {
        case DIK_BACK: Replace(first == last ? Prev(m_source, first) : first, last, ""); return;
        case DIK_DELETE: Replace(first, first == last ? Next(m_source, last) : last, ""); return;
        case DIK_TAB: Replace(first, last, std::string(4 - col % 4, ' ')); return;
        case DIK_RETURN: case DIK_NUMPADENTER: {
            size_t i = m_lines[line];
            while (i < first && (m_source[i] == ' ' || m_source[i] == '\t'))
                ++i;
            Replace(first, last, "\n" + m_source.substr(m_lines[line], i - m_lines[line]));
            return;
        }
        }
        const auto text = KeyText(dik);
        if (!text.empty()) {
            Replace(first, last, text);
            if (!Live().Attached() && (Ident(text.back()) || text.back() == '.' || text.back() == ':')) {
                m_suggestAt = Device.dwTimeContinual + 140;
                m_suggestPending = true;
            }
        }
    }

    void Rect(float x, float y, float w, float h, u32 color) {
        m_quad.SetWndPos(Fvector2().set(x, y));
        m_quad.SetWndSize(Fvector2().set(w, h));
        m_quad.SetTextureColor(color);
        m_quad.DrawTexture();
    }

    void Label(float x, float y, float width, std::string_view value, u32 color) {
        std::string bytes = Display(value.substr(0, 512));
        for (char& c : bytes)
            if (static_cast<unsigned char>(c) < ' ')
                c = ' ';
        const float limit = UI().ClientToScreenScaledX(width);
        while (!bytes.empty() && m_font->SizeOf_(bytes.c_str()) > limit)
            bytes.pop_back();
        if (bytes.empty())
            return;
        m_font->SetColor(color);
        m_font->Out(UI().ClientToScreenScaledX(x), UI().ClientToScreenScaledY(y), "%s", bytes.c_str());
    }

    void DrawCode() {
        const size_t selectFirst = (std::min)(m_cursor, m_anchor);
        const size_t selectLast = (std::max)(m_cursor, m_anchor);
        const int caretLine = LineOf(m_cursor);
        const bool live = Live().Attached();
        const bool paused = live ? Live().Paused() : m_session.GetState() == State::Paused;
        int stoppedLine = live ? 0 : m_session.ErrorLine();
        if (!stoppedLine && (live || paused)) {
            for (const auto& frame : Frames())
                if (frame.editorSource) { stoppedLine = frame.line; break; }
        }
        constexpr std::string_view keywords =
            " and break do else elseif end false for function if in local nil not or repeat return then true until while ";
        for (int line = m_top; line < (std::min)(m_top + CodeRows, LineCount()); ++line) {
            const float y = BodyY + (line - m_top) * Row;
            if ((live || !m_stale) && line + 1 == stoppedLine)
                Rect(34, y, 624, Row, paused ? 0xff444026 : 0xff482c38);
            else if (line == caretLine)
                Rect(34, y, 624, Row, 0xff213340);
            const bool point = m_breakpoints.count(line + 1) != 0;
            Label(38, y + 2, 40, (point ? "*" : "") + std::to_string(line + 1), point ? Red : Dim);
            const size_t end = End(line);
            u32 color = Text;
            int col = 0;
            size_t tokenEnd = m_lines[line];
            for (size_t i = m_lines[line]; i < end && col < m_left + m_columns; i = Next(m_source, i)) {
                const char c = m_source[i];
                if (i >= tokenEnd) {
                    color = Text;
                    tokenEnd = Next(m_source, i);
                    if (c == '-' && i + 1 < end && m_source[i + 1] == '-') {
                        color = 0xff81a491;
                        tokenEnd = end;
                    } else if (c == '\'' || c == '"') {
                        color = Gold;
                        while (tokenEnd < end) {
                            const char ch = m_source[tokenEnd++];
                            if (ch == '\\' && tokenEnd < end)
                                ++tokenEnd;
                            else if (ch == c)
                                break;
                        }
                    } else if (Ident(c)) {
                        while (tokenEnd < end && Ident(m_source[tokenEnd]))
                            ++tokenEnd;
                        if (std::isdigit(static_cast<unsigned char>(c)))
                            color = Gold;
                        else if (tokenEnd - i <= 8 && keywords.find(" " + m_source.substr(i, tokenEnd - i) + " ") !=
                                 std::string_view::npos)
                            color = Accent;
                    }
                }
                const int cells = c == '\t' ? 4 - col % 4 : 1;
                if (col + cells > m_left && i >= selectFirst && i < selectLast) {
                    const int from = (std::max)(col, m_left);
                    const int to = (std::min)(col + cells, m_left + m_columns);
                    Rect(CodeX + (from - m_left) * m_cell, y, (to - from) * m_cell, Row, 0xff395e76);
                }
                if (col >= m_left && c != '\t' && c != ' ')
                    Label(CodeX + (col - m_left) * m_cell, y + 2, m_cell + .5f,
                          std::string_view(m_source).substr(i, Next(m_source, i) - i), color);
                col += cells;
            }
            if (line == caretLine) {
                const int caret = Column(line, m_cursor) - m_left;
                if (caret >= 0 && caret < m_columns)
                    Rect(CodeX + caret * m_cell, y + 2, 1.2f, Row - 4, Accent);
            }
        }
    }

    void RefreshDiagnostics(bool resetScroll) {
        m_diagnostics.clear();
        const bool live = Live().Attached();
        const std::vector<std::string> liveOutput = live ? std::vector<std::string>{Live().Message()} :
                                                          std::vector<std::string>{};
        const auto& output = live ? liveOutput : m_session.Output();
        const auto& frames = Frames();
        const bool failed = !output.empty() && (live || m_session.GetState() == State::Failed);
        m_diagnostics.push_back({"OUTPUT", -1, Accent});
        // Keep the latest diagnostic and call stack visible after verbose output.
        size_t first = output.size() > 8 ? output.size() - 8 : 0;
        if (first)
            m_diagnostics.push_back({"Scroll down for earlier output", -1, Dim});
        const auto addOutput = [&](size_t index) {
            const auto& line = output[index];
            for (size_t at = 0; at < line.size() || at == 0; ) {
                const size_t end = line.find_first_of("\r\n", at);
                const size_t limit = end == std::string::npos ? line.size() : end;
                size_t part = at;
                do {
                    size_t next = (std::min)(part + 47, limit);
                    while (next < limit && Continuation(line[next]))
                        ++next;
                    m_diagnostics.push_back({line.substr(part, next - part), -1,
                        failed && index + 1 == output.size() ? (live ? Gold : Red) : Text});
                    part = next;
                } while (part < limit);
                if (end == std::string::npos)
                    break;
                at = end + 1;
            }
        };
        // Put errors and locals above the long tail of the console log.
        if (failed)
            addOutput(output.size() - 1);
        if (!frames.empty()) {
            m_diagnostics.push_back({"CALL STACK", -1, Accent});
            for (size_t i = 0; i < frames.size(); ++i) {
                const auto& f = frames[i];
                const size_t slash = f.source.find_last_of("/\\");
                const std::string file = f.source.substr(slash == std::string::npos ? 0 : slash + 1);
                m_diagnostics.push_back({(i == static_cast<size_t>(m_frame) ? "> " : "  ") + file +
                    (f.line > 0 ? ":" + std::to_string(f.line) : "") + "  " + f.function,
                    static_cast<int>(i), i == static_cast<size_t>(m_frame) ? Gold : Text});
            }
            m_diagnostics.push_back({"LOCALS / UPVALUES", -1, Accent});
            for (const auto& local : frames[m_frame].variables)
                m_diagnostics.push_back({local, -1, Text});
            if (frames[m_frame].variables.empty())
                m_diagnostics.push_back({"No variables for this frame", -1, Dim});
            m_diagnostics.push_back({"OUTPUT", -1, Accent});
        }
        const size_t end = output.size() - (failed ? 1 : 0);
        for (size_t i = first; i < end; ++i)
            addOutput(i);
        if (first) {
            m_diagnostics.push_back({"EARLIER OUTPUT", -1, Accent});
            for (size_t i = 0; i < first; ++i)
                addOutput(i);
        }
        if (resetScroll)
            m_debugTop = 0;
        else
            m_debugTop = (std::min)(m_debugTop, (std::max)(0, static_cast<int>(m_diagnostics.size()) - DebugRows));
    }

    void DrawDiagnostics() {
        if (m_diagnostics.empty()) {
            Label(DebugX, BodyY + 2, 300, "Check or run a script to see results.", Dim);
            return;
        }
        for (size_t i = m_debugTop; i < m_diagnostics.size() && i < static_cast<size_t>(m_debugTop + DebugRows); ++i) {
            const auto& row = m_diagnostics[i];
            Label(DebugX, BodyY + (i - m_debugTop) * Row + 2, 298, row.text, row.color);
        }
    }

    void DrawCompletion() {
        if (m_completion.empty())
            return;
        const int first = (std::max)(0, m_completionIndex - 5);
        const int count = (std::min)(6, static_cast<int>(m_completion.size()) - first);
        const float x = 320;
        const float y = 678 - (count + 1) * Row;
        Rect(x, y, 334, (count + 1) * Row, 0xff334b60);
        Label(x + 7, y + 2, 320, "COMPLETION  /  Enter or Tab", Accent);
        for (int i = 0; i < count; ++i) {
            const bool active = i + first == m_completionIndex;
            Label(x + 7, y + (i + 1) * Row + 2, 320, (active ? "> " : "  ") + m_completion[i + first].signature,
                  active ? Gold : Text);
        }
    }

    Session m_session;
    CUIStatic m_quad;
    CGameFont* m_font = nullptr;
    std::string m_source, m_name, m_status;
    std::vector<size_t> m_lines;
    std::set<int> m_breakpoints;
    std::vector<Edit> m_undo, m_redo;
    std::vector<Diagnostic> m_diagnostics;
    std::vector<lua_workbench::Completion> m_completion;
    size_t m_cursor = 0, m_anchor = 0;
    int m_top = 0, m_left = 0, m_columns = 74, m_debugTop = 0, m_frame = 0;
    int m_completionIndex = 0, m_repeatKey = 0;
    u32 m_repeatAt = 0, m_suggestAt = 0;
    unsigned m_liveRevision = 0;
    float m_cell = 8.f;
    bool m_dirty = false, m_stale = true, m_legacy = false, m_dragging = false;
    bool m_executing = false, m_suggestPending = false, m_gameSource = false, m_sourceEdited = false;
    Action m_action = Action::None;
};

Workbench* workbench = nullptr;
} // namespace

bool LuaWorkbenchShown() { return workbench && workbench->IsShown(); }

void ShowLuaWorkbench(bool show) {
    if (workbench && workbench->Executing())
        return;
    if (!show) {
        if (Live().Attached() && workbench)
            workbench->ResetRun();
        if (LuaWorkbenchShown())
            workbench->HideDialog();
        return;
    }
    if (!workbench)
        workbench = xr_new<Workbench>();
    if (!workbench->IsShown())
        CurrentGameUI()->StartDialog(workbench, true);
}

void OpenLuaWorkbenchScript(const char* name) {
    ShowLuaWorkbench(true);
    workbench->Open(name);
}

void DestroyLuaWorkbench() {
    ShowLuaWorkbench(false);
    xr_delete(workbench);
}

void ResetLuaWorkbenchRun() {
    if (workbench)
        workbench->ResetRun();
}
