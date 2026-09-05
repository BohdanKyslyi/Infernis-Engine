#include "stdafx.h"
#pragma hdrstop

#include "particle_core.h"
#include <random>
#include <cmath>

using namespace PAPI;

// To offset [0 .. 1] vectors to [-.5 .. .5]
static const pVector vHalf(0.5f, 0.5f, 0.5f);
static const float fPI = 3.14159265358979323846f;

static thread_local std::mt19937 rng(std::random_device{}());
static thread_local std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

inline float get_rand() { return dist01(rng); }

ICF pVector RandVec() { 
    return pVector(get_rand(), get_rand(), get_rand()); 
}

// Return a random number with a normal distribution.
float PAPI::NRand(float sigma) {
    if (sigma == 0.0f)
        return 0.0f;

    std::normal_distribution<float> norm_dist(0.0f, sigma);
    return norm_dist(rng);
}

////////////////////////////////////////////////////////////////////////////////
// Stuff for the pDomain.
pDomain::pDomain(PDomainEnum dtype, float a0, float a1, float a2, float a3, float a4, float a5,
                 float a6, float a7, float a8) {
    type = dtype;
    switch (type) {
    case PDPoint:
        p1 = pVector(a0, a1, a2);
        break;
    case PDLine: {
        p1 = pVector(a0, a1, a2);
        pVector tmp(a3, a4, a5);
        p2 = tmp - p1;
    } break;
    case PDBox:
        if (a0 < a3) { p1.x = a0; p2.x = a3; } else { p1.x = a3; p2.x = a0; }
        if (a1 < a4) { p1.y = a1; p2.y = a4; } else { p1.y = a4; p2.y = a1; }
        if (a2 < a5) { p1.z = a2; p2.z = a5; } else { p1.z = a5; p2.z = a2; }
        break;
    case PDTriangle: {
        p1 = pVector(a0, a1, a2);
        pVector tp2 = pVector(a3, a4, a5);
        pVector tp3 = pVector(a6, a7, a8);

        u = tp2 - p1;
        v = tp3 - p1;

        radius1Sqr = u.length();
        pVector tu = u / radius1Sqr;
        radius2Sqr = v.length();
        pVector tv = v / radius2Sqr;

        p2 = tu ^ tv;        
        p2.normalize_safe(); 
        radius1 = -(p1 * p2);
    } break;
    case PDRectangle: {
        p1 = pVector(a0, a1, a2);
        u = pVector(a3, a4, a5);
        v = pVector(a6, a7, a8);

        radius1Sqr = u.length();
        pVector tu = u / radius1Sqr;
        radius2Sqr = v.length();
        pVector tv = v / radius2Sqr;

        p2 = tu ^ tv;        
        p2.normalize_safe(); 
        radius1 = -(p1 * p2);
    } break;
    case PDPlane: {
        p1 = pVector(a0, a1, a2);
        p2 = pVector(a3, a4, a5);
        p2.normalize_safe(); 
        radius1 = -(p1 * p2);
    } break;
    case PDSphere:
        p1 = pVector(a0, a1, a2);
        if (a3 > a4) {
            radius1 = a3;
            radius2 = a4;
        } else {
            radius1 = a4;
            radius2 = a3;
        }
        radius1Sqr = radius1 * radius1;
        radius2Sqr = radius2 * radius2;
        break;
    case PDCone:
    case PDCylinder: {
        p1 = pVector(a0, a1, a2);
        pVector tmp(a3, a4, a5);
        p2 = tmp - p1;

        if (a6 > a7) {
            radius1 = a6;
            radius2 = a7;
        } else {
            radius1 = a7;
            radius2 = a6;
        }
        radius1Sqr = radius1 * radius1;

        pVector n = p2;
        float p2l2 = n.length2(); 
        n.normalize_safe();

        radius2Sqr = (p2l2 > 0.0f) ? 1.0f / p2l2 : 0.0f;

        pVector basis(1.0f, 0.0f, 0.0f);
        if (std::abs(basis * n) > 0.999f)
            basis = pVector(0.0f, 1.0f, 0.0f);

        u = basis - n * (basis * n);
        u.normalize_safe();
        v = n ^ u;
    } break;
    case PDBlob: {
        p1 = pVector(a0, a1, a2);
        radius1 = a3;
        float tmp = 1.0f / radius1;
        radius2Sqr = -0.5f * (tmp * tmp);
        radius2 = ONEOVERSQRT2PI * tmp;
    } break;
    case PDDisc: {
        p1 = pVector(a0, a1, a2); 
        p2 = pVector(a3, a4, a5); 
        p2.normalize_safe();

        if (a6 > a7) {
            radius1 = a6;
            radius2 = a7;
        } else {
            radius1 = a7;
            radius2 = a6;
        }

        pVector basis(1.0f, 0.0f, 0.0f);
        if (std::abs(basis * p2) > 0.999f)
            basis = pVector(0.0f, 1.0f, 0.0f);

        u = basis - p2 * (basis * p2);
        u.normalize_safe();
        v = p2 ^ u;
        radius1Sqr = -(p1 * p2); 
    } break;
    }
}

// Determines if pos is inside the domain
BOOL pDomain::Within(const pVector& pos) const {
    switch (type) {
    case PDBox:
        return !((pos.x < p1.x) || (pos.x > p2.x) || (pos.y < p1.y) || (pos.y > p2.y) ||
                 (pos.z < p1.z) || (pos.z > p2.z));
    case PDPlane:
        return pos * p2 >= -radius1;
    case PDSphere: {
        pVector rvec(pos - p1);
        float rSqr = rvec.length2();
        return rSqr <= radius1Sqr && rSqr >= radius2Sqr;
    }
    case PDCylinder:
    case PDCone: {
        pVector x(pos - p1);
        float dist = (p2 * x) * radius2Sqr;
        
        if (dist < 0.0f || dist > 1.0f)
            return FALSE;

        pVector xrad = x - p2 * dist; 
        float rSqr = xrad.length2();

        if (type == PDCone) {
            float scaledDist = dist * radius1;
            float scaledDist2 = dist * radius2;
            return (rSqr <= (scaledDist * scaledDist) && rSqr >= (scaledDist2 * scaledDist2));
        } else {
            return (rSqr <= radius1Sqr && rSqr >= (radius2 * radius2));
        }
    }
    case PDBlob: {
        pVector x(pos - p1);
        float Gx = std::exp(x.length2() * radius2Sqr) * radius2;
        return (get_rand() < Gx);
    }
    case PDPoint:
    case PDLine:
    case PDRectangle:
    case PDTriangle:
    case PDDisc:
    default:
        return FALSE;
    }
}

// Generate a random point uniformly distrbuted within the domain
void pDomain::Generate(pVector& pos) const {
    switch (type) {
    case PDPoint:
        pos = p1;
        break;
    case PDLine:
        pos = p1 + p2 * get_rand();
        break;
    case PDBox:
        pos.x = p1.x + (p2.x - p1.x) * get_rand();
        pos.y = p1.y + (p2.y - p1.y) * get_rand();
        pos.z = p1.z + (p2.z - p1.z) * get_rand();
        break;
    case PDTriangle: {
        float r1 = get_rand();
        float r2 = get_rand();
        if (r1 + r2 < 1.0f)
            pos = p1 + u * r1 + v * r2;
        else
            pos = p1 + u * (1.0f - r1) + v * (1.0f - r2);
    } break;
    case PDRectangle:
        pos = p1 + u * get_rand() + v * get_rand();
        break;
    case PDPlane: 
        pos = p1;
        break;
    case PDSphere:
        pos = RandVec() - vHalf;
        pos.normalize_safe();

        if (radius1 == radius2)
            pos = p1 + pos * radius1;
        else
            pos = p1 + pos * (radius2 + get_rand() * (radius1 - radius2));
        break;
    case PDCylinder:
    case PDCone: {
        float dist = get_rand();                       
        float theta = get_rand() * 2.0f * fPI; 
        float r = radius2 + get_rand() * (radius1 - radius2);

        float x = r * std::cos(theta); 
        float y = r * std::sin(theta);

        if (type == PDCone) {
            x *= dist;
            y *= dist;
        }

        pos = p1 + p2 * dist + u * x + v * y;
    } break;
    case PDBlob:
        pos.x = p1.x + NRand(radius1);
        pos.y = p1.y + NRand(radius1);
        pos.z = p1.z + NRand(radius1);
        break;
    case PDDisc: {
        float theta = get_rand() * 2.0f * fPI; 
        float r = radius2 + get_rand() * (radius1 - radius2);

        float x = r * std::cos(theta); 
        float y = r * std::sin(theta);

        pos = p1 + u * x + v * y;
    } break;
    default:
        pos = pVector(0.0f, 0.0f, 0.0f);
    }
}

void pDomain::transform(const pDomain& domain, const Fmatrix& m) {
    switch (type) {
    case PDBox: {
        Fbox* bb_dest = (Fbox*)&p1;
        Fbox* bb_from = (Fbox*)&domain.p1;
        bb_dest->xform(*bb_from, m);
    } break;
    case PDPlane:
        m.transform_tiny(p1, domain.p1);
        m.transform_dir(p2, domain.p2);
        radius1 = -(p1 * p2);
        break;
    case PDSphere:
        m.transform_tiny(p1, domain.p1);
        break;
    case PDCylinder:
    case PDCone:
        m.transform_tiny(p1, domain.p1);
        m.transform_dir(p2, domain.p2);
        m.transform_dir(u, domain.u);
        m.transform_dir(v, domain.v);
        break;
    case PDBlob:
        m.transform_tiny(p1, domain.p1);
        break;
    case PDPoint:
        m.transform_tiny(p1, domain.p1);
        break;
    case PDLine:
        m.transform_tiny(p1, domain.p1);
        m.transform_dir(p2, domain.p2);
        break;
    case PDRectangle:
        m.transform_tiny(p1, domain.p1);
        m.transform_dir(p2, domain.p2);
        m.transform_dir(u, domain.u);
        m.transform_dir(v, domain.v);
        break;
    case PDTriangle:
        m.transform_tiny(p1, domain.p1);
        m.transform_dir(p2, domain.p2);
        m.transform_dir(u, domain.u);
        m.transform_dir(v, domain.v);
        break;
    case PDDisc:
        m.transform_tiny(p1, domain.p1);
        m.transform_dir(p2, domain.p2);
        m.transform_dir(u, domain.u);
        m.transform_dir(v, domain.v);
        break;
    default:
        NODEFAULT;
    }
}

void pDomain::transform_dir(const pDomain& domain, const Fmatrix& m) {
    Fmatrix M = m;
    M.c.set(0.0f, 0.0f, 0.0f);
    transform(domain, M);
}