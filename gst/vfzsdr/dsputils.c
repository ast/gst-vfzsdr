//
//  dsputils.c
//  meta
//
//  Created by Albin Stigö on 28/01/2018.
//  Copyright © 2018 Albin Stigo. All rights reserved.
//

#include "dsputils.h"

#include <math.h>

void osc_init(osc_t *o, float f, float fs) {
    float w = f * (2 * M_PI / fs);
    o->u = 1;
    o->v = 0;
    o->k1 = tanf(0.5 * w);
    o->k2 = 2 * o->k1 / (1 + o->k1 * o->k1);
}

void dc_filter_init(dc_filter_t *f, float b1) {
    f->xz1 = f->yz1 = 0.;
    f->b1 = b1;
}
