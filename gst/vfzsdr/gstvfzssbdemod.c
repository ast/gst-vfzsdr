#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include "gstvfzssbdemod.h"

GST_DEBUG_CATEGORY_STATIC (gst_vfzssbdemod_debug_category);
#define GST_CAT_DEFAULT gst_vfzssbdemod_debug_category

/* prototypes */


static void gst_vfzssbdemod_set_property (GObject * object,
                                          guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_vfzssbdemod_get_property (GObject * object,
                                          guint property_id, GValue * value, GParamSpec * pspec);
static void gst_vfzssbdemod_dispose (GObject * object);
static void gst_vfzssbdemod_finalize (GObject * object);

static GstCaps *gst_vfzssbdemod_transform_caps (GstBaseTransform * trans,
                                                GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_vfzssbdemod_fixate_caps (GstBaseTransform * trans,
                                             GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_vfzssbdemod_accept_caps (GstBaseTransform * trans,
                                             GstPadDirection direction, GstCaps * caps);
static gboolean gst_vfzssbdemod_set_caps (GstBaseTransform * trans,
                                          GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_vfzssbdemod_query (GstBaseTransform * trans,
                                       GstPadDirection direction, GstQuery * query);
static gboolean gst_vfzssbdemod_decide_allocation (GstBaseTransform * trans,
                                                   GstQuery * query);
static gboolean gst_vfzssbdemod_filter_meta (GstBaseTransform * trans,
                                             GstQuery * query, GType api, const GstStructure * params);
static gboolean gst_vfzssbdemod_propose_allocation (GstBaseTransform * trans,
                                                    GstQuery * decide_query, GstQuery * query);
static gboolean gst_vfzssbdemod_transform_size (GstBaseTransform * trans,
                                                GstPadDirection direction, GstCaps * caps, gsize size, GstCaps * othercaps,
                                                gsize * othersize);
static gboolean gst_vfzssbdemod_get_unit_size (GstBaseTransform * trans,
                                               GstCaps * caps, gsize * size);
static gboolean gst_vfzssbdemod_start (GstBaseTransform * trans);
static gboolean gst_vfzssbdemod_stop (GstBaseTransform * trans);
static gboolean gst_vfzssbdemod_sink_event (GstBaseTransform * trans,
                                            GstEvent * event);
static gboolean gst_vfzssbdemod_src_event (GstBaseTransform * trans,
                                           GstEvent * event);
static GstFlowReturn gst_vfzssbdemod_prepare_output_buffer (GstBaseTransform *
                                                            trans, GstBuffer * input, GstBuffer ** outbuf);
static gboolean gst_vfzssbdemod_copy_metadata (GstBaseTransform * trans,
                                               GstBuffer * input, GstBuffer * outbuf);
static gboolean gst_vfzssbdemod_transform_meta (GstBaseTransform * trans,
                                                GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf);
static void gst_vfzssbdemod_before_transform (GstBaseTransform * trans,
                                              GstBuffer * buffer);
static GstFlowReturn gst_vfzssbdemod_transform (GstBaseTransform * trans,
                                                GstBuffer * inbuf, GstBuffer * outbuf);
static GstFlowReturn gst_vfzssbdemod_transform_ip (GstBaseTransform * trans,
                                                   GstBuffer * buf);

enum
{
    PROP_0
};

/* Demodulation functions */
static void
gst_vfzssbdemod_lsb_demod (GstBaseTransform *trans, float *out, float complex *in, guint N) {
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    float y;
    float complex mixed;
    
    for (int n = 0; n < N; n++) {
        // Mix with local oscillator
        // Look up weaver method if you wonder what's going on.
        // TO ONLY DIFFERNCE BETWEEN USB AND LSB IS THE SIGN => -
        mixed = in[n] * osc_next(&vfzssbdemod->lo);
        y = 0.2 * (crealf(mixed) - cimag(mixed));
        // Remove DC
        out[n] = dc_filtered(&vfzssbdemod->dc_filter, y);
    }
}

static void
gst_vfzssbdemod_usb_demod (GstBaseTransform *trans, float *out, float complex *in, guint N) {
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    float y;
    float complex mixed;
    
    for (int n = 0; n < N; n++) {
        // Mix with local oscillator
        // Look up weaver method if you wonder what's going on.
        // TO ONLY DIFFERNCE BETWEEN USB AND LSB IS THE SIGN => +
        mixed = in[n] * osc_next(&vfzssbdemod->lo);
        y = 0.2 * (crealf(mixed) + cimag(mixed));
        // Remove DC
        out[n] = dc_filtered(&vfzssbdemod->dc_filter, y);
    }
}

/* Need to profile carefully to see if this is pointless */
static inline int
fast_is_greater(float* f1, float* f2)
{
    int i1, i2, t1, t2;
    
    i1 = *(int*)f1;
    i2 = *(int*)f2;
    
    t1 = i1 >> 31;
    i1 = (i1 ^ t1) + (t1 & 0x80000001);
    
    t2 = i2 >> 31;
    i2 = (i2 ^ t2) + (t2 & 0x80000001);
    
    return i1 > i2;
}

static inline float
cfastmag(float complex *in) {
    const float alpha = 0.947543636291;
    const float beta = 0.392485425092;
    float abs_i = fabsf(crealf(*in));
    float abs_q = fabsf(cimagf(*in));
    
    // Mag ~=Alpha * max(|I|, |Q|) + Beta * min(|I|, |Q|)
    if (fast_is_greater(&abs_i, &abs_q)) {
        return alpha * abs_i + beta * abs_q;
    } else {
        return alpha * abs_q + beta * abs_i;
    }
}

static void
gst_vfzssbdemod_am_demod (GstBaseTransform *trans, float *out, float complex *in, guint N) {
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    float y;
    for (int n = 0; n < N; n++) {
        
        y = cfastmag(&in[n]);
        // y = 0.5 * cabsf(in[n]);
        out[n] = dc_filtered(&vfzssbdemod->dc_filter, y);
    }
}

/* pad templates */

static GstStaticPadTemplate gst_vfzssbdemod_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
                         GST_PAD_SRC,
                         GST_PAD_ALWAYS,
                         GST_STATIC_CAPS ("audio/x-raw,format=F32LE,layout=interleaved,channels=1")
                         );

static GstStaticPadTemplate gst_vfzssbdemod_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
                         GST_PAD_SINK,
                         GST_PAD_ALWAYS,
                         GST_STATIC_CAPS ("audio/x-raw,format=F32LE,layout=interleaved,channels=2")
                         );


/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstVfzssbdemod, gst_vfzssbdemod,
                         GST_TYPE_BASE_TRANSFORM,
                         GST_DEBUG_CATEGORY_INIT (gst_vfzssbdemod_debug_category, "vfzssbdemod", 0,
                                                  "debug category for vfzssbdemod element"));

static void
gst_vfzssbdemod_class_init (GstVfzssbdemodClass * klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GstBaseTransformClass *base_transform_class =
    GST_BASE_TRANSFORM_CLASS (klass);
    
    /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
    gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
                                               &gst_vfzssbdemod_src_template);
    gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
                                               &gst_vfzssbdemod_sink_template);
    
    gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
                                           "FIXME Long name", "Generic", "FIXME Description",
                                           "FIXME <fixme@example.com>");
    
    gobject_class->set_property = gst_vfzssbdemod_set_property;
    gobject_class->get_property = gst_vfzssbdemod_get_property;
    gobject_class->dispose = gst_vfzssbdemod_dispose;
    gobject_class->finalize = gst_vfzssbdemod_finalize;

    
    base_transform_class->get_unit_size = GST_DEBUG_FUNCPTR (gst_vfzssbdemod_get_unit_size);
    base_transform_class->transform_caps = GST_DEBUG_FUNCPTR (gst_vfzssbdemod_transform_caps);
    base_transform_class->set_caps = GST_DEBUG_FUNCPTR (gst_vfzssbdemod_set_caps);
    base_transform_class->transform = GST_DEBUG_FUNCPTR (gst_vfzssbdemod_transform);
}

static void
gst_vfzssbdemod_init (GstVfzssbdemod * vfzssbdemod)
{
    // TODO: Probably move this to set_caps and make b1 dependent on sample rate.
    dc_filter_init(&vfzssbdemod->dc_filter, 0.995);
    
    // vfzssbdemod->process = gst_vfzssbdemod_lsb_demod;
    vfzssbdemod->process = gst_vfzssbdemod_am_demod;
}

void
gst_vfzssbdemod_set_property (GObject * object, guint property_id,
                              const GValue * value, GParamSpec * pspec)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (object);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "set_property");
    
    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

void
gst_vfzssbdemod_get_property (GObject * object, guint property_id,
                              GValue * value, GParamSpec * pspec)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (object);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "get_property");
    
    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

void
gst_vfzssbdemod_dispose (GObject * object)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (object);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "dispose");
    
    /* clean up as possible.  may be called multiple times */
    
    G_OBJECT_CLASS (gst_vfzssbdemod_parent_class)->dispose (object);
}

void
gst_vfzssbdemod_finalize (GObject * object)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (object);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "finalize");
    
    /* clean up object here */
    
    G_OBJECT_CLASS (gst_vfzssbdemod_parent_class)->finalize (object);
}

static GstCaps *
gst_vfzssbdemod_transform_caps (GstBaseTransform * trans,
                                GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "transform_caps");

    GstCaps *output = NULL;
    GstStructure *structure;
    gint i;
    
    output = gst_caps_copy (caps);
    
    switch (direction) {
        case GST_PAD_SINK:
            for (i = 0; i < gst_caps_get_size (output); i++) {
                structure = gst_caps_get_structure (output, i);
                gst_structure_set (structure,
                                   "channels", G_TYPE_INT, 1,
                                   NULL);
            }
            break;
        case GST_PAD_SRC:
            for (i = 0; i < gst_caps_get_size (output); i++) {
                structure = gst_caps_get_structure (output, i);
                gst_structure_set (structure,
                                   "channels", G_TYPE_INT, 2,
                                   NULL);
            }
            break;
        default:
            gst_caps_unref (output);
            output = NULL;
            g_assert_not_reached ();
            break;
    }
    
    if (filter) {
        GstCaps *intersect;
        intersect = gst_caps_intersect (output, filter);
        gst_caps_unref (output);
        return intersect;
    } else {
        //GST_LOG ("HELLO are %" GST_PTR_FORMAT, othercaps);
        return output;
    }
}

static GstCaps *
gst_vfzssbdemod_fixate_caps (GstBaseTransform * trans,
                             GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "fixate_caps");
    
    return NULL;
}

static gboolean
gst_vfzssbdemod_accept_caps (GstBaseTransform * trans,
                             GstPadDirection direction, GstCaps * caps)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "accept_caps");
    
    return TRUE;
}

static gboolean
gst_vfzssbdemod_set_caps (GstBaseTransform * trans, GstCaps * incaps,
                          GstCaps * outcaps)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "set_caps");
    
    GstStructure *structure;
    GValue *value;
    
    g_assert(GST_CAPS_IS_SIMPLE(incaps));
    structure = gst_caps_get_structure (incaps, 0);
    
    g_assert(gst_structure_has_field_typed(structure, "rate", G_TYPE_INT));
    value = (GValue*) gst_structure_get_value(structure, "rate");
    gint rate = g_value_get_int(value);

    osc_init(&vfzssbdemod->lo, 1500., (float) rate);
    
    return TRUE;
}

static gboolean
gst_vfzssbdemod_query (GstBaseTransform * trans, GstPadDirection direction,
                       GstQuery * query)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "query");
    
    return TRUE;
}

/* decide allocation query for output buffers */
static gboolean
gst_vfzssbdemod_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "decide_allocation");
    
    return TRUE;
}

static gboolean
gst_vfzssbdemod_filter_meta (GstBaseTransform * trans, GstQuery * query,
                             GType api, const GstStructure * params)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "filter_meta");
    
    return TRUE;
}

/* propose allocation query parameters for input buffers */
static gboolean
gst_vfzssbdemod_propose_allocation (GstBaseTransform * trans,
                                    GstQuery * decide_query, GstQuery * query)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "propose_allocation");
    
    return TRUE;
}

/* transform size */
static gboolean
gst_vfzssbdemod_transform_size (GstBaseTransform * trans,
                                GstPadDirection direction, GstCaps * caps, gsize size, GstCaps * othercaps,
                                gsize * othersize)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "transform_size");
    
    return TRUE;
}

static gboolean
gst_vfzssbdemod_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
                               gsize * size)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    GstStructure *structure;
    gint channels, ret;
    
    structure = gst_caps_get_structure (caps, 0);
    ret = gst_structure_get_int (structure, "channels", &channels);
    if (!ret)
        return FALSE;
    
    *size = sizeof(float) * channels;
    
    GST_DEBUG_OBJECT (vfzssbdemod, "get_unit_size");
    
    return TRUE;
}

/* states */
static gboolean
gst_vfzssbdemod_start (GstBaseTransform * trans)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "start");
    
    return TRUE;
}

static gboolean
gst_vfzssbdemod_stop (GstBaseTransform * trans)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "stop");
    
    return TRUE;
}

/* sink and src pad event handlers */
static gboolean
gst_vfzssbdemod_sink_event (GstBaseTransform * trans, GstEvent * event)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "sink_event");
    
    return GST_BASE_TRANSFORM_CLASS (gst_vfzssbdemod_parent_class)->
    sink_event (trans, event);
}

static gboolean
gst_vfzssbdemod_src_event (GstBaseTransform * trans, GstEvent * event)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "src_event");
    
    return GST_BASE_TRANSFORM_CLASS (gst_vfzssbdemod_parent_class)->
    src_event (trans, event);
}

static GstFlowReturn
gst_vfzssbdemod_prepare_output_buffer (GstBaseTransform * trans,
                                       GstBuffer * input, GstBuffer ** outbuf)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "prepare_output_buffer");
    
    return GST_FLOW_OK;
}

/* metadata */
static gboolean
gst_vfzssbdemod_copy_metadata (GstBaseTransform * trans, GstBuffer * input,
                               GstBuffer * outbuf)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "copy_metadata");
    
    return TRUE;
}

static gboolean
gst_vfzssbdemod_transform_meta (GstBaseTransform * trans, GstBuffer * outbuf,
                                GstMeta * meta, GstBuffer * inbuf)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "transform_meta");
    
    return TRUE;
}

static void
gst_vfzssbdemod_before_transform (GstBaseTransform * trans, GstBuffer * buffer)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "before_transform");
    
}


/* transform */
static GstFlowReturn
gst_vfzssbdemod_transform (GstBaseTransform * trans, GstBuffer * inbuf,
                           GstBuffer * outbuf)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "transform");
    
    GstMapInfo inmap, outmap;
    
    gst_buffer_map (inbuf, &inmap, GST_MAP_READ);
    gst_buffer_map (outbuf, &outmap, GST_MAP_WRITE);
    
    float complex *in = (float complex*) inmap.data;
    float *out = (float*) outmap.data;
    
    int N = (int) inmap.size / sizeof(float complex);
    // Function pointer to actual demodulation function
    vfzssbdemod->process(GST_BASE_TRANSFORM(vfzssbdemod), out, in, N);
    
    gst_buffer_unmap (inbuf, &inmap);
    gst_buffer_unmap (outbuf, &outmap);
    
    return GST_FLOW_OK;
}

static GstFlowReturn
gst_vfzssbdemod_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
    GstVfzssbdemod *vfzssbdemod = GST_VFZSSBDEMOD (trans);
    
    GST_DEBUG_OBJECT (vfzssbdemod, "transform_ip");
    
    return GST_FLOW_OK;
}

gboolean
gst_vfzssbdemod_plugin_init (GstPlugin * plugin)
{
    return gst_element_register (plugin, "vfzssbdemod",
                                 GST_RANK_NONE, GST_TYPE_VFZSSBDEMOD);
}
