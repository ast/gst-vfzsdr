//
//  dsputils.h
//  meta
//
//  Created by Albin Stigö on 28/01/2018.
//  Copyright © 2018 Albin Stigo. All rights reserved.
//

#ifndef dsputils_h
#define dsputils_h

#include <complex.h>

/* Complex recursive oscillator implementation */
typedef struct {
    float v;
    float u;
    float k1;
    float k2;
} osc_t;

void osc_init(osc_t *o, float f, float fs);

static inline float complex osc_next(osc_t *o) {
    float tmp;
    tmp = o->u - o->k1 * o->v;
    o->v = o->v + o->k2 * tmp;
    o->u = tmp - o->k1 * o->v;
    return o->u + o->v * I;
}

/* Recurse DC filter */
typedef struct {
    float b1;
    float xz1;
    float yz1;
} dc_filter_t;

void dc_filter_init(dc_filter_t *f, float b1);

static inline float dc_filtered(dc_filter_t *f, float x) {
    float y = x - f->xz1 + f->b1 * f->yz1;
    f->xz1 = x;
    f->yz1 = y;
    return y;
}

#endif /* dsputils_h */
