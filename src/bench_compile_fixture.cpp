#if defined(ARDUINO_ARCH_ESP32) && defined(RIDESYNC_BENCH_COMPILE_FIXTURE)
#include "bench_application.h"
bool ridesyncPrivateBenchConfig(ridesync::BenchApplicationConfig &) { return false; }
#endif
