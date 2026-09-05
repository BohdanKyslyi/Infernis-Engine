#include "stdafx.h"
#pragma hdrstop

#include "particle_actions_collection.h"
#include "particle_effect.h"
#include <cmath>
#include <random>
#include <vector>

using namespace PAPI;

//-------------------------------------------------------------------------------------------------

void PAPI::PAAvoid::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    float magdt = magnitude * dt;

    switch (position.type) {
    case PDPlane: {
        if (look_ahead < P_MAXFLOAT) {
            for (u32 i = 0; i < effect->p_count; ++i) {
                Particle& m = effect->particles[i];

                float dist = m.pos * position.p2 + position.radius1;

                if (dist < look_ahead) {
                    float vm = m.vel.length();
                    pVector Vn = m.vel / vm;

                    pVector tmp = (position.p2 * (magdt / (dist * dist + epsilon))) + Vn;
                    m.vel = tmp * (vm / tmp.length());
                }
            }
        } else {
            for (u32 i = 0; i < effect->p_count; ++i) {
                Particle& m = effect->particles[i];

                float dist = m.pos * position.p2 + position.radius1;

                float vm = m.vel.length();
                pVector Vn = m.vel / vm;

                pVector tmp = (position.p2 * (magdt / (dist * dist + epsilon))) + Vn;
                m.vel = tmp * (vm / tmp.length());
            }
        }
    } break;
    case PDRectangle: {
        pVector& u = position.u;
        pVector& v = position.v;

        pVector un = u / position.radius1Sqr;
        pVector vn = v / position.radius2Sqr;

        float wx = u.y * v.z - u.z * v.y;
        float wy = u.z * v.x - u.x * v.z;
        float wz = u.x * v.y - u.y * v.x;

        float det = 1.0f / (wz * u.x * v.y - wz * u.y * v.x - u.z * wx * v.y - u.x * v.z * wy +
                            v.z * wx * u.y + u.z * v.x * wy);

        pVector s1((v.y * wz - v.z * wy), (v.z * wx - v.x * wz), (v.x * wy - v.y * wx));
        s1 *= det;
        pVector s2((u.y * wz - u.z * wy), (u.z * wx - u.x * wz), (u.x * wy - u.y * wx));
        s2 *= -det;

        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];
            pVector pnext(m.pos + m.vel * dt * look_ahead);

            float distold = m.pos * position.p2 + position.radius1;
            float distnew = pnext * position.p2 + position.radius1;

            if (distold * distnew >= 0.0f)
                continue;

            float nv = position.p2 * m.vel;
            float t = -distold / nv;

            pVector phit(m.pos + m.vel * t);
            pVector offset(phit - position.p1);

            float upos = offset * s1;
            float vpos = offset * s2;

            if (upos < 0.0f || vpos < 0.0f || upos > 1.0f || vpos > 1.0f)
                continue;

            pVector uofs = (un * (un * offset)) - offset;
            float udistSqr = uofs.length2();
            pVector vofs = (vn * (vn * offset)) - offset;
            float vdistSqr = vofs.length2();

            pVector foffset((u + v) - offset);
            pVector fofs = (un * (un * foffset)) - foffset;
            float fdistSqr = fofs.length2();
            pVector gofs = (un * (un * foffset)) - foffset;
            float gdistSqr = gofs.length2();

            pVector S;
            if (udistSqr <= vdistSqr && udistSqr <= fdistSqr && udistSqr <= gdistSqr)
                S = uofs;
            else if (vdistSqr <= fdistSqr && vdistSqr <= gdistSqr)
                S = vofs;
            else if (fdistSqr <= gdistSqr)
                S = fofs;
            else
                S = gofs;

            S.normalize_safe();

            float vm = m.vel.length();
            pVector Vn = m.vel / vm;

            pVector tmp = (S * (magdt / (t * t + epsilon))) + Vn;
            m.vel = tmp * (vm / tmp.length());
        }
    } break;
    case PDTriangle: {
        pVector& u = position.u;
        pVector& v = position.v;

        pVector un = u / position.radius1Sqr;
        pVector vn = v / position.radius2Sqr;

        pVector f = v - u;
        pVector fn(f);
        fn.normalize_safe();

        float wx = u.y * v.z - u.z * v.y;
        float wy = u.z * v.x - u.x * v.z;
        float wz = u.x * v.y - u.y * v.x;

        float det = 1.0f / (wz * u.x * v.y - wz * u.y * v.x - u.z * wx * v.y - u.x * v.z * wy +
                            v.z * wx * u.y + u.z * v.x * wy);

        pVector s1((v.y * wz - v.z * wy), (v.z * wx - v.x * wz), (v.x * wy - v.y * wx));
        s1 *= det;
        pVector s2((u.y * wz - u.z * wy), (u.z * wx - u.x * wz), (u.x * wy - u.y * wx));
        s2 *= -det;

        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];
            pVector pnext(m.pos + m.vel * dt * look_ahead);

            float distold = m.pos * position.p2 + position.radius1;
            float distnew = pnext * position.p2 + position.radius1;

            if (distold * distnew >= 0.0f)
                continue;

            float nv = position.p2 * m.vel;
            float t = -distold / nv;

            pVector phit(m.pos + m.vel * t);
            pVector offset(phit - position.p1);

            float upos = offset * s1;
            float vpos = offset * s2;

            if (upos < 0.0f || vpos < 0.0f || (upos + vpos) > 1.0f)
                continue;

            pVector uofs = (un * (un * offset)) - offset;
            float udistSqr = uofs.length2();
            pVector vofs = (vn * (vn * offset)) - offset;
            float vdistSqr = vofs.length2();
            pVector foffset(offset - u);
            pVector fofs = (fn * (fn * foffset)) - foffset;
            float fdistSqr = fofs.length2();
            
            pVector S;
            if (udistSqr <= vdistSqr && udistSqr <= fdistSqr)
                S = uofs;
            else if (vdistSqr <= fdistSqr)
                S = vofs;
            else
                S = fofs;

            S.normalize_safe();

            float vm = m.vel.length();
            pVector Vn = m.vel / vm;

            pVector tmp = (S * (magdt / (t * t + epsilon))) + Vn;
            m.vel = tmp * (vm / tmp.length());
        }
    } break;
    case PDDisc: {
        float r1Sqr = xr::sqr(position.radius1);
        float r2Sqr = xr::sqr(position.radius2);

        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];
            pVector pnext(m.pos + m.vel * dt * look_ahead);

            float distold = m.pos * position.p2 + position.radius1Sqr;
            float distnew = pnext * position.p2 + position.radius1Sqr;

            if (distold * distnew >= 0.0f)
                continue;

            float nv = position.p2 * m.vel;
            float t = -distold / nv;

            pVector phit(m.pos + m.vel * t);
            pVector offset(phit - position.p1);

            float rad = offset.length2();

            if (rad > r1Sqr || rad < r2Sqr)
                continue;

            pVector S = offset;
            S.normalize_safe();

            float vm = m.vel.length();
            pVector Vn = m.vel / vm;

            pVector tmp = (S * (magdt / (t * t + epsilon))) + Vn;
            m.vel = tmp * (vm / tmp.length());
        }
    } break;
    case PDSphere: {
        float rSqr = position.radius1 * position.radius1;

        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];

            float vm = m.vel.length();
            pVector Vn = m.vel / vm;

            pVector L = position.p1 - m.pos;
            float v = L * Vn;

            float disc = rSqr - (L * L) + v * v;
            if (disc < 0.0f)
                continue; 

            float t = v - std::sqrt(disc);
            if (t < 0.0f || t > (vm * look_ahead))
                continue;

            pVector C = Vn ^ L;
            C.normalize_safe();
            pVector S = Vn ^ C;

            pVector tmp = (S * (magdt / (t * t + epsilon))) + Vn;
            m.vel = tmp * (vm / tmp.length());
        }
    } break;
    }
}
void PAPI::PAAvoid::Transform(const Fmatrix& m) { position.transform(positionL, m); }
//-------------------------------------------------------------------------------------------------

void PABounce::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    switch (position.type) {
    case PDTriangle: {
        pVector& u = position.u;
        pVector& v = position.v;

        float wx = u.y * v.z - u.z * v.y;
        float wy = u.z * v.x - u.x * v.z;
        float wz = u.x * v.y - u.y * v.x;

        float det = 1.0f / (wz * u.x * v.y - wz * u.y * v.x - u.z * wx * v.y - u.x * v.z * wy +
                            v.z * wx * u.y + u.z * v.x * wy);

        pVector s1((v.y * wz - v.z * wy), (v.z * wx - v.x * wz), (v.x * wy - v.y * wx));
        s1 *= det;
        pVector s2((u.y * wz - u.z * wy), (u.z * wx - u.x * wz), (u.x * wy - u.y * wx));
        s2 *= -det;

        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];
            pVector pnext(m.pos + m.vel * dt);

            float distold = m.pos * position.p2 + position.radius1;
            float distnew = pnext * position.p2 + position.radius1;

            if (distold * distnew >= 0.0f)
                continue;

            float nv = position.p2 * m.vel;
            float t = -distold / nv;

            pVector phit(m.pos + m.vel * t);
            pVector offset(phit - position.p1);

            float upos = offset * s1;
            float vpos = offset * s2;

            if (upos < 0.0f || vpos < 0.0f || (upos + vpos) > 1.0f)
                continue;

            pVector vn(position.p2 * nv);
            pVector vt(m.vel - vn);       

            if (vt.length2() <= cutoffSqr)
                m.vel = vt - vn * resilience;
            else
                m.vel = vt * oneMinusFriction - vn * resilience;
        }
    } break;
    case PDDisc: {
        float r1Sqr = xr::sqr(position.radius1);
        float r2Sqr = xr::sqr(position.radius2);

        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];
            pVector pnext(m.pos + m.vel * dt);

            float distold = m.pos * position.p2 + position.radius1Sqr;
            float distnew = pnext * position.p2 + position.radius1Sqr;

            if (distold * distnew >= 0.0f)
                continue;

            float nv = position.p2 * m.vel;
            float t = -distold / nv;

            pVector phit(m.pos + m.vel * t);
            pVector offset(phit - position.p1);

            float rad = offset.length2();

            if (rad > r1Sqr || rad < r2Sqr)
                continue;

            pVector vn(position.p2 * nv); 
            pVector vt(m.vel - vn);       

            if (vt.length2() <= cutoffSqr)
                m.vel = vt - vn * resilience;
            else
                m.vel = vt * oneMinusFriction - vn * resilience;
        }
    } break;
    case PDPlane: {
        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];
            pVector pnext(m.pos + m.vel * dt);

            float distold = m.pos * position.p2 + position.radius1;
            float distnew = pnext * position.p2 + position.radius1;

            if (distold * distnew >= 0.0f)
                continue;

            float nmag = m.vel * position.p2;
            pVector vn(position.p2 * nmag); 
            pVector vt(m.vel - vn);         

            if (vt.length2() <= cutoffSqr)
                m.vel = vt - vn * resilience;
            else
                m.vel = vt * oneMinusFriction - vn * resilience;
        }
    } break;
    case PDRectangle: {
        pVector& u = position.u;
        pVector& v = position.v;

        float wx = u.y * v.z - u.z * v.y;
        float wy = u.z * v.x - u.x * v.z;
        float wz = u.x * v.y - u.y * v.x;

        float det = 1.0f / (wz * u.x * v.y - wz * u.y * v.x - u.z * wx * v.y - u.x * v.z * wy +
                            v.z * wx * u.y + u.z * v.x * wy);

        pVector s1((v.y * wz - v.z * wy), (v.z * wx - v.x * wz), (v.x * wy - v.y * wx));
        s1 *= det;
        pVector s2((u.y * wz - u.z * wy), (u.z * wx - u.x * wz), (u.x * wy - u.y * wx));
        s2 *= -det;

        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];
            pVector pnext(m.pos + m.vel * dt);

            float distold = m.pos * position.p2 + position.radius1;
            float distnew = pnext * position.p2 + position.radius1;

            if (distold * distnew >= 0.0f)
                continue;

            float t = -distold / (position.p2 * m.vel);

            pVector phit(m.pos + m.vel * t);
            pVector offset(phit - position.p1);

            float upos = offset * s1;
            float vpos = offset * s2;

            if (upos < 0.0f || upos > 1.0f || vpos < 0.0f || vpos > 1.0f)
                continue;

            float nmag = m.vel * position.p2;
            pVector vn(position.p2 * nmag); 
            pVector vt(m.vel - vn);         

            if (vt.length2() <= cutoffSqr)
                m.vel = vt - vn * resilience;
            else
                m.vel = vt * oneMinusFriction - vn * resilience;
        }
    } break;
    case PDSphere: {
        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];
            pVector pnext(m.pos + m.vel * dt);

            if (position.Within(pnext)) {
                BOOL pinside = position.Within(m.pos);

                pVector n(m.pos - position.p1);
                n.normalize_safe();

                float nmag = m.vel * n;

                pVector vn(n * nmag);    
                pVector vt = m.vel - vn; 

                if (pinside) {
                    if (nmag < 0.0f)
                        m.vel = vt - vn;
                } else {
                    if (vt.length2() <= cutoffSqr)
                        m.vel = vt - vn * resilience;
                    else
                        m.vel = vt * oneMinusFriction - vn * resilience;
                }
            }
        }
    }
    }
}
void PABounce::Transform(const Fmatrix& m) { position.transform(positionL, m); }
//-------------------------------------------------------------------------------------------------

void PACopyVertexB::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    if (copy_pos) {
        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];
            m.posB = m.pos;
        }
    }
}
void PACopyVertexB::Transform(const Fmatrix&) { ; }
//-------------------------------------------------------------------------------------------------

void PADamping::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    pVector one(1.0f, 1.0f, 1.0f);
    pVector scale(one - ((one - damping) * dt));

    for (u32 i = 0; i < effect->p_count; ++i) {
        Particle& m = effect->particles[i];
        float vSqr = m.vel.length2();

        if (vSqr >= vlowSqr && vSqr <= vhighSqr) {
            m.vel.x *= scale.x;
            m.vel.y *= scale.y;
            m.vel.z *= scale.z;
        }
    }
}
void PADamping::Transform(const Fmatrix&) { ; }
//-------------------------------------------------------------------------------------------------

void PAExplosion::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    float radius = velocity * age;
    float magdt = magnitude * dt;
    float oneOverSigma = 1.0f / stdev;
    float inexp = -0.5f * xr::sqr(oneOverSigma);
    float outexp = ONEOVERSQRT2PI * oneOverSigma;

    for (u32 i = 0; i < effect->p_count; ++i) {
        Particle& m = effect->particles[i];

        pVector dir(m.pos - center);
        float distSqr = dir.length2();
        float dist = std::sqrt(distSqr);
        float DistFromWaveSqr = xr::sqr(radius - dist);

        float Gd = std::exp(DistFromWaveSqr * inexp) * outexp;

        m.vel += dir * (Gd * magdt / ((dist + EPS) * (distSqr + epsilon)));
    }

    age += dt;
}
void PAExplosion::Transform(const Fmatrix& m) { m.transform_tiny(center, centerL); }
//-------------------------------------------------------------------------------------------------

void PAFollow::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    float magdt = magnitude * dt;
    float max_radiusSqr = max_radius * max_radius;

    if (max_radiusSqr < P_MAXFLOAT) {
        for (u32 i = 0; i < effect->p_count - 1; ++i) {
            Particle& m = effect->particles[i];
            pVector tohim(effect->particles[i + 1].pos - m.pos); 
            float tohimlenSqr = tohim.length2();

            if (tohimlenSqr < max_radiusSqr) {
                m.vel += tohim * (magdt / (std::sqrt(tohimlenSqr) * (tohimlenSqr + epsilon)));
            }
        }
    } else {
        for (u32 i = 0; i < effect->p_count - 1; ++i) {
            Particle& m = effect->particles[i];
            pVector tohim(effect->particles[i + 1].pos - m.pos); 
            float tohimlenSqr = tohim.length2();

            m.vel += tohim * (magdt / (std::sqrt(tohimlenSqr) * (tohimlenSqr + epsilon)));
        }
    }
}
void PAFollow::Transform(const Fmatrix&) { ; }
//-------------------------------------------------------------------------------------------------

void PAGravitate::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    float magdt = magnitude * dt;
    float max_radiusSqr = max_radius * max_radius;

    if (max_radiusSqr < P_MAXFLOAT) {
        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];

            for (u32 j = i + 1; j < effect->p_count; ++j) {
                Particle& mj = effect->particles[j];

                pVector tohim(mj.pos - m.pos); 
                float tohimlenSqr = tohim.length2() + EPS_S;

                if (tohimlenSqr < max_radiusSqr) {
                    pVector acc(tohim * (magdt / (std::sqrt(tohimlenSqr) * (tohimlenSqr + epsilon))));

                    m.vel += acc;
                    mj.vel -= acc;
                }
            }
        }
    } else {
        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];

            for (u32 j = i + 1; j < effect->p_count; ++j) {
                Particle& mj = effect->particles[j];

                pVector tohim(mj.pos - m.pos); 
                float tohimlenSqr = tohim.length2() + EPS_S;

                pVector acc(tohim * (magdt / (std::sqrt(tohimlenSqr) * (tohimlenSqr + epsilon))));

                m.vel += acc;
                mj.vel -= acc;
            }
        }
    }
}
void PAGravitate::Transform(const Fmatrix&) { ; }
//-------------------------------------------------------------------------------------------------

void PAGravity::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    pVector ddir(direction * dt);

    for (u32 i = 0; i < effect->p_count; ++i) {
        effect->particles[i].vel += ddir;
    }
}
void PAGravity::Transform(const Fmatrix&) { ; }
//-------------------------------------------------------------------------------------------------

void PAJet::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    float magdt = magnitude * dt;
    float max_radiusSqr = max_radius * max_radius;

    if (max_radiusSqr < P_MAXFLOAT) {
        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];
            pVector dir(m.pos - center);
            float rSqr = dir.length2();

            if (rSqr < max_radiusSqr) {
                pVector accel;
                acc.Generate(accel);
                m.vel += accel * (magdt / (rSqr + epsilon));
            }
        }
    } else {
        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];
            pVector dir(m.pos - center);
            float rSqr = dir.length2();

            pVector accel;
            acc.Generate(accel);
            m.vel += accel * (magdt / (rSqr + epsilon));
        }
    }
}
void PAJet::Transform(const Fmatrix& m) {
    m.transform_tiny(center, centerL);
    acc.transform_dir(accL, m);
}
//-------------------------------------------------------------------------------------------------

void PAScatter::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    float magdt = magnitude * dt;
    float max_radiusSqr = max_radius * max_radius;

    if (max_radiusSqr < P_MAXFLOAT) {
        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];
            pVector dir(m.pos - center);
            float rSqr = dir.length2();

            if (rSqr < max_radiusSqr) {
                pVector accel = dir / std::sqrt(rSqr);
                m.vel += accel * (magdt / (rSqr + epsilon));
            }
        }
    } else {
        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];
            pVector dir(m.pos - center);
            float rSqr = dir.length2();

            pVector accel = dir / std::sqrt(rSqr);
            m.vel += accel * (magdt / (rSqr + epsilon));
        }
    }
}
void PAScatter::Transform(const Fmatrix& m) { m.transform_tiny(center, centerL); }
//-------------------------------------------------------------------------------------------------

void PAKillOld::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    tm_max = age_limit;
    for (int i = effect->p_count - 1; i >= 0; --i) {
        Particle& m = effect->particles[i];

        if (!((m.age < age_limit) ^ kill_less_than))
            effect->Remove(i);
    }
}
void PAKillOld::Transform(const Fmatrix&) {}
//-------------------------------------------------------------------------------------------------

void PAMatchVelocity::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    float magdt = magnitude * dt;
    float max_radiusSqr = max_radius * max_radius;

    if (max_radiusSqr < P_MAXFLOAT) {
        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];

            for (u32 j = i + 1; j < effect->p_count; ++j) {
                Particle& mj = effect->particles[j];
                pVector tohim(mj.pos - m.pos); 
                float tohimlenSqr = tohim.length2();

                if (tohimlenSqr < max_radiusSqr) {
                    pVector acc(mj.vel * (magdt / (tohimlenSqr + epsilon)));
                    m.vel += acc;
                    mj.vel -= acc;
                }
            }
        }
    } else {
        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];

            for (u32 j = i + 1; j < effect->p_count; ++j) {
                Particle& mj = effect->particles[j];
                pVector tohim(mj.pos - m.pos); 
                float tohimlenSqr = tohim.length2();

                pVector acc(mj.vel * (magdt / (tohimlenSqr + epsilon)));
                m.vel += acc;
                mj.vel -= acc;
            }
        }
    }
}
void PAMatchVelocity::Transform(const Fmatrix&) { ; }
//-------------------------------------------------------------------------------------------------

void PAMove::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    for (u32 i = 0; i < effect->p_count; ++i) {
        Particle& m = effect->particles[i];
        m.age += dt;
        m.posB = m.pos;
        m.pos += m.vel * dt;
    }
}
void PAMove::Transform(const Fmatrix&) { ; }
//-------------------------------------------------------------------------------------------------

void PAOrbitLine::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    float magdt = magnitude * dt;
    float max_radiusSqr = max_radius * max_radius;

    if (max_radiusSqr < P_MAXFLOAT) {
        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];
            pVector f(m.pos - p);
            pVector w(axis * (f * axis));
            pVector into = w - f;
            float rSqr = into.length2();

            if (rSqr < max_radiusSqr)
                m.vel += into * (magdt / (std::sqrt(rSqr) + (rSqr + epsilon)));
        }
    } else {
        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];
            pVector f(m.pos - p);
            pVector w(axis * (f * axis));
            pVector into = w - f;
            float rSqr = into.length2();

            m.vel += into * (magdt / (std::sqrt(rSqr) + (rSqr + epsilon)));
        }
    }
}
void PAOrbitLine::Transform(const Fmatrix& m) {
    m.transform_tiny(p, pL);
    m.transform_dir(axis, axisL);
}
//-------------------------------------------------------------------------------------------------

void PAOrbitPoint::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    float magdt = magnitude * dt;
    float max_radiusSqr = max_radius * max_radius;

    if (max_radiusSqr < P_MAXFLOAT) {
        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];
            pVector dir(center - m.pos);
            float rSqr = dir.length2();

            if (rSqr < max_radiusSqr)
                m.vel += dir * (magdt / (std::sqrt(rSqr) + (rSqr + epsilon)));
        }
    } else {
        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];
            pVector dir(center - m.pos);
            float rSqr = dir.length2();

            m.vel += dir * (magdt / (std::sqrt(rSqr) + (rSqr + epsilon)));
        }
    }
}
void PAOrbitPoint::Transform(const Fmatrix& m) { m.transform_tiny(center, centerL); }
//-------------------------------------------------------------------------------------------------

void PARandomAccel::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    for (u32 i = 0; i < effect->p_count; ++i) {
        Particle& m = effect->particles[i];
        pVector acceleration;
        gen_acc.Generate(acceleration);
        m.vel += acceleration * dt;
    }
}
void PARandomAccel::Transform(const Fmatrix& m) { gen_acc.transform_dir(gen_accL, m); }
//-------------------------------------------------------------------------------------------------

void PARandomDisplace::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    for (u32 i = 0; i < effect->p_count; ++i) {
        Particle& m = effect->particles[i];
        pVector displacement;
        gen_disp.Generate(displacement);
        m.pos += displacement * dt;
    }
}
void PARandomDisplace::Transform(const Fmatrix& m) { gen_disp.transform_dir(gen_dispL, m); }
//-------------------------------------------------------------------------------------------------

void PARandomVelocity::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    for (u32 i = 0; i < effect->p_count; ++i) {
        Particle& m = effect->particles[i];
        pVector velocity;
        gen_vel.Generate(velocity);
        m.vel = velocity;
    }
}
void PARandomVelocity::Transform(const Fmatrix& m) { gen_vel.transform_dir(gen_velL, m); }
//-------------------------------------------------------------------------------------------------

void PARestore::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    if (time_left <= 0.0f) {
        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];
            m.pos = m.posB;
            m.vel = pVector(0, 0, 0);
        }
    } else {
        float t = time_left;
        float dtSqr = dt * dt;
        float tSqrInv2dt = dt * 2.0f / (t * t);
        float tCubInv3dtSqr = dtSqr * 3.0f / (t * t * t);

        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];

            float b = (-2.0f * t * m.vel.x + 3.0f * m.posB.x - 3.0f * m.pos.x) * tSqrInv2dt;
            float a = (t * m.vel.x - m.posB.x - m.posB.x + m.pos.x + m.pos.x) * tCubInv3dtSqr;
            m.vel.x += a + b;

            b = (-2.0f * t * m.vel.y + 3.0f * m.posB.y - 3.0f * m.pos.y) * tSqrInv2dt;
            a = (t * m.vel.y - m.posB.y - m.posB.y + m.pos.y + m.pos.y) * tCubInv3dtSqr;
            m.vel.y += a + b;

            b = (-2.0f * t * m.vel.z + 3.0f * m.posB.z - 3.0f * m.pos.z) * tSqrInv2dt;
            a = (t * m.vel.z - m.posB.z - m.posB.z + m.pos.z + m.pos.z) * tCubInv3dtSqr;
            m.vel.z += a + b;
        }
    }

    time_left -= dt;
}
void PARestore::Transform(const Fmatrix&) { ; }
//-------------------------------------------------------------------------------------------------

void PASink::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    for (int i = effect->p_count - 1; i >= 0; --i) {
        Particle& m = effect->particles[i];

        if (!(position.Within(m.pos) ^ kill_inside))
            effect->Remove(i);
    }
}
void PASink::Transform(const Fmatrix& m) { position.transform(positionL, m); }
//-------------------------------------------------------------------------------------------------

void PASinkVelocity::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    for (int i = effect->p_count - 1; i >= 0; --i) {
        Particle& m = effect->particles[i];

        if (!(velocity.Within(m.vel) ^ kill_inside))
            effect->Remove(i);
    }
}
void PASinkVelocity::Transform(const Fmatrix& m) { velocity.transform_dir(velocityL, m); }

void PASource::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    if (m_Flags.is(flSilent))
        return;

    int rate = static_cast<int>(std::floor(particle_rate * dt));

    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    if (dist(rng) < particle_rate * dt - static_cast<float>(rate))
        rate++;

    if (effect->p_count + rate > effect->max_particles)
        rate = effect->max_particles - effect->p_count;

    pVector pos, posB, vel, col, siz, rt;

    const bool vertexB_tracks = m_Flags.is(u32(flVertexB_tracks));
    const bool singleSize = m_Flags.is(flSingleSize);

    for (int i = 0; i < rate; ++i) {
        position.Generate(pos);
        size.Generate(siz);
        
        if (singleSize)
            siz.set(siz.x, siz.x, siz.x);
            
        rot.Generate(rt);
        velocity.Generate(vel);
        vel += parent_vel;
        color.Generate(col);
        float ag = age + NRand(age_sigma);

        effect->Add(pos, vertexB_tracks ? pos : posB, siz, rt, vel, color_argb_f(alpha, col.x, col.y, col.z), ag);
    }
}
void PASource::Transform(const Fmatrix& m) {
    position.transform(positionL, m);
    velocity.transform_dir(velocityL, m);
}

void PASpeedLimit::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    float min_sqr = min_speed * min_speed;
    float max_sqr = max_speed * max_speed;

    for (u32 i = 0; i < effect->p_count; ++i) {
        Particle& m = effect->particles[i];
        float sSqr = m.vel.length2();
        if (sSqr < min_sqr && sSqr) {
            float s = std::sqrt(sSqr);
            m.vel *= (min_speed / s);
        } else if (sSqr > max_sqr) {
            float s = std::sqrt(sSqr);
            m.vel *= (max_speed / s);
        }
    }
}
void PASpeedLimit::Transform(const Fmatrix&) { ; }

void PATargetColor::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    float scaleFac = scale * dt;
    Fcolor c_p, c_t;

    for (u32 i = 0; i < effect->p_count; ++i) {
        Particle& m = effect->particles[i];
        if (m.age < timeFrom * tm_max || m.age > timeTo * tm_max)
            continue;

        c_p.set(m.color);
        c_t.set(c_p.r + (color.x - c_p.r) * scaleFac, 
                c_p.g + (color.y - c_p.g) * scaleFac,
                c_p.b + (color.z - c_p.b) * scaleFac, 
                c_p.a + (alpha - c_p.a) * scaleFac);
        m.color = c_t.get();
    }
}
void PATargetColor::Transform(const Fmatrix&) { ; }

void PATargetSize::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    float scaleFac_x = scale.x * dt;
    float scaleFac_y = scale.y * dt;
    float scaleFac_z = scale.z * dt;

    for (u32 i = 0; i < effect->p_count; ++i) {
        Particle& m = effect->particles[i];
        pVector dif(size - m.size);
        dif.x *= scaleFac_x;
        dif.y *= scaleFac_y;
        dif.z *= scaleFac_z;
        m.size += dif;
    }
}
void PATargetSize::Transform(const Fmatrix&) { ; }

void PATargetRotate::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    float scaleFac = scale * dt;
    float r = std::abs(rot.x);

    for (u32 i = 0; i < effect->p_count; ++i) {
        Particle& m = effect->particles[i];
        float sign = m.rot.x >= 0.0f ? scaleFac : -scaleFac;
        float dif = (r - std::abs(m.rot.x)) * sign;
        m.rot.x += dif;
    }
}
void PATargetRotate::Transform(const Fmatrix&) { ; }

void PATargetVelocity::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    float scaleFac = scale * dt;

    for (u32 i = 0; i < effect->p_count; ++i) {
        Particle& m = effect->particles[i];
        m.vel += (velocity - m.vel) * scaleFac;
    }
}
void PATargetVelocity::Transform(const Fmatrix& m) { m.transform_dir(velocity, velocityL); }

void PAVortex::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    float magdt = magnitude * dt;
    float max_radiusSqr = max_radius * max_radius;

    if (max_radiusSqr < P_MAXFLOAT) {
        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];
            pVector offset(m.pos - center);
            float rSqr = offset.length2();

            if (rSqr > max_radiusSqr)
                continue;

            float r = std::sqrt(rSqr);
            pVector offnorm(offset / r);

            float axisProj = offnorm * axis; 
            pVector w(axis * axisProj); 
            pVector u(offnorm - w);     
            pVector v(axis ^ u);

            float theta = magdt / (rSqr + epsilon);
            float s = std::sin(theta);
            float c = std::cos(theta);

            offset = (u * c + v * s + w) * r;
            m.pos = offset + center;
        }
    } else {
        for (u32 i = 0; i < effect->p_count; ++i) {
            Particle& m = effect->particles[i];
            pVector offset(m.pos - center);
            float rSqr = offset.length2();
            float r = std::sqrt(rSqr);
            pVector offnorm(offset / r);

            float axisProj = offnorm * axis; 
            pVector w(axis * axisProj); 
            pVector u(offnorm - w);     
            pVector v(axis ^ u);

            float theta = magdt / (rSqr + epsilon);
            float s = std::sin(theta);
            float c = std::cos(theta);

            offset = (u * c + v * s + w) * r;
            m.pos = offset + center;
        }
    }
}
void PAVortex::Transform(const Fmatrix& m) {
    m.transform_tiny(center, centerL);
    m.transform_dir(axis, axisL);
}
//-------------------------------------------------------------------------------------------------

// Turbulence
#include "noise.h"

static int noise_start = 1;
extern void noise3Init();

#ifndef _EDITOR

#include <xmmintrin.h>
#include <emmintrin.h>
#include "../xrCPU_Pipe/ttapi.h"

inline __m128 _mm_load_fvector(const Fvector& v) {
    __m128 R1 = _mm_load_ss(reinterpret_cast<const float*>(&v.x)); 
    __m128 R2 = _mm_load_ss(reinterpret_cast<const float*>(&v.y)); 
    R1 = _mm_unpacklo_ps(R1, R2);   
    R2 = _mm_load_ss(reinterpret_cast<const float*>(&v.z)); 
    R1 = _mm_movelh_ps(R1, R2);     
    return R1;
}

inline void _mm_store_fvector(Fvector& v, const __m128 R1) {
    __m128 R2;
    _mm_store_ss(reinterpret_cast<float*>(&v.x), R1);
    R2 = _mm_unpacklo_ps(R1, R1); 
    R2 = _mm_movehl_ps(R2, R2);   
    _mm_store_ss(reinterpret_cast<float*>(&v.y), R2);
    R2 = _mm_movehl_ps(R1, R1); 
    _mm_store_ss(reinterpret_cast<float*>(&v.z), R2);
}

struct TES_PARAMS {
    u32 p_from;
    u32 p_to;
    ParticleEffect* effect;
    pVector offset;
    float age;
    float epsilon;
    float frequency;
    int octaves;
    float magnitude;
};

void PATurbulenceExecuteStream(LPVOID lpvParams) {
#ifdef _GPA_ENABLED
    TAL_SCOPED_TASK_NAMED("PATurbulenceExecuteStream()");

    TAL_ID rtID = TAL_MakeID(1, Core.dwFrame, 0);
    TAL_AddRelationThis(TAL_RELATION_IS_CHILD_OF, rtID);
#endif // _GPA_ENABLED

    pVector pV;
    pVector vX;
    pVector vY;
    pVector vZ;

    TES_PARAMS* pParams = static_cast<TES_PARAMS*>(lpvParams);

    u32 p_from = pParams->p_from;
    u32 p_to = pParams->p_to;
    ParticleEffect* effect = pParams->effect;
    pVector offset = pParams->offset;
    float age = pParams->age;
    float epsilon = pParams->epsilon;
    float frequency = pParams->frequency;
    int octaves = pParams->octaves;
    float magnitude = pParams->magnitude;

    for (u32 i = p_from; i < p_to; ++i) {
        Particle& m = effect->particles[i];

        pV.mad(m.pos, offset, age);
        vX.set(pV.x + epsilon, pV.y, pV.z);
        vY.set(pV.x, pV.y + epsilon, pV.z);
        vZ.set(pV.x, pV.y, pV.z + epsilon);

        float d = fractalsum3(pV, frequency, octaves);

        pVector D;
        D.x = fractalsum3(vX, frequency, octaves);
        D.y = fractalsum3(vY, frequency, octaves);
        D.z = fractalsum3(vZ, frequency, octaves);

        __m128 _D = _mm_load_fvector(D);
        __m128 _d = _mm_set1_ps(d);
        __m128 _magnitude = _mm_set1_ps(magnitude);
        __m128 _mvel = _mm_load_fvector(m.vel);
        _D = _mm_sub_ps(_D, _d);
        _D = _mm_mul_ps(_D, _magnitude);

        __m128 _vmo = _mm_mul_ps(_mvel, _mvel);  
        __m128 _tmp = _mm_movehl_ps(_vmo, _vmo); 
        _vmo = _mm_add_ss(_vmo, _tmp);           
        _tmp = _mm_unpacklo_ps(_vmo, _vmo);      
        _tmp = _mm_movehl_ps(_tmp, _tmp);        
        _vmo = _mm_add_ss(_vmo, _tmp);           
        _vmo = _mm_sqrt_ss(_vmo);                

        _mvel = _mm_add_ps(_mvel, _D);

        __m128 _vmn = _mm_mul_ps(_mvel, _mvel); 
        _tmp = _mm_movehl_ps(_vmn, _vmn);       
        _vmn = _mm_add_ss(_vmn, _tmp);          
        _tmp = _mm_unpacklo_ps(_vmn, _vmn);     
        _tmp = _mm_movehl_ps(_tmp, _tmp);       
        _vmn = _mm_add_ss(_vmn, _tmp);          
        _vmn = _mm_sqrt_ss(_vmn);               

        _vmo = _mm_div_ss(_vmo, _vmn); 

        _vmo = _mm_shuffle_ps(_vmo, _vmo, _MM_SHUFFLE(0, 0, 0, 0)); 
        _mvel = _mm_mul_ps(_mvel, _vmo);

        _mm_store_fvector(m.vel, _mvel);
    }
}

void PATurbulence::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
#ifdef _GPA_ENABLED
    TAL_SCOPED_TASK_NAMED("PATurbulence::Execute()");
#endif // _GPA_ENABLED

    if (noise_start) {
        noise_start = 0;
        noise3Init();
    };

    age += dt;

    u32 p_cnt = effect->p_count;

    if (!p_cnt)
        return;

    size_t nWorkers = ttapi.threads.size();

    if (p_cnt < nWorkers * 20)
        nWorkers = 1;

    std::vector<TES_PARAMS> tesParams(nWorkers);

    u32 nSlice = p_cnt / 128;
    u32 nStep = ((p_cnt - nSlice) / nWorkers);

    for (u32 i = 0; i < nWorkers; ++i) {
        tesParams[i].p_from = i * nStep;
        tesParams[i].p_to = (i == (nWorkers - 1)) ? p_cnt : (tesParams[i].p_from + nStep);

        tesParams[i].effect = effect;
        tesParams[i].offset = offset;
        tesParams[i].age = age;
        tesParams[i].epsilon = epsilon;
        tesParams[i].frequency = frequency;
        tesParams[i].octaves = octaves;
        tesParams[i].magnitude = magnitude;
        
        TES_PARAMS* pParam = &tesParams[i];
        ttapi.threads[i]->addJob([pParam] { PATurbulenceExecuteStream((void*)pParam); });
    }
    ttapi.wait();
}

#else

void PATurbulence::Execute(ParticleEffect* effect, const float dt, float& tm_max) {
    if (noise_start) {
        noise_start = 0;
        noise3Init();
    };

    pVector pV;
    pVector vX;
    pVector vY;
    pVector vZ;
    age += dt;
    for (u32 i = 0; i < effect->p_count; ++i) {
        Particle& m = effect->particles[i];

        pV.mad(m.pos, offset, age);
        vX.set(pV.x + epsilon, pV.y, pV.z);
        vY.set(pV.x, pV.y + epsilon, pV.z);
        vZ.set(pV.x, pV.y, pV.z + epsilon);

        pVector D;
        float d = fractalsum3(pV, frequency, octaves);
        D.x = (fractalsum3(vX, frequency, octaves) - d) * static_cast<float>(magnitude);
        D.y = (fractalsum3(vY, frequency, octaves) - d) * static_cast<float>(magnitude);
        D.z = (fractalsum3(vZ, frequency, octaves) - d) * static_cast<float>(magnitude);

        float velMagOrig = m.vel.magnitude();
        m.vel.add(D);
        float velMagNow = m.vel.magnitude();
        float valMagScale = velMagOrig / velMagNow;
        m.vel.mul(valMagScale);
    }
}
#endif

void PATurbulence::Transform(const Fmatrix& m) {}