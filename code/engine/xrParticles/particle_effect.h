//---------------------------------------------------------------------------
#ifndef particle_effectH
#define particle_effectH
#pragma once

#include <cstring> 
#include <cstdint> 

namespace PAPI {

// A effect of particles - Info and an array of Particles
struct ParticleEffect {
    u32 p_count = 0;                 // Number of particles currently existing.
    u32 max_particles = 0;           // Max particles allowed in effect.
    u32 particles_allocated = 0;     // Actual allocated size.
    Particle* particles = nullptr;   // Actually, num_particles in size
    void* real_ptr = nullptr;        // Base, possible not aligned pointer
    
    OnBirthParticleCB b_cb = nullptr;
    OnDeadParticleCB d_cb = nullptr;
    void* owner = nullptr;
    u32 param = 0;

public:
    explicit ParticleEffect(int mp) {
        max_particles = mp;
        particles_allocated = max_particles;

        real_ptr = xr_malloc(sizeof(Particle) * max_particles + 63);
        
        particles = reinterpret_cast<Particle*>(
            (reinterpret_cast<std::uintptr_t>(real_ptr) + 63) & ~std::uintptr_t(63)
        );
    }

    ~ParticleEffect() { 
        xr_free(real_ptr); 
    }

    IC int Resize(u32 max_count) {
        // Reducing max.
        if (particles_allocated >= max_count) {
            max_particles = max_count;

            // May have to kill particles.
            if (p_count > max_particles)
                p_count = max_particles;

            return max_count;
        }

        // Allocate particles with optimal padding
        void* new_real_ptr = xr_malloc(sizeof(Particle) * max_count + 63);

        if (new_real_ptr == nullptr) {
            // ERROR - Not enough memory. Just give all we've got.
            max_particles = particles_allocated;
            return max_particles;
        }

        // Recalculate aligned pointer for the new buffer
        Particle* new_particles = reinterpret_cast<Particle*>(
            (reinterpret_cast<std::uintptr_t>(new_real_ptr) + 63) & ~std::uintptr_t(63)
        );

        if (p_count > 0) {
            std::memcpy(new_particles, particles, p_count * sizeof(Particle));
        }
        
        xr_free(real_ptr);

        particles = new_particles;
        real_ptr = new_real_ptr;

        max_particles = max_count;
        particles_allocated = max_count;
        return max_count;
    }

    IC void Remove(int i) {
        if (0 == p_count)
            return;
            
        Particle& m = particles[i];
        if (d_cb)
            d_cb(owner, param, m, i);
            
        m = particles[--p_count]; 
    }

    [[nodiscard]] IC BOOL Add(const pVector& pos, const pVector& posB, const pVector& size, const pVector& rot,
                const pVector& vel, u32 color, const float age = 0.0f, u16 frame = 0,
                u16 flags = 0) {
        if (p_count >= max_particles)
            return FALSE;

        Particle& P = particles[p_count];
        P.pos = pos;
        P.posB = posB;
        P.size = size;
        P.rot.x = rot.x;
        P.vel = vel;
        P.color = color;
        P.age = age;
        P.frame = frame;
        P.flags.assign(flags);
        
        if (b_cb)
            b_cb(owner, param, P, p_count);
            
        p_count++;
        return TRUE;
    }
};

}; // namespace PAPI
//---------------------------------------------------------------------------
#endif