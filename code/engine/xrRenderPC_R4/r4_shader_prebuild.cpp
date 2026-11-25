#include "stdafx.h"

// Written by Ivan Danko, 2026-11-14: item 9 stage E point 4 ( multithreading ) - the shader build
// ahead on all cores, see r4_shader_prebuild.h.
// Створено (Ivan Danko, 2026-11-14): пункт 9 етап E пункт 4 ( багатопотоковість ) - збирання
// шейдерів наперед на всіх ядрах, див. r4_shader_prebuild.h.

#include "r4_shader_prebuild.h"
#include "xrCore/xrTasks.h"

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace shader_prebuild {
namespace {
using prebuild_clock = std::chrono::steady_clock;

s64 micros_since(const prebuild_clock::time_point& from) {
    return std::chrono::duration_cast<std::chrono::microseconds>(prebuild_clock::now() - from)
        .count();
}

// One pre-build. The workers only read jobs, includes, contents and with ( not changed after
// start ) and write the results of the requests they took, the atomics and threads ( under lock ).
// Одне попереднє збирання. Робочі потоки лише читають jobs, includes, contents і with ( після
// start вони не змінюються ) і записують результати взятих запитів, атомарні лічильники та threads
// ( під lock ).
struct context {
    xr_vector<job*> jobs;
    xr_vector<variant*> all;                       // in queue order
    xr_map<xr_string, xr_vector<variant*>> lookup; // "ext|settings key|source CRC" -> requests
    xr_map<xr_string, xr_string> includes;         // include name ( lower case ) -> its text
    xr_set<xr_string> contents;                    // the code files of the cache at start
    tools with;

    xrTasks::Group group;
    std::atomic<bool> cancelled{ false };
    std::atomic<u32> remaining{ 0 }; // requests not settled yet ( done or main )
    std::mutex lock;
    std::condition_variable settled;
    xr_set<DWORD> threads; // the threads that built something

    prebuild_clock::time_point started;
    std::atomic<s64> last_us{ 0 };  // the last result, from started
    std::atomic<s64> work_us{ 0 };  // the workers' time, all threads together
    std::atomic<u32> compiled{ 0 }; // texts compiled

    // the main thread's / головного потоку
    size_t cursor = 0; // next_extra
    u32 taken = 0, built_by_main = 0, written = 0;
    double waited_ms = 0;
};

struct summary_data {
    bool valid = false;
    u32 requests = 0, sources = 0, threads = 0;
    double seconds = 0, work_seconds = 0;
    u32 compiled = 0, found = 0, failed = 0, not_built = 0;
    u32 taken = 0, built_by_main = 0, written = 0;
    double waited_ms = 0;
};

context* g_context = nullptr;
summary_data g_summary; // of the last pre-build, after end / останнього збирання, після end
string512 g_note = "";  // the start line ( r4.cpp ) / рядок запуску ( r4.cpp )

// The includer of the workers: the texts the main thread read before ( r4.cpp, shader_include_crc ),
// by the name as written after #include, in lower case, as the includer of r4.cpp opens the files.
// Includer робочих потоків: тексти, які головний потік прочитав раніше ( r4.cpp,
// shader_include_crc ), за назвою, як її написано після #include, малими літерами, так само як
// includer з r4.cpp відкриває файли.
class memory_includer : public ID3DInclude {
public:
    explicit memory_includer(const xr_map<xr_string, xr_string>& texts) : m_texts(texts) {}

    HRESULT __stdcall Open(D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData,
                           LPCVOID* ppData, UINT* pBytes) {
        xr_string name = pFileName;
        for (char& c : name)
            if (c >= 'A' && c <= 'Z')
                c = char(c - 'A' + 'a');
        auto const found = m_texts.find(name);
        if (found == m_texts.end())
            return E_FAIL;
        *ppData = found->second.c_str();
        *pBytes = (UINT)found->second.size();
        return S_OK;
    }
    HRESULT __stdcall Close(LPCVOID pData) { return S_OK; }

private:
    const xr_map<xr_string, xr_string>& m_texts;
};

void macros_of(const variant& v, xr_vector<D3D_SHADER_MACRO>& out) {
    out.clear();
    for (const define& d : v.defines) {
        D3D_SHADER_MACRO const macro = { d.name.c_str(), d.null_value ? nullptr : d.value.c_str() };
        out.push_back(macro);
    }
    D3D_SHADER_MACRO const last = { nullptr, nullptr };
    out.push_back(last);
}

// A request settles: its kind, then done; a main thread waiting for it wakes up.
// Запит завершується: його kind, потім done; головний потік, що на нього чекає, прокидається.
void publish(context& c, variant& v, u32 kind) {
    v.kind = kind;
    v.state.store(state_done, std::memory_order_release);
    c.remaining.fetch_sub(1);
    {
        std::lock_guard<std::mutex> guard(c.lock); // no wake-up lost between its check and wait
    }
    c.settled.notify_all();
}

// One source on a worker thread. Each request still queued is taken one at a time ( the others
// stay free for the main thread ), preprocessed for its text hash as shader_compile computes it,
// then found in the cache by that text, given the code of the same text built for another request
// of this source, or compiled.
// Одне джерело на робочому потоці. Кожен запит, що ще в черзі, береться по одному ( решта
// лишаються вільними для головного потоку ), препроцесується для хешу тексту так, як його обчислює
// shader_compile, а потім знаходиться в кеші за цим текстом, отримує код того самого тексту,
// зібраний для іншого запиту цього джерела, або збирається.
void build_job(context& c, job& j) {
    if (c.cancelled.load())
        return;
    const prebuild_clock::time_point begin = prebuild_clock::now();
    {
        std::lock_guard<std::mutex> guard(c.lock);
        c.threads.insert(GetCurrentThreadId());
    }
    memory_includer includer(c.includes);
    xr_vector<D3D_SHADER_MACRO> macros;
    xr_vector<variant*> built; // this source's requests compiled here
    for (variant* const v : j.variants) {
        if (c.cancelled.load())
            break;
        u32 expected = state_queued;
        if (!v->state.compare_exchange_strong(expected, state_running))
            continue; // the main thread asked first and builds it itself
        macros_of(*v, macros);
        ID3DBlob* text = nullptr;
        ID3DBlob* errors = nullptr;
        HRESULT const preprocessed = c.with.preprocess(j.source.data(), j.source.size(), "",
                                                       macros.data(), &includer, &text, &errors);
        bool const hashed = SUCCEEDED(preprocessed) && text;
        if (hashed)
            v->hash = c.with.text_hash(text->GetBufferPointer(), text->GetBufferSize(),
                                       v->entry.c_str(), v->target.c_str(), v->flags,
                                       c.with.compiler);
        _RELEASE(text);
        _RELEASE(errors);
        if (!hashed) {
            publish(c, *v, kind_failed);
            continue;
        }
        string_path content;
        xr_sprintf(content, "%s.%s\\x%016llx", v->request.c_str(), j.ext.c_str(), v->hash);
        if (c.contents.find(xr_string(content)) != c.contents.end()) {
            publish(c, *v, kind_cached);
            continue;
        }
        variant* same = nullptr;
        for (variant* const b : built)
            if (b->hash == v->hash) {
                same = b;
                break;
            }
        if (same) {
            v->code = same->code;
            publish(c, *v, same->kind);
            continue;
        }
        ID3DBlob* code = nullptr;
        HRESULT const compiled = c.with.compile(
            j.source.data(), j.source.size(), "", macros.data(), &includer, v->entry.c_str(),
            v->target.c_str(), v->flags, 0, &code, &errors);
        u32 kind = kind_failed;
        if (SUCCEEDED(compiled) && code) {
            j.codes[v->index].assign((const char*)code->GetBufferPointer(), code->GetBufferSize());
            v->code = v->index;
            kind = kind_code;
            c.compiled.fetch_add(1);
        }
        _RELEASE(code);
        _RELEASE(errors);
        built.push_back(v);
        publish(c, *v, kind);
    }
    c.work_us.fetch_add(micros_since(begin));
    s64 const now = micros_since(c.started);
    s64 last = c.last_us.load();
    while (now > last && !c.last_us.compare_exchange_weak(last, now)) {
    }
}

// Every input of the compile the same: the source bytes, the flags, the entry, the target and the
// defines with their values, in their order.
// Усі вхідні дані збирання ті самі: байти джерела, прапорці, точка входу, ціль і макроси з їхніми
// значеннями, у їхньому порядку.
bool same_request(const variant& v, const void* source, u32 size, const D3D_SHADER_MACRO* defines,
                  LPCSTR entry, LPCSTR target, DWORD flags) {
    const job& j = *v.owner;
    if (j.source.size() != size || 0 != std::memcmp(j.source.data(), source, size))
        return false;
    if (v.flags != flags || 0 != xr_strcmp(v.entry.c_str(), entry) ||
        0 != xr_strcmp(v.target.c_str(), target))
        return false;
    size_t i = 0;
    for (const D3D_SHADER_MACRO* d = defines; d->Name; ++d, ++i) {
        if (i >= v.defines.size())
            return false;
        const define& mine = v.defines[i];
        if (0 != xr_strcmp(mine.name.c_str(), d->Name) || mine.null_value != !d->Definition)
            return false;
        if (d->Definition && 0 != xr_strcmp(mine.value.c_str(), d->Definition))
            return false;
    }
    return i == v.defines.size();
}

void summarize(context& c, summary_data& s) {
    s = summary_data();
    s.valid = true;
    s.requests = (u32)c.all.size();
    s.sources = (u32)c.jobs.size();
    {
        std::lock_guard<std::mutex> guard(c.lock);
        s.threads = (u32)c.threads.size();
    }
    s.seconds = c.last_us.load() / 1000000.0;
    s.work_seconds = c.work_us.load() / 1000000.0;
    s.compiled = c.compiled.load();
    for (const variant* v : c.all) {
        u32 const state = v->state.load(std::memory_order_acquire);
        if (state_main == state)
            continue;
        if (state_done != state || kind_none == v->kind)
            s.not_built++;
        else if (kind_cached == v->kind)
            s.found++;
        else if (kind_failed == v->kind)
            s.failed++;
    }
    s.taken = c.taken;
    s.built_by_main = c.built_by_main;
    s.written = c.written;
    s.waited_ms = c.waited_ms;
}
} // namespace

void start(xr_vector<job*>& jobs, xr_map<xr_string, xr_string>& includes,
           xr_set<xr_string>& contents, const tools& with) {
    end(); // a pre-build before it, not expected / попереднє збирання, не очікується
    context* const c = xr_new<context>();
    c->jobs.swap(jobs);
    c->includes.swap(includes);
    c->contents.swap(contents);
    c->with = with;
    for (job* const j : c->jobs) {
        j->codes.resize(j->variants.size());
        u32 const source_crc = crc32(j->source.data(), (u32)j->source.size());
        for (u32 i = 0; i < (u32)j->variants.size(); ++i) {
            variant* const v = j->variants[i];
            v->owner = j;
            v->index = i;
            c->all.push_back(v);
            string_path key;
            xr_sprintf(key, "%s|%s|%08x", j->ext.c_str(), v->key.c_str(), source_crc);
            c->lookup[xr_string(key)].push_back(v);
        }
    }
    c->remaining.store((u32)c->all.size());
    c->started = prebuild_clock::now();
    g_context = c;
    for (job* const j : c->jobs)
        xrTasks::Run(
            c->group, [c, j]() { build_job(*c, *j); }, xrTasks::Priority::Background);
}

bool take(LPCSTR ext, LPCSTR key, LPCSTR request, const void* source, u32 size,
          const D3D_SHADER_MACRO* defines, LPCSTR entry, LPCSTR target, DWORD flags, result& out) {
    context* const c = g_context;
    if (!c)
        return false;
    string_path lookup_key;
    xr_sprintf(lookup_key, "%s|%s|%08x", ext, key, crc32(source, size));
    auto const found = c->lookup.find(xr_string(lookup_key));
    if (found == c->lookup.end())
        return false;
    variant* chosen = nullptr;
    for (variant* const v : found->second) {
        if (!same_request(*v, source, size, defines, entry, target, flags))
            continue;
        chosen = v;
        if (0 == xr_strcmp(v->request.c_str(), request))
            break; // the same name as well: the same cache folder
    }
    if (!chosen)
        return false;
    variant& v = *chosen;
    u32 state = v.state.load(std::memory_order_acquire);
    if (state_queued == state) {
        if (v.state.compare_exchange_strong(state, state_main)) {
            c->remaining.fetch_sub(1);
            c->built_by_main++;
            return false; // no worker has started it: the game builds it, as before
        }
        // a worker took it meanwhile: state holds what it is now
    }
    if (state_main == state)
        return false;
    if (state_running == state) {
        const prebuild_clock::time_point wait_start = prebuild_clock::now();
        std::unique_lock<std::mutex> guard(c->lock);
        c->settled.wait(guard, [&v]() {
            return state_done == v.state.load(std::memory_order_acquire);
        });
        guard.unlock();
        c->waited_ms += micros_since(wait_start) / 1000.0;
    }
    if (0 == xr_strcmp(v.request.c_str(), request))
        v.used = true;
    if (kind_code == v.kind) {
        const xr_string& code = v.owner->codes[v.code];
        out.hash = v.hash;
        out.code = code.data();
        out.size = (u32)code.size();
        c->taken++;
        return true;
    }
    if (kind_cached == v.kind) {
        out.hash = v.hash;
        out.code = nullptr;
        out.size = 0;
        c->taken++;
        return true;
    }
    return false;
}

bool next_extra(const job*& j, const variant*& v) {
    context* const c = g_context;
    if (!c)
        return false;
    bool const stopped = c->cancelled.load(); // after cancel nothing settles any more
    while (c->cursor < c->all.size()) {
        variant* const x = c->all[c->cursor];
        u32 const state = x->state.load(std::memory_order_acquire);
        if (!stopped && (state_queued == state || state_running == state))
            return false; // not settled yet: the next call goes on from here
        ++c->cursor;
        if (state_done != state || x->used || kind_none == x->kind)
            continue;
        x->used = true;
        c->written++;
        j = x->owner;
        v = x;
        return true;
    }
    return false;
}

bool running() { return nullptr != g_context; }

bool finished() {
    context* const c = g_context;
    return c && 0 == c->remaining.load() && c->cursor >= c->all.size();
}

void cancel() {
    context* const c = g_context;
    if (!c)
        return;
    c->cancelled.store(true);
    xrTasks::Wait(c->group);
}

void end() {
    context* c = g_context;
    if (!c)
        return;
    c->cancelled.store(true);
    xrTasks::Wait(c->group); // after it no worker touches the context
    summarize(*c, g_summary);
    for (job* j : c->jobs) {
        for (variant* v : j->variants)
            xr_delete(v);
        xr_delete(j);
    }
    g_context = nullptr;
    xr_delete(c);
}

void note(LPCSTR line) {
    xr_strcpy(g_note, line);
    g_summary = summary_data(); // a new renderer: an older summary is not this one's
    Msg("%s", line);
}

void print_result() {
    summary_data s;
    bool const live = nullptr != g_context;
    if (live)
        summarize(*g_context, s);
    else
        s = g_summary;
    if (!s.valid)
        return;
    Msg("* Shader prebuild%s: %u shader requests of %u sources built ahead in %.2f s on %u threads "
        "( compile time over all threads %.2f s ): %u texts compiled, %u requests found in the "
        "cache by their text, %u refused by the compiler, %u not built",
        live ? " ( still building )" : "", s.requests, s.sources, s.seconds, s.threads,
        s.work_seconds, s.compiled, s.found, s.failed, s.not_built);
    Msg("* Shader prebuild: the game took %u of them and waited %.2f s for them, built %u itself; "
        "%u it did not ask for went into the cache",
        s.taken, s.waited_ms / 1000.0, s.built_by_main, s.written);
}

void print_stats() {
    if (g_note[0])
        Msg("%s", g_note);
    print_result();
}
} // namespace shader_prebuild
