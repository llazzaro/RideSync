#ifndef RIDESYNC_GNSS_FIXTURES_H
#define RIDESYNC_GNSS_FIXTURES_H
// Public SIMCom V1.09 example, printed p.491, NOT a local hardware fix.
static const char kManualCgpsinfo[] =
    "+CGPSINFO:3113.343286,N,12121.234064,E,250311,072809.33,44.1,0.0,0";
// Observed factory-firmware probe 2026-10-07, no fix, no private location.
static const char kObservedCgpsinfoNoFix[] = "+CGPSINFO: ,,,,,,,,";
static const char kObservedCgnssinfoNoFix[] = "+CGNSSINFO: ,,,,,,,,";
#endif
