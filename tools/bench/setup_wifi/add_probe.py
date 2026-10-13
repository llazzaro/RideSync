from SCons.Script import Import

Import("env")
env = globals()["env"]
env.BuildSources("$BUILD_DIR/setup_wifi_probe", "$PROJECT_DIR/tools/bench/setup_wifi", "+<sdk_probe.cpp>")
