#pragma once

// Written by Ivan Danko, 2026-11-14: item 9 stage E point 4 ( multithreading ) - the shader build
// ahead on all cores. Our own design; the donor ( Monolith ) builds a shader only when the game
// asks for it, one by one.
//
// At renderer start ( CRender::create, r4.cpp ) every shader request the game can make with the
// current settings is worked out by shader_compile itself: each .vs, .ps and .gs of the shader
// folder ( addons included ), a vertex shader as plain and with skinning 0..4, as the resource
// manager asks for them. The list comes from the game's own files and the current settings, so it
// is the same for every player, any settings and any hardware. A request whose cache file is
// valid is left out, so a start with a full cache builds nothing. The others are preprocessed and
// compiled on the background worker threads of the task system ( xrCore\xrTasks.h ) while the
// game goes on loading. When the game asks for a shader, shader_compile takes the ready code,
// waits for a shader a worker is building at that moment, or builds a shader no worker has
// started yet itself, as before. A result is taken only when every input of the compile is the
// same: the source bytes, the defines, the entry, the target and the flags. Results the game did
// not ask for go into the cache from the main thread ( a little each frame, the rest when the
// renderer goes ). The workers read no file and write no log: the sources and their includes are
// read on the main thread first.
//
// Створено (Ivan Danko, 2026-11-14): пункт 9 етап E пункт 4 ( багатопотоковість ) - збирання
// шейдерів наперед на всіх ядрах. Наша власна розробка; донор ( Monolith ) збирає шейдер лише
// тоді, коли його запитує гра, по одному.
//
// Під час запуску рендера ( CRender::create, r4.cpp ) кожен запит шейдера, який гра може зробити
// з поточними налаштуваннями, визначає сам shader_compile: кожен .vs, .ps і .gs теки шейдерів
// ( разом з аддонами ), вершинний шейдер без скінінгу та зі скінінгом 0..4, як їх запитує
// менеджер ресурсів. Список береться з власних файлів гри та поточних налаштувань, тому він
// однаковий для кожного гравця, будь-яких налаштувань і будь-якого заліза. Запит, файл кешу якого
// чинний, пропускається, тож запуск із повним кешем нічого не збирає. Решта препроцесується й
// збирається на фонових робочих потоках системи завдань ( xrCore\xrTasks.h ), поки гра далі
// завантажується. Коли гра запитує шейдер, shader_compile бере готовий код, чекає на шейдер, який
// робочий потік збирає саме зараз, або, як і раніше, сам збирає шейдер, якого ще не почав жоден
// потік. Результат береться лише тоді, коли всі вхідні дані збирання ті самі: байти джерела,
// макроси, точка входу, ціль і прапорці. Результати, яких гра не запитала, записуються в кеш з
// головного потоку ( потроху щокадру, решта - коли рендер закривається ). Робочі потоки не
// читають файлів і не пишуть у журнал: джерела та їхні включені файли спершу читаються на
// головному потоці.
//
// Include after stdafx.h ( the D3D compiler types, xr_string, xr_vector, xr_map, xr_set ).
// Підключати після stdafx.h ( типи компілятора D3D, xr_string, xr_vector, xr_map, xr_set ).

#include <atomic>

namespace shader_prebuild {
// The compiler and the text hash of r4.cpp ( shader_compiler, shader_preprocessor,
// shader_text_hash_of ) and the compiler's id: set up on the main thread, then only called.
// Компілятор і хеш тексту з r4.cpp ( shader_compiler, shader_preprocessor, shader_text_hash_of )
// та ідентифікатор компілятора: налаштовуються на головному потоці, далі лише викликаються.
typedef HRESULT(WINAPI* preprocess_t)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*,
                                      ID3DInclude*, ID3DBlob**, ID3DBlob**);
typedef u64 (*text_hash_t)(const void* text, size_t size, LPCSTR entry, LPCSTR target, DWORD flags,
                           u64 compiler);

struct tools {
    pD3DCompile compile = nullptr;
    preprocess_t preprocess = nullptr;
    text_hash_t text_hash = nullptr;
    u64 compiler = 0;
};

// One define of a request; null_value: a define without a value.
// Один макрос запиту; null_value: макрос без значення.
struct define {
    xr_string name, value;
    bool null_value = false;
};

// A request: queued -> running ( a worker builds it ) -> done, or queued -> main ( the game asked
// first and builds it itself ). What it gave: its code ( code ), its code is in the cache already
// ( cached ), the compiler refused it ( failed ), nothing ( none: not built ).
// Запит: queued -> running ( його збирає робочий потік ) -> done, або queued -> main ( гра
// запитала раніше й збирає його сама ). Що він дав: свій код ( code ), його код уже є в кеші
// ( cached ), компілятор його відхилив ( failed ), нічого ( none: не зібрано ).
enum : u32 { state_queued, state_running, state_done, state_main };
enum : u32 { kind_none, kind_code, kind_cached, kind_failed };

struct job;

struct variant {
    xr_string request; // the name the game asks for ( the source's name, _0 .. _4 for skinning )
    xr_string link;    // its cache file ( the link of stage E point 3 )
    xr_string key;     // the settings key ( sh_name of shader_compile )
    xr_vector<define> defines;
    xr_string entry, target;
    DWORD flags = 0;

    job* owner = nullptr;
    u32 index = 0;                          // in owner->variants
    std::atomic<u32> state{ state_queued }; // see above
    u32 kind = kind_none;                   // set before state_done
    u64 hash = 0;                           // the text hash ( code, cached )
    u32 code = 0;                           // code: its index in owner->codes
    bool used = false;                      // main thread: taken by the game or written
};

// One source with its requests: one task of the worker threads.
// Одне джерело з його запитами: одне завдання робочих потоків.
struct job {
    xr_string ext;    // vs, ps or gs
    xr_string source; // the source bytes, as the resource manager reads them
    u32 source_crc = 0;
    xr_vector<variant*> variants;
    xr_vector<xr_string> codes; // the compiled code, by variant index
};

struct result {
    u64 hash = 0;
    const void* code = nullptr; // null: the code is in the cache under hash
    u32 size = 0;
};

// All of them on the main thread only.
// Усі - лише на головному потоці.

// Takes the jobs ( the three containers are left empty ) and hands them to the worker threads.
// Забирає завдання ( три контейнери лишаються порожніми ) та передає їх робочим потокам.
void start(xr_vector<job*>& jobs, xr_map<xr_string, xr_string>& includes,
           xr_set<xr_string>& contents, const tools& with);

// The result for a request of shader_compile when all its inputs are those of a queued request:
// the code, or the text hash when the code is in the cache already. A request no worker has
// started is left to the caller ( false ); one a worker is building is waited for.
// Результат для запиту shader_compile, коли всі його вхідні дані такі самі, як у запиту з черги:
// код або хеш тексту, коли код уже є в кеші. Запит, якого ще не почав жоден потік, лишається
// тому, хто викликає ( false ); на запит, який збирає робочий потік, чекають.
bool take(LPCSTR ext, LPCSTR key, LPCSTR request, const void* source, u32 size,
          const D3D_SHADER_MACRO* defines, LPCSTR entry, LPCSTR target, DWORD flags, result& out);

// The next result the game has not taken, in queue order; false when there is none yet.
// Наступний результат, якого гра не взяла, у порядку черги; false, коли його ще немає.
bool next_extra(const job*& j, const variant*& v);

// Started and not ended.
// Запущено й не завершено.
bool running();
// Every request settled and every result handed out.
// Усі запити завершено, усі результати видано.
bool finished();
// No request starts any more; returns when the workers are done.
// Жоден запит більше не починається; повертається, коли робочі потоки закінчили.
void cancel();
// cancel, the summary kept, everything freed.
// cancel, підсумок зберігається, усе звільняється.
void end();
// The start line into the log, kept for print_stats.
// Рядок запуску в журнал, зберігається для print_stats.
void note(LPCSTR line);
// The result lines into the log.
// Рядки результату в журнал.
void print_result();
// r__shader_build_stats: the start line and the result.
// r__shader_build_stats: рядок запуску та результат.
void print_stats();
} // namespace shader_prebuild
