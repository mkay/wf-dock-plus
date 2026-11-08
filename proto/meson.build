wayland_scanner = find_program('wayland-scanner')

wayland_scanner_code = generator(
	wayland_scanner,
	output: '@BASENAME@-protocol.c',
	arguments: ['private-code', '@INPUT@', '@OUTPUT@'],
)

wayland_scanner_client = generator(
	wayland_scanner,
	output: '@BASENAME@-client-protocol.h',
	arguments: ['client-header', '@INPUT@', '@OUTPUT@'],
)

# wayfire-shell-unstable-v2.xml is a copy of the file Wayfire installs
# (MIT, https://github.com/WayfireWM/wayfire), bundled so the build doesn't
# need Wayfire. Update it by hand when Wayfire changes the protocol.
client_protocols = [
	'wlr-foreign-toplevel-management-unstable-v1.xml',
	'wayfire-shell-unstable-v2.xml',
]

wl_protos_client_src = []
wl_protos_headers = []

foreach xml : client_protocols
	wl_protos_client_src += wayland_scanner_code.process(xml)
	wl_protos_headers += wayland_scanner_client.process(xml)
endforeach

lib_wl_protos = static_library('wl_protos', wl_protos_client_src + wl_protos_headers,
	dependencies: [wayland_client]) # for the include directory

wf_protos = declare_dependency(
	link_with: lib_wl_protos,
	sources: wl_protos_headers,
)
