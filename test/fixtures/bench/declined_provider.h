#pragma once
#include "bench_application.h"
// Compile/link test only: no identity, store proof, namespace or enabled device.
// Body is in another translation unit so the complete private startup branch
// remains linkable and measurable. It always declines at runtime.
bool ridesyncPrivateBenchConfig(ridesync::BenchApplicationConfig &);
