#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include "gstvfzsdr.h"
#include "gstvfzfftsimp.h"
#include "gstvfzssbdemod.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
    if (!gst_vfzssbdemod_plugin_init(plugin))
        return FALSE;
    
    if (!gst_vfzsdr_plugin_init(plugin))
        return FALSE;
    
    if (!gst_vfzfftsimp_plugin_init(plugin))
        return FALSE;
    
    return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, vfzsdr, "FIXME Template plugin", plugin_init, VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")
