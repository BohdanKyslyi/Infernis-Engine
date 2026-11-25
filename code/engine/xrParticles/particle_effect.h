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

    // Written by Ivan Danko, 2026-11-17: FPS plan step 2 - the float colour of every particle for the
    // smooth fade of Anomaly (Monolith keeps colorR / G / B / A floats in Particle; ours keeps them
    // here, so Particle stays 64 bytes for the other renderers). Made only when the Anomaly behaviour
    // is on (psystem.h); nullptr = the colour works as it was.
    // Створено (Ivan Danko, 2026-11-17): крок 2 плану FPS - колір кожної частинки у float для
    // плавного згасання Anomaly (Monolith тримає colorR / G / B / A у Particle; ми - тут, тож Particle
    // лишається 64 байти для інших рендерів). Створюється лише з увімкненою поведінкою Anomaly
    // (psystem.h); nullptr = колір працює, як раніше.
    Fcolor* colors_f = nullptr;

public:
    explicit ParticleEffect(int mp) {
        max_particles = mp;
        particles_allocated = max_particles;

        real_ptr = xr_malloc(sizeof(Particle) * max_particles + 63);
        
        particles = reinterpret_cast<Particle*>(
            (reinterpret_cast<std::uintptr_t>(real_ptr) + 63) & ~std::uintptr_t(63)
        );

        // Written by Ivan Danko, 2026-11-17: FPS plan step 2 - the float colours (see colors_f).
        // Створено (Ivan Danko, 2026-11-17): крок 2 плану FPS - кольори у float (див. colors_f).
        if (AnomalyMode())
            colors_f = static_cast<Fcolor*>(xr_malloc(sizeof(Fcolor) * (max_particles ? max_particles : 1)));
    }

    ~ParticleEffect() { 
        xr_free(real_ptr); 
        // Written by Ivan Danko, 2026-11-17: FPS plan step 2 - the float colours (see colors_f).
        // Створено (Ivan Danko, 2026-11-17): крок 2 плану FPS - кольори у float (див. colors_f).
        xr_free(colors_f);
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

        // Written by Ivan Danko, 2026-11-17: FPS plan step 2 - the float colours grow with the
        // particles; no memory for them = the colour works as it was (colors_f nullptr).
        // Створено (Ivan Danko, 2026-11-17): крок 2 плану FPS - кольори у float ростуть разом із
        // частинками; немає пам'яті для них = колір працює, як раніше (colors_f nullptr).
        if (colors_f) {
            Fcolor* new_colors = static_cast<Fcolor*>(xr_malloc(sizeof(Fcolor) * max_count));
            if (new_colors && p_count > 0)
                std::memcpy(new_colors, colors_f, p_count * sizeof(Fcolor));
            xr_free(colors_f);
            colors_f = new_colors;
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
        // Written by Ivan Danko, 2026-11-17: FPS plan step 2 - the float colour follows its particle.
        // Створено (Ivan Danko, 2026-11-17): крок 2 плану FPS - колір у float іде за своєю частинкою.
        if (colors_f)
            colors_f[i] = colors_f[p_count];
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

        // Written by Ivan Danko, 2026-11-17: FPS plan step 2 - the birth colour as floats, the same
        // conversion as Monolith's Add (1/255 per channel).
        // Створено (Ivan Danko, 2026-11-17): крок 2 плану FPS - колір народження у float, те саме
        // перетворення, що в Add у Monolith (1/255 на канал).
        if (colors_f)
            colors_f[p_count].set(P.color);
            
        p_count++;
        return TRUE;
    }
};

}; // namespace PAPI
//---------------------------------------------------------------------------
#endif