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

#ifndef _GST_VFZFFTSIMP_H_
#define _GST_VFZFFTSIMP_H_

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/gstbufferpool.h>
#include <gst/audio/audio.h>

#include <NE10/NE10.h>
#include <liquid/liquid.h>
#include <complex.h>

G_BEGIN_DECLS

#define GST_TYPE_VFZFFTSIMP   (gst_vfzfftsimp_get_type())
#define GST_VFZFFTSIMP(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VFZFFTSIMP,GstVfzfftsimp))
#define GST_VFZFFTSIMP_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VFZFFTSIMP,GstVfzfftsimpClass))
#define GST_IS_VFZFFTSIMP(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VFZFFTSIMP))
#define GST_IS_VFZFFTSIMP_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VFZFFTSIMP))

typedef struct _GstVfzfftsimp GstVfzfftsimp;
typedef struct _GstVfzfftsimpClass GstVfzfftsimpClass;

struct _GstVfzfftsimp
{
    GstElement base_vfzfftsimp;
    
    // Pads
    GstPad *sinkpad;
    GstPad *srcpad;
    // Objects
    GstBufferPool *buffer_pool;
    GstAdapter *adapter;
    GstAudioInfo *audio_info_src;
    
    // FFT configs
    ne10_fft_cfg_float32_t fft_cfg;
    ne10_fft_cfg_float32_t ifft_cfg;

    // FFT outputs
    float complex *fft_out;
    float complex *ifft_out;
    // Frequnecy domain filter
    float complex *H;
    float complex *hc;
    
    // Filter coefficients
    guint L;     // input block size
    guint P;     // length of filter
    guint D;     // decimation factor
    guint N;     // length of FFT
};

struct _GstVfzfftsimpClass
{
    GstElementClass base_vfzfftsimp_class;
};

GType gst_vfzfftsimp_get_type (void);

gboolean gst_vfzfftsimp_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif
