# Executable stubs with version information for the `app_quirks` option,
# generated from `daw_quirks` in `src/common/daw-quirks.h`. The Wine plugin
# host presents itself as these files to plugins that identify the DAW through
# the process they're loaded into, see `src/plugin/app-quirks.cpp`. Install
# the generated `quirks` directory next to `yabridge-host.exe.so`.

make_version_stubs = executable(
  'make-version-stubs',
  'make-version-stubs.cpp',
  native : true,
  install : false,
)

custom_target(
  'quirk-stubs',
  output : 'quirks',
  command : [make_version_stubs, '@OUTPUT@'],
  build_by_default : true,
)
