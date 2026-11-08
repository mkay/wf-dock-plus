util = static_library(
  'util',
  [
    'css-config.cpp',
    'gtk-utils.cpp',
    'wf-autohide-window.cpp',
    'wf-shell-app.cpp',
  ],
  dependencies: [
    wf_protos,
    gtklayershell,
    wayland_client,
    gtkmm,
    wfconfig,
    libinotify,
  ],
)

util_includes = include_directories('.')
libutil = declare_dependency(link_with: util, include_directories: util_includes)
