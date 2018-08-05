#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include "gstvfzsdr.h"
#include <complex.h>

GST_DEBUG_CATEGORY_STATIC (gst_vfzsdr_debug_category);
#define GST_CAT_DEFAULT gst_vfzsdr_debug_category

/* prototypes */

static void gst_vfzsdr_set_property (GObject * object,
                                     guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_vfzsdr_get_property (GObject * object,
                                     guint property_id, GValue * value, GParamSpec * pspec);
static void gst_vfzsdr_dispose (GObject * object);
static void gst_vfzsdr_finalize (GObject * object);

static GstCaps *gst_vfzsdr_transform_caps (GstBaseTransform * trans,
                                           GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_vfzsdr_fixate_caps (GstBaseTransform * trans,
                                        GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_vfzsdr_accept_caps (GstBaseTransform * trans,
                                        GstPadDirection direction, GstCaps * caps);
static gboolean gst_vfzsdr_set_caps (GstBaseTransform * trans,
                                     GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_vfzsdr_query (GstBaseTransform * trans,
                                  GstPadDirection direction, GstQuery * query);
static gboolean gst_vfzsdr_decide_allocation (GstBaseTransform * trans,
                                              GstQuery * query);
static gboolean gst_vfzsdr_filter_meta (GstBaseTransform * trans,
                                        GstQuery * query, GType api, const GstStructure * params);
static gboolean gst_vfzsdr_propose_allocation (GstBaseTransform * trans,
                                               GstQuery * decide_query, GstQuery * query);
static gboolean gst_vfzsdr_transform_size (GstBaseTransform * trans,
                                           GstPadDirection direction, GstCaps * caps, gsize size, GstCaps * othercaps,
                                           gsize * othersize);
static gboolean gst_vfzsdr_get_unit_size (GstBaseTransform * trans,
                                          GstCaps * caps, gsize * size);
static gboolean gst_vfzsdr_start (GstBaseTransform * trans);
static gboolean gst_vfzsdr_stop (GstBaseTransform * trans);
static gboolean gst_vfzsdr_sink_event (GstBaseTransform * trans,
                                       GstEvent * event);
static gboolean gst_vfzsdr_src_event (GstBaseTransform * trans,
                                      GstEvent * event);
static GstFlowReturn gst_vfzsdr_prepare_output_buffer (GstBaseTransform *
                                                       trans, GstBuffer * input, GstBuffer ** outbuf);
static gboolean gst_vfzsdr_copy_metadata (GstBaseTransform * trans,
                                          GstBuffer * input, GstBuffer * outbuf);
static gboolean gst_vfzsdr_transform_meta (GstBaseTransform * trans,
                                           GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf);
static void gst_vfzsdr_before_transform (GstBaseTransform * trans,
                                         GstBuffer * buffer);
static GstFlowReturn gst_vfzsdr_transform (GstBaseTransform * trans,
                                           GstBuffer * inbuf, GstBuffer * outbuf);
static GstFlowReturn gst_vfzsdr_transform_ip (GstBaseTransform * trans,
                                              GstBuffer * buf);

enum
{
    PROP_0
};

/* pad templates */

static GstStaticPadTemplate gst_vfzsdr_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
                         GST_PAD_SRC,
                         GST_PAD_ALWAYS,
                         GST_STATIC_CAPS ("audio/x-raw,format=F32LE,layout=interleaved,channels=2")
                         );

static GstStaticPadTemplate gst_vfzsdr_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
                         GST_PAD_SINK,
                         GST_PAD_ALWAYS,
                         GST_STATIC_CAPS ("audio/x-raw,format=F32LE,layout=interleaved,channels=2")
                         );


/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstVfzsdr, gst_vfzsdr, GST_TYPE_BASE_TRANSFORM,
                         GST_DEBUG_CATEGORY_INIT (gst_vfzsdr_debug_category, "vfzsdr", 0,
                                                  "debug category for vfzsdr element"));

static void
gst_vfzsdr_class_init (GstVfzsdrClass * klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GstBaseTransformClass *base_transform_class =
    GST_BASE_TRANSFORM_CLASS (klass);
    
    /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */

    gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
                                               &gst_vfzsdr_src_template);
    
    gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
                                               &gst_vfzsdr_sink_template);
    
    gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
                                           "FIXME Long name", "Generic", "FIXME Description",
                                           "FIXME <fixme@example.com>");
    
    gobject_class->set_property = gst_vfzsdr_set_property;
    gobject_class->get_property = gst_vfzsdr_get_property;
    gobject_class->dispose = gst_vfzsdr_dispose;
    gobject_class->finalize = gst_vfzsdr_finalize;

    // base_transform_class->transform_size = GST_DEBUG_FUNCPTR (gst_vfzsdr_transform_size);
    // base_transform_class->get_unit_size = GST_DEBUG_FUNCPTR (gst_vfzsdr_get_unit_size);
    // base_transform_class->transform_caps = GST_DEBUG_FUNCPTR (gst_vfzsdr_transform_caps);
    // base_transform_class->transform = GST_DEBUG_FUNCPTR (gst_vfzsdr_transform);
    
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR (gst_vfzsdr_transform_ip);
}

static void
gst_vfzsdr_init (GstVfzsdr * vfzsdr)
{
    vfzsdr->ref = 1.0;
    vfzsdr->rate_rise = 0.050;
    vfzsdr->rate_hang = 0.002;
    vfzsdr->gain = 50.0;
}

void
gst_vfzsdr_set_property (GObject * object, guint property_id,
                         const GValue * value, GParamSpec * pspec)
{
    GstVfzsdr *vfzsdr = GST_VFZSDR (object);
    
    GST_DEBUG_OBJECT (vfzsdr, "set_property");
    
    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

void
gst_vfzsdr_get_property (GObject * object, guint property_id,
                         GValue * value, GParamSpec * pspec)
{
    GstVfzsdr *vfzsdr = GST_VFZSDR (object);
    
    GST_DEBUG_OBJECT (vfzsdr, "get_property");

    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

void
gst_vfzsdr_dispose (GObject * object)
{
    GstVfzsdr *vfzsdr = GST_VFZSDR (object);
    
    GST_DEBUG_OBJECT (vfzsdr, "dispose");
    
    /* clean up as possible.  may be called multiple times */

    G_OBJECT_CLASS (gst_vfzsdr_parent_class)->dispose (object);
}

void
gst_vfzsdr_finalize (GObject * object)
{
    GstVfzsdr *vfzsdr = GST_VFZSDR (object);
    
    GST_DEBUG_OBJECT (vfzsdr, "finalize");
    
    /* clean up object here */
    
    G_OBJECT_CLASS (gst_vfzsdr_parent_class)->finalize (object);
}

/* states */
static gboolean
gst_vfzsdr_start (GstBaseTransform * trans)
{
    GstVfzsdr *vfzsdr = GST_VFZSDR (trans);
    
    GST_DEBUG_OBJECT (vfzsdr, "start");
    
    return TRUE;
}

static gboolean
gst_vfzsdr_stop (GstBaseTransform * trans)
{
    GstVfzsdr *vfzsdr = GST_VFZSDR (trans);
    
    GST_DEBUG_OBJECT (vfzsdr, "stop");
    
    return TRUE;
}

/* Need to profile carefully to see if this is pointless */
static inline gboolean
fast_is_greater_than_zero(float* f)
{
    return *(int*)&f > 0;
}

static GstFlowReturn
gst_vfzsdr_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
    GstVfzsdr *vfzsdr = GST_VFZSDR (trans);
    
    GST_DEBUG_OBJECT (vfzsdr, "transform_ip");
    
    // GST_DEBUG_OBJECT (vfzsdr, "transform");
    // TODO: do in place
    
    GstMapInfo map;
    
    gst_buffer_map (buf, &map, GST_MAP_READWRITE);
    
    // g_print("%d %d\n", inmap.size, outmap.size);
    
    float complex *samples = (float complex*) map.data;
    
    float complex out;
    float pow, diff;
    
    int outsize = (int) map.size / sizeof(float complex);
    for (int i = 0; i < outsize; i++) {
        // apply current gain
        out = samples[i] = samples[i] * vfzsdr->gain;
        // samples[i] = out;
        // estimate power in output and adjust gain
        pow = crealf(out * conjf(out));
        
        diff = vfzsdr->ref - pow;
        
        // Faster compare?
        if(fast_is_greater_than_zero(&diff)) {
            vfzsdr->gain += diff * vfzsdr->rate_hang;
        } else {
            vfzsdr->gain += diff * vfzsdr->rate_rise;
        }
    }
    
    gst_buffer_unmap (buf, &map);

    return GST_FLOW_OK;
}

gboolean
gst_vfzsdr_plugin_init (GstPlugin * plugin) {
    return gst_element_register (plugin, "vfzsdr",
                                 GST_RANK_NONE, GST_TYPE_VFZSDR);
}
