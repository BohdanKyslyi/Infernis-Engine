#pragma once

// Item 9 stage B ( multithreading ): what is left of the old pool. Infernis had a small
// third-party pool here ( MIT, one thread with its own queue per CPU ). It is gone; the three
// callers - software skinning ( xrSkin2W_thread.cpp ), particle turbulence
// ( xrParticles\particle_actions_collection.cpp ) and particle geometry
// ( xrRenderCommon\ParticleEffect.cpp ) - keep their code as it was and now run on the X-Ray task
// system ( xrCore\xrTasks.cpp ). Our own code.
//
// The callers read threads.size() once, cut their work into that many pieces, hand piece i to
// threads[i]->addJob() and call wait(). Every piece goes into the calling thread's own group; the
// workers take them, and wait() runs on the calling thread the pieces no worker has taken yet,
// then waits for the rest. threads.size() is the workers in use plus the calling thread, or 1 when
// every task runs on the calling thread ( "cpu_tasks 0" ).

#include <functional>
#include "../xrCore/xrTasks.h"

class ThreadPool {
public:
    // What threads[i] gives: whichever piece it is, it goes into the calling thread's group.
    struct Slot {
        void addJob(std::function<void()> job) { xrTasks::RunInThreadGroup(std::move(job)); }
    };

    struct Slots {
        size_t size() const { return xrTasks::Slots(); }
        Slot* operator[](size_t) { return &slot; }
        Slot slot;
    };

    Slots threads;

    void initialize() {} // the task system starts with xrCore ( Core._initialize )
    void wait() { xrTasks::WaitThreadGroup(); }
};
