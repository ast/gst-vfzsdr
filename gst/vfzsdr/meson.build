vfzsdr_sources = [
  'dsputils.c',
  'gstvfzfftsimp.c',
  'gstvfzsdr.c',
  'gstvfzssbdemod.c',
  'gstvfzsdrplugin.c',
]

# deps for this plugin
ne10_dep   = cc.find_library('NE10', required : false)
liquid_dep = cc.find_library('liquid', required : true)

gstvfzsdr = library('gstvfzsdr',
  vfzsdr_sources,
  c_args : gst_test_args,
  include_directories : [configinc, libsinc],
  dependencies : [gst_dep, gstaudio_dep, ne10_dep, liquid_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
