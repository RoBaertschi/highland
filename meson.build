project(
  'highland',
  ['cpp', 'c'],
  version : '0.1',
  meson_version : '>= 1.3.0',
  default_options : ['warning_level=3', 'cpp_std=c++11'],
)

dependencies = [
  dependency('wayland-server', required: true),
  dependency('libudev', required: true),
  dependency('libdrm', required: true),
  dependency('libseat', required: true),
]

wayland_scanner = find_program('wayland-scanner', required: true)
wayland_protocols = dependency('wayland-protocols', required: true)
wayland_protocols_datadir = wayland_protocols.get_variable(
  pkgconfig: 'pkgdatadir',
)

wayland_protocols_xml = [
  ['xdg-shell', join_paths('stable', 'xdg-shell', 'xdg-shell.xml')],
]

generated_protocol_sources = []

foreach p : wayland_protocols_xml
  protocol_name = p[0]
  protocol_xml = join_paths(wayland_protocols_datadir, p[1])

  protocol_h = custom_target(
    '@0@-protocol-h'.format(protocol_name),
    input: protocol_xml,
    output: '@0@-protocol.h'.format(protocol_name),
    command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
  )

  protocol_c = custom_target(
    '@0@-protocol-c'.format(protocol_name),
    input: protocol_xml,
    output: '@0@-protocol.c'.format(protocol_name),
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
    depends: protocol_h,
  )

  generated_protocol_sources += protocol_c
endforeach

sources = [
  'src/highland.cpp',
] + generated_protocol_sources

exe = executable(
  'highland',
  [sources],
  install : true,
  dependencies : dependencies,
  include_directories: include_directories('.'),
)

test('basic', exe)
