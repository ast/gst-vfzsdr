#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstvfzfftsimp.h"

GST_DEBUG_CATEGORY_STATIC (gst_vfzfftsimp_debug_category);
#define GST_CAT_DEFAULT gst_vfzfftsimp_debug_category


#define ARRAYSIZE(arr) (sizeof(arr) / sizeof(arr[0]))

/* prototypes */
static void gst_vfzfftsimp_set_property (GObject * object,
                                         guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_vfzfftsimp_get_property (GObject * object,
                                         guint property_id, GValue * value, GParamSpec * pspec);
static void gst_vfzfftsimp_dispose (GObject * object);
static void gst_vfzfftsimp_finalize (GObject * object);
static gboolean gst_vfzfftsimp_sink_event (GstPad    *pad,
                                           GstObject *parent,
                                           GstEvent  *event);
static GstFlowReturn gst_vfzfftsimp_sink_chain (GstPad * pad,
                                                GstObject *parent,
                                                GstBuffer * buffer);

static gboolean
gst_vfzfftsimp_sink_query (GstPad *pad,
                           GstObject *parent,
                           GstQuery *query);

enum
{
    PROP_0
};

/* pad templates */

static GstStaticPadTemplate gst_vfzfftsimp_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
                         GST_PAD_SINK,
                         GST_PAD_ALWAYS,
                         GST_STATIC_CAPS ("audio/x-raw,"
                                          "format=(string)F32LE,"
                                          "channel-mask=(bitmask)0x0000000000000003,"
                                          "rate=(int)[1, MAX],"
                                          "layout=(string)interleaved,"
                                          "channels=(int)2")
                         );

static GstStaticPadTemplate gst_vfzfftsimp_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
                         GST_PAD_SRC,
                         GST_PAD_ALWAYS,
                         GST_STATIC_CAPS ("audio/x-raw,"
                                          "format=(string)F32LE,"
                                          "channel-mask=(bitmask)0x0000000000000003,"
                                          "rate=(int)[1, MAX],"
                                          "layout=(string)interleaved,"
                                          "channels=(int)2")
                         );


/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstVfzfftsimp, gst_vfzfftsimp, GST_TYPE_ELEMENT,
                         GST_DEBUG_CATEGORY_INIT (gst_vfzfftsimp_debug_category, "vfzfftsimp", 0,
                                                  "debug category for vfzfftsimp element"));

static void
gst_vfzfftsimp_class_init (GstVfzfftsimpClass * klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
    
    /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
    gst_element_class_add_static_pad_template (element_class,
                                               &gst_vfzfftsimp_sink_template);
    gst_element_class_add_static_pad_template (element_class,
                                               &gst_vfzfftsimp_src_template);
    
    gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
                                           "FIXME Long name", "Generic", "FIXME Description",
                                           "FIXME <fixme@example.com>");
    
    gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_vfzfftsimp_set_property);
    gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_vfzfftsimp_get_property);
    gobject_class->dispose = GST_DEBUG_FUNCPTR(gst_vfzfftsimp_dispose);
    gobject_class->finalize = GST_DEBUG_FUNCPTR(gst_vfzfftsimp_finalize);
    
}

static void
gst_vfzfftsimp_init (GstVfzfftsimp * vfzfftsimp)
{
    vfzfftsimp->sinkpad =
    gst_pad_new_from_static_template (&gst_vfzfftsimp_sink_template, "sink");
    gst_pad_set_chain_function (vfzfftsimp->sinkpad,
                                GST_DEBUG_FUNCPTR(gst_vfzfftsimp_sink_chain));
    gst_pad_set_event_function (vfzfftsimp->sinkpad,
                                GST_DEBUG_FUNCPTR(gst_vfzfftsimp_sink_event));
    gst_pad_set_query_function(vfzfftsimp->sinkpad,
                               GST_DEBUG_FUNCPTR(gst_vfzfftsimp_sink_query));
    
    gst_element_add_pad (GST_ELEMENT (vfzfftsimp), vfzfftsimp->sinkpad);
    
    vfzfftsimp->srcpad =
    gst_pad_new_from_static_template (&gst_vfzfftsimp_src_template, "src");
    gst_element_add_pad (GST_ELEMENT (vfzfftsimp), vfzfftsimp->srcpad);
    
    // TODO: need to move this to a separate function and call on state changes
    // Filter
    vfzfftsimp->D = 2;       // decimation factor
    vfzfftsimp->P = 801;     // filter taps
    vfzfftsimp->N = 4096;    // length of FFT

    // N, P and D are designed choices
    // L (block length) is determined by these
    vfzfftsimp->L = vfzfftsimp->N - vfzfftsimp->P + 1;
    
    // TODO: just som random number, must be divisible by D.
    g_assert((vfzfftsimp->P - 1) % vfzfftsimp->D == 0);
    
    // Neon setup
    ne10_init();
    g_assert(ne10_HasNEON() == NE10_OK);
    // Setup FFTs
    vfzfftsimp->fft_cfg = ne10_fft_alloc_c2c_float32(vfzfftsimp->N);
    g_assert(vfzfftsimp->fft_cfg != NULL);

    vfzfftsimp->ifft_cfg = ne10_fft_alloc_c2c_float32(vfzfftsimp->N / vfzfftsimp->D);
    g_assert(vfzfftsimp->ifft_cfg != NULL);
    
    // Output of filter fft
    vfzfftsimp->fft_out = g_malloc0(vfzfftsimp->N * sizeof(float complex));
    // decimated so shorter
    vfzfftsimp->ifft_out = g_malloc0(vfzfftsimp->N / vfzfftsimp->D * sizeof(float complex));
    // time domain
    vfzfftsimp->hc = g_malloc0(vfzfftsimp->N * sizeof(float complex));
    // frequency domain filter
    vfzfftsimp->H = g_malloc0(vfzfftsimp->N * sizeof(float complex));
    
    // adapter
    vfzfftsimp->adapter = gst_adapter_new();
    
    // buffer pool
    vfzfftsimp->buffer_pool = gst_buffer_pool_new();
    
    // audio info
    vfzfftsimp->audio_info_src = gst_audio_info_new();
    
    // Create a test filter
    float fc = 5000.0 / (float) 89286;
    float As = 60.0f; // stop-band attenuation [dB]
    float mu = 0.0f;
    float h[vfzfftsimp->P];
    liquid_firdes_kaiser(vfzfftsimp->P, fc, As, mu, h);
    // TODO: make this a macro or something
    for (int n = 0; n < ARRAYSIZE(h); n++) {
        vfzfftsimp->hc[n] = h[n];
    }
    // Do FFT of taps
    // This FFT needs all variables on the heap it seems?
    ne10_fft_c2c_1d_float32((ne10_fft_cpx_float32_t*)vfzfftsimp->H,
                            (ne10_fft_cpx_float32_t*)vfzfftsimp->hc,
                            vfzfftsimp->fft_cfg,
                            0);
}

void
gst_vfzfftsimp_set_property (GObject * object, guint property_id,
                             const GValue * value, GParamSpec * pspec)
{
    GstVfzfftsimp *vfzfftsimp = GST_VFZFFTSIMP (object);
    
    GST_DEBUG_OBJECT (vfzfftsimp, "set_property");
    
    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }


}

void
gst_vfzfftsimp_get_property (GObject * object, guint property_id,
                             GValue * value, GParamSpec * pspec)
{
    GstVfzfftsimp *vfzfftsimp = GST_VFZFFTSIMP (object);
    
    GST_DEBUG_OBJECT (vfzfftsimp, "get_property");
    
    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

void
gst_vfzfftsimp_dispose (GObject * object)
{
    GstVfzfftsimp *vfzfftsimp = GST_VFZFFTSIMP (object);
    
    GST_DEBUG_OBJECT (vfzfftsimp, "dispose");
    
    /* clean up as possible.  may be called multiple times */
    g_clear_object(&vfzfftsimp->adapter);
    g_clear_object(&vfzfftsimp->buffer_pool);
    g_clear_object(&vfzfftsimp->audio_info_src);
    
    G_OBJECT_CLASS (gst_vfzfftsimp_parent_class)->dispose (object);
}

void
gst_vfzfftsimp_finalize (GObject * object)
{
    GstVfzfftsimp *vfzfftsimp = GST_VFZFFTSIMP (object);
    
    GST_DEBUG_OBJECT (vfzfftsimp, "finalize");
    
    /* clean up object here */
    
    ne10_fft_destroy_c2c_float32(vfzfftsimp->fft_cfg);
    ne10_fft_destroy_c2c_float32(vfzfftsimp->ifft_cfg);
    g_free(vfzfftsimp->fft_out);
    g_free(vfzfftsimp->ifft_out);
    
    G_OBJECT_CLASS (gst_vfzfftsimp_parent_class)->finalize (object);
}

static gboolean
gst_vfzfftsimp_propose_allocation (GstVfzfftsimp *vfzfftsimp, GstQuery * query) {
    GstBufferPool *pool;
    GstCaps *caps;
    GstAudioInfo info;
    GstStructure *config;
    guint size;

    gst_query_parse_allocation (query, &caps, NULL);
    if (caps == NULL)
        return FALSE;
    if (!gst_audio_info_from_caps(&info, caps))
        return FALSE;
    
    // Propose our block size
    size = GST_AUDIO_INFO_BPF(&info) * vfzfftsimp->L;

    pool = gst_buffer_pool_new ();
    
    gst_query_add_allocation_pool (query, pool, size, 0, 0);
    
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);
    gst_buffer_pool_set_config (pool, config);
    gst_object_unref (pool);
    

    return TRUE;
}

static gboolean
gst_vfzfftsimp_sink_query (GstPad *pad,
                           GstObject *parent,
                           GstQuery *query) {
    
    gboolean ret;
    GstVfzfftsimp *vfzfftsimp = GST_VFZFFTSIMP (parent);
    
    switch (GST_QUERY_TYPE (query)) {
        case GST_QUERY_ALLOCATION:
            // g_message("allocation!!!");
            ret = gst_vfzfftsimp_propose_allocation (vfzfftsimp, query);
            /*case GST_QUERY_POSITION:
             // we should report the current position
             break;
             case GST_QUERY_DURATION:
             // we should report the duration here
             break;
             case GST_QUERY_CAPS:
             // we should report the supported caps here
             break;*/
        default:
            /* just call the default handler */
            ret = gst_pad_query_default (pad, parent, query);
            break;
    }
    return ret;
}

static GstFlowReturn
gst_vfzfftsimp_filter (GstVfzfftsimp *vfzfftsimp,
                       GstBuffer *outbuffer,
                       const float complex *insamples)
{
    GstMapInfo outmap;
    
    // Perform forward FFT
    ne10_fft_c2c_1d_float32((ne10_fft_cpx_float32_t*)vfzfftsimp->fft_out,
                            (ne10_fft_cpx_float32_t*)insamples,
                            vfzfftsimp->fft_cfg,
                            0); // <- forward
    
    // Perform circular convolution FFT{insamples} * H
    for (int n = 0; n < vfzfftsimp->N; n++) {
        vfzfftsimp->fft_out[n] *= vfzfftsimp->H[n];
    }
    
    // Decimate i freq domain by folding the FFT over itself
    // TODO: fix only decimating by two
    guint ND = vfzfftsimp->N / vfzfftsimp->D;
    for (int n = 0; n < ND; n++) {
        vfzfftsimp->fft_out[n] += vfzfftsimp->fft_out[ND + n];
    }
    
    // Inverse FFT
    ne10_fft_c2c_1d_float32((ne10_fft_cpx_float32_t*)vfzfftsimp->ifft_out,
                            (ne10_fft_cpx_float32_t*)vfzfftsimp->fft_out,
                            vfzfftsimp->ifft_cfg,
                            1); // <- inverse
    
    // Discard (P-1)/D samples because of circular convoltuion
    gst_buffer_map (outbuffer, &outmap, GST_MAP_WRITE);
    guint offset = (vfzfftsimp->P - 1) / vfzfftsimp->D;
    // Copy data to buffer
    memcpy(outmap.data, &vfzfftsimp->ifft_out[offset], outmap.size);
    gst_buffer_unmap (outbuffer, &outmap);
    
    return GST_FLOW_OK;
}

static GstFlowReturn
gst_vfzfftsimp_sink_chain (GstPad * pad, GstObject *parent, GstBuffer * buffer)
{
    GstVfzfftsimp *vfzfftsimp = GST_VFZFFTSIMP (parent);
    
    GstFlowReturn ret;
    GstBuffer *outbuffer;
    GstBufferPool *pool = vfzfftsimp->buffer_pool;
    GstAudioInfo *info = vfzfftsimp->audio_info_src;
    const float complex *samples;
    
    GST_DEBUG_OBJECT (vfzfftsimp, "chain");
    
    gst_adapter_push(vfzfftsimp->adapter, buffer);
    
    gsize process_size = GST_AUDIO_INFO_BPF(info) * vfzfftsimp->N;
    gsize block_size = GST_AUDIO_INFO_BPF(info) * vfzfftsimp->L;
    
    // Process while we have process_size or more
    while (gst_adapter_available(vfzfftsimp->adapter) >= process_size) {
        // we need to process process_size but only flush block size thus
        // leaving a tail and introducing a latency.
        samples = (const float complex *) gst_adapter_map(vfzfftsimp->adapter, process_size);
        g_assert(samples != NULL);
        // do stuff with buffer
        
        // we have input data so get an output buffer
        ret = gst_buffer_pool_acquire_buffer (pool, &outbuffer, NULL);
        if (G_UNLIKELY (ret != GST_FLOW_OK))
            goto pool_failed;
        
        gst_vfzfftsimp_filter(vfzfftsimp, outbuffer, samples);
        gst_adapter_unmap(vfzfftsimp->adapter);
        gst_adapter_flush(vfzfftsimp->adapter, block_size);
        GST_DEBUG_OBJECT (vfzfftsimp, "output %lu", gst_buffer_get_size(outbuffer));
        
        // TODO: scale timestamps and set correctly
        GST_BUFFER_DTS(outbuffer) = GST_CLOCK_TIME_NONE;
        GST_BUFFER_PTS(outbuffer) = GST_CLOCK_TIME_NONE;
        
        ret = gst_pad_push (vfzfftsimp->srcpad, outbuffer);
        if (G_UNLIKELY (ret != GST_FLOW_OK))
            goto push_failed;
    }
    return ret;
    
push_failed:
    GST_ERROR_OBJECT (vfzfftsimp, "gst_pad_push failed");
    return GST_FLOW_ERROR;
pool_failed:
    GST_ERROR_OBJECT (vfzfftsimp, "gst_buffer_pool_acquire_buffer failed");
    return GST_FLOW_ERROR;
}

static gboolean
gst_vfzfftsimp_setcaps (GstVfzfftsimp *vfzfftsimp,
                        GstCaps *caps)
{
    gboolean ret;
    GstCaps *outcaps;
    
    ret = gst_audio_info_from_caps(vfzfftsimp->audio_info_src, caps);
    g_assert(ret);
    // change rate by decimation factor and set
    GST_AUDIO_INFO_RATE(vfzfftsimp->audio_info_src) /= vfzfftsimp->D;
    // outcaps are like incaps with lower rate
    outcaps = gst_audio_info_to_caps(vfzfftsimp->audio_info_src);
    // set caps
    ret = gst_pad_set_caps (vfzfftsimp->srcpad, outcaps);
    g_assert(ret);
    GST_LOG_OBJECT(vfzfftsimp, "outcaps are %" GST_PTR_FORMAT, outcaps);
    
    /* configure buffer pool */
    GstStructure *config;
    // we need output buffers of blocksize / decimation factor * size per frame
    guint size = (vfzfftsimp->L / vfzfftsimp->D) * GST_AUDIO_INFO_BPF(vfzfftsimp->audio_info_src);
    guint min = 0;
    guint max = 0;
    config = gst_buffer_pool_get_config(vfzfftsimp->buffer_pool);
    g_assert(ret);
    
    GST_LOG_OBJECT(vfzfftsimp, "outcaps are %" GST_PTR_FORMAT, outcaps);
    
    GST_DEBUG_OBJECT (vfzfftsimp, "wants %d", size);
    
    gst_buffer_pool_config_set_params(config, outcaps, size, min, max);
    g_assert(ret);
    ret = gst_buffer_pool_set_config(vfzfftsimp->buffer_pool, config);
    g_assert(ret);
    ret = gst_buffer_pool_set_active(vfzfftsimp->buffer_pool, TRUE);
    g_assert(ret);
    
    gst_caps_unref (outcaps);
    
    return ret;
}

static gboolean
gst_vfzfftsimp_sink_event (GstPad    *pad,
                           GstObject *parent,
                           GstEvent  *event)
{
    gboolean ret;
    GstVfzfftsimp *vfzfftsimp = GST_VFZFFTSIMP(parent);

    switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_CAPS:
        {
            GstCaps *caps;
            gst_event_parse_caps (event, &caps);
            gst_pad_check_reconfigure (vfzfftsimp->srcpad);
            ret = gst_vfzfftsimp_setcaps (vfzfftsimp, caps);
            break;
        }
        case GST_EVENT_EOS:
            /* end-of-stream, we should close down all stream leftovers here */
            // gst_vfzfftsimp_stop_processing (vfzfftsimp);
            
            ret = gst_pad_event_default (pad, parent, event);
            break;
        default:
            /* just call the default handler */
            ret = gst_pad_event_default (pad, parent, event);
            break;
    }
    return ret;
}

gboolean
gst_vfzfftsimp_plugin_init (GstPlugin * plugin)
{
    return gst_element_register (plugin, "vfzfftsimp",
                                 GST_RANK_NONE, GST_TYPE_VFZFFTSIMP);
}
