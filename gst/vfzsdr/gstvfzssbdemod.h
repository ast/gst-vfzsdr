/* GStreamer
 * Copyright (C) 2018 FIXME <fixme@example.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GST_VFZSSBDEMOD_H_
#define _GST_VFZSSBDEMOD_H_

#include <gst/base/gstbasetransform.h>
#include <math.h>
#include <complex.h>

#include "dsputils.h"

G_BEGIN_DECLS

#define GST_TYPE_VFZSSBDEMOD   (gst_vfzssbdemod_get_type())
#define GST_VFZSSBDEMOD(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VFZSSBDEMOD,GstVfzssbdemod))
#define GST_VFZSSBDEMOD_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VFZSSBDEMOD,GstVfzssbdemodClass))
#define GST_IS_VFZSSBDEMOD(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VFZSSBDEMOD))
#define GST_IS_VFZSSBDEMOD_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VFZSSBDEMOD))

typedef struct _GstVfzssbdemod GstVfzssbdemod;
typedef struct _GstVfzssbdemodClass GstVfzssbdemodClass;


typedef void (*GstSsbDemodProcessFunc) (GstBaseTransform *, float *, float complex *, guint);

struct _GstVfzssbdemod
{
    GstBaseTransform base_vfzssbdemod;
    
    // Private
    GstSsbDemodProcessFunc process;
    // Local oscillator
    osc_t lo;
    osc_t cw_lo;
    // DC removal filter
    dc_filter_t dc_filter;
};

struct _GstVfzssbdemodClass
{
    GstBaseTransformClass base_vfzssbdemod_class;
};

GType gst_vfzssbdemod_get_type (void);

gboolean gst_vfzssbdemod_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif
