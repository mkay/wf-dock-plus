project(
  'wf-dock-plus',
  'c',
  'cpp',
  version: '0.1.0',
  license: 'MIT',
  meson_version: '>=0.58.0',
  default_options: [
    'cpp_std=c++17',
    'c_std=c11',
    'warning_level=2',
    'werror=false',
  ],
)

wayland_client = dependency('wayland-client')
gtkmm = dependency('gtkmm-4.0', version: '>=4.12')
wfconfig = dependency('wf-config', version: '>=0.7.0')
gtklayershell = dependency('gtk4-layer-shell-0', version: '>=1.3.0', required: false)
if not gtklayershell.found()
  gtklayershell = dependency('gtk4-layer-shell', fallback: ['gtk4-layer-shell'])
endif

needs_libinotify = ['freebsd', 'dragonfly'].contains(host_machine.system())
libinotify = dependency('libinotify', required: needs_libinotify)

add_project_arguments(
  ['-Wno-pedantic', '-Wno-unused-parameter', '-Wno-parentheses'],
  language: 'cpp',
)

# The dock's own data. Its WCM page goes next to wf-shell's, since WCM saves
# the pages in that directory to wf-shell.ini.
resource_dir = get_option('prefix') / get_option('datadir') / 'wf-dock-plus'
metadata_dir = get_option('prefix') / get_option('datadir') / 'wf-shell' / 'metadata'
sysconf_dir = get_option('prefix') / get_option('sysconfdir')

add_project_arguments(
  [
    '-DDOCK_SECTION="dock-plus"',
    '-DRESOURCEDIR="' + resource_dir + '"',
    '-DMETADATA_DIR="' + metadata_dir + '"',
    '-DSYSCONF_DIR="' + sysconf_dir + '"',
  ],
  language: 'cpp',
)

install_data('metadata/wf-dock-plus.xml', install_dir: metadata_dir)
install_data('data/default.css', install_dir: resource_dir / 'css')
# WCM looks for page icons in Wayfire's icon directory before its own
install_data('data/icons/plugin-dock-plus.svg',
  install_dir: get_option('prefix') / get_option('datadir') / 'wayfire' / 'icons')

subdir('proto')
subdir('src')
