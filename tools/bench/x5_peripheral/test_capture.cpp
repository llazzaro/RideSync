#include "capture.h"
#include "protocol/insta360_codec.h"
#include <cassert>
#include <cstring>

using namespace x5_probe;
int main() {
  const uint8_t expected[] = {0xfc, 0xef, 0xfe, 0x86, 0x00, 0x03, 0x01, 0x02, 0x00};
  const auto shutter_bytes = ridesync::insta360::encodeShutterEvent();
  assert(shutter_bytes.size() == sizeof(expected));
  assert(std::memcmp(shutter_bytes.data(), expected, sizeof(expected)) == 0);
  {
    Capture capture;
    SdkControl sdk;
    ShutterControl shutter;
    assert(!shutter.request(capture, sdk, 0)); // Idle is capture-only.
    assert(capture.begin(0));
    capture.ready();
    assert(!shutter.request(capture, sdk, 1)); // No peer.
    shutter.connected(7);
    assert(!shutter.request(capture, sdk, 2)); // No CE82 notify subscription.
    shutter.subscription(8, 18, true);         // Wrong peer cannot grant admission.
    assert(!shutter.request(capture, sdk, 3));
    shutter.subscription(7, 18, true);
    assert(shutter.request(capture, sdk, 4));
    assert(!shutter.request(capture, sdk, 5)); // One bounded pending slot.
    uint16_t conn = 0xffff, attr = 0;
    assert(shutter.admit(capture, sdk, 6, conn, attr));
    assert(conn == 7 && attr == 18 && sdk.snapshot().action == SdkAction::Shutter);
    assert(!shutter.request(capture, sdk, 7)); // SDK attempt still in flight.
    sdk.complete(19);                          // Ambiguous/error return never queues a retry.
    assert(!shutter.admit(capture, sdk, 8, conn, attr));
    assert(sdk.snapshot().accepted == 1 && sdk.snapshot().returned == 1);
    assert(shutter.request(capture, sdk, 9)); // Only a fresh explicit S may try again.
    assert(shutter.admit(capture, sdk, 10, conn, attr));
    sdk.complete(0);
    assert(!shutter.admit(capture, sdk, 11, conn, attr));
  }
  for (unsigned scenario = 0; scenario < 4; ++scenario) {
    Capture capture;
    SdkControl sdk;
    ShutterControl shutter;
    assert(capture.begin(0));
    capture.ready();
    shutter.connected(7);
    shutter.subscription(7, 18, true);
    assert(shutter.request(capture, sdk, 1));
    uint32_t submission = 2;
    if (scenario == 0) {
      capture.stop(StopReason::Requested);
      sdk.requestStop();
    } else if (scenario == 1)
      submission = Capture::kWindowMs;
    else if (scenario == 2)
      shutter.disconnected();
    else
      shutter.subscription(7, 18, false);
    uint16_t conn = 0xffff, attr = 0;
    assert(!shutter.admit(capture, sdk, submission, conn, attr));
    assert(!shutter.pending() && sdk.snapshot().accepted == 0);
    assert(!shutter.admit(capture, sdk, submission, conn, attr));
  }
  {
    Capture capture;
    SdkControl sdk;
    ShutterControl shutter;
    assert(capture.begin(0));
    capture.ready();
    shutter.connected(7);
    shutter.subscription(7, 18, true);
    assert(shutter.request(capture, sdk, 1));
    shutter.cancel(); // Allocation failure or synchronous X cannot replay later.
    uint16_t conn, attr;
    assert(!shutter.admit(capture, sdk, 2, conn, attr));
    assert(shutter.request(capture, sdk, 3));
    assert(shutter.admit(capture, sdk, 4, conn, attr));
    capture.stop(StopReason::Requested);
    sdk.requestStop();
    assert(sdk.snapshot().in_flight && sdk.snapshot().stop_requested);
    sdk.complete(0); // Accepted-before-stop work remains honest, not retro-cancelled.
    assert(!shutter.request(capture, sdk, 5));
  }
  {
    // A cancelled/late sync may not even begin preparation; no stop is lost
    // behind a startup operation whose return publication arrives afterwards.
    Capture capture;
    SdkControl sdk;
    assert(capture.begin(0) && sdk.begin(SdkAction::Startup));
    capture.stop(StopReason::Requested);
    sdk.requestStop();
    sdk.complete(17);
    assert(!sdk.admitPreparation(capture, 1));
    assert(sdk.snapshot().last_returned_action == SdkAction::Startup);
    assert(sdk.snapshot().last_status == 17 && sdk.snapshot().stop_requested);
  }
  {
    // Cancellation during preparation must reject a new advertising submission.
    Capture capture;
    SdkControl sdk;
    assert(capture.begin(100));
    assert(sdk.admitPreparation(capture, 101));
    capture.stop(StopReason::Requested);
    sdk.requestStop();
    sdk.complete(0); // Preparation returns after X, not camera/SDK success proof.
    assert(sdk.admitAdvertising(capture, 102) == 0);
    assert(sdk.snapshot().stop_requested && !sdk.snapshot().in_flight);
  }
  {
    // Fresh time at submission catches startup expiry while preparation stalls.
    Capture capture;
    SdkControl sdk;
    assert(capture.begin(0));
    assert(sdk.admitPreparation(capture, 14999));
    sdk.complete(0);
    assert(sdk.admitAdvertising(capture, 15000) == 0);
    assert(capture.reason() == StopReason::StartupTimeout && sdk.snapshot().stop_requested);
  }
  {
    // The current remaining duration, not the sync-entry snapshot, is admitted.
    Capture capture;
    SdkControl sdk;
    assert(capture.begin(1000));
    assert(sdk.admitPreparation(capture, 1001));
    sdk.complete(0);
    assert(sdk.admitAdvertising(capture, 11000) == 110000);
    assert(sdk.snapshot().in_flight && sdk.snapshot().action == SdkAction::Advertise);
    // Already admitted work can still be pending when X arrives. Keep that
    // publication until the owner returns, and retain the compensating stop.
    capture.stop(StopReason::Requested);
    sdk.requestStop();
    assert(sdk.snapshot().in_flight && sdk.snapshot().stop_requested);
    sdk.complete(0);
    capture.ready();
    assert(!capture.active() && sdk.snapshot().stop_requested);
    assert(sdk.snapshot().accepted == 2 && sdk.snapshot().returned == 2);
  }
  {
    // Window expiry between preparation and submission cannot restart work.
    Capture capture;
    SdkControl sdk;
    assert(capture.begin(0));
    capture.ready();
    assert(sdk.admitPreparation(capture, 119999));
    sdk.complete(0);
    assert(sdk.admitAdvertising(capture, 120000) == 0);
    assert(capture.reason() == StopReason::Deadline);
  }
  for (unsigned scenario = 0; scenario < 3; ++scenario) {
    // Never returning cleanup cannot consume/hold control admission, deadlines
    // or event reporting. These are policy schedules, not fake SDK executions.
    Capture capture;
    SdkControl sdk;
    assert(capture.begin(0));
    if (scenario == 0)
      capture.stop(StopReason::Requested);
    else if (scenario == 1)
      capture.tick(15000);
    else {
      capture.ready();
      capture.tick(120000);
    }
    sdk.requestStop();
    assert(sdk.begin(SdkAction::StopAdvertising));
    assert(!sdk.begin(SdkAction::Terminate)); // Single fixed in-flight publication.
    for (unsigned i = 0; i < 20; ++i) {
      capture.tick(200000 + i * 1000);
      sdk.requestStop(); // Repeated nonblocking admission never clears the latch.
      assert(!capture.active() && sdk.snapshot().in_flight);
      assert(capture.push(Kind::Stop, 200000 + i * 1000, 0xffff, 0, int(capture.reason())));
      char line[768];
      const size_t length = formatEvent(*capture.front(), false, line, sizeof(line));
      assert(canReport(length, sizeof(line)));
      capture.pop();
    }
    assert(capture.stats().reported == 20 && sdk.snapshot().returned == 0);
    assert(sdk.snapshot().stop_requested && !capture.begin(300000));
  }
  {
    Capture capture;
    capture.tick(900000);
    assert(!capture.used() && !capture.active());
    assert(capture.begin(100));
    assert(!capture.begin(101));
    capture.tick(15099);
    assert(capture.active());
    capture.tick(15100);
    assert(!capture.active() && capture.reason() == StopReason::StartupTimeout);
    capture.ready();
    assert(!capture.active() && !capture.begin(20000));
  }
  {
    Capture capture;
    assert(capture.begin(0xffffff00u));
    capture.ready();
    capture.tick(uint32_t(0xffffff00u + 119999u));
    assert(capture.active());
    capture.tick(uint32_t(0xffffff00u + 120000u));
    assert(!capture.active() && capture.reason() == StopReason::Deadline);
  }
  {
    Capture capture;
    capture.stop(StopReason::Requested);
    assert(!capture.used()); // X before A does not consume the trial.
    assert(capture.begin(0));
    capture.ready();
    capture.stop(StopReason::Requested);
    assert(!capture.active() && !capture.begin(1));
    capture.stop(StopReason::Error);
    assert(capture.reason() == StopReason::Requested);
  }
  {
    Capture capture;
    uint8_t ingress[300];
    std::memset(ingress, 0xa5, sizeof(ingress));
    assert(capture.push(Kind::Write, 123456, 7, 9, 0, ingress, sizeof(ingress)));
    ingress[0] = 0; // Queued evidence owns a bounded byte copy.
    const Event *event = capture.front();
    assert(event && event->sequence == 1 && event->time_ms == 123456);
    assert(event->connection == 7 && event->attribute == 9 && event->length == 300);
    assert(event->copied == 256 && event->bytes[0] == 0xa5 && event->bytes[255] == 0xa5);
    assert(capture.stats().truncated == 1);
    char line[768];
    size_t length = formatEvent(*event, false, line, sizeof(line));
    assert(length && std::strstr(line, "len=300 copied=256"));
    assert(!std::strstr(line, "hex=") && !std::strstr(line, "a5a5"));
    assert(!canReport(length, length - 1) && canReport(length, length));
    assert(formatEvent(*event, false, line, length) == 0); // Includes newline/NUL bounds.
    length = formatEvent(*event, true, line, sizeof(line));
    assert(length && std::strstr(line, "SENSITIVE") && std::strstr(line, "hex_bytes=256 hex="));
    assert(std::strstr(line, "a5a5"));
    capture.pop();
    assert(!capture.front() && capture.stats().reported == 1);
  }
  {
    Capture capture;
    for (unsigned i = 0; i < 32; ++i)
      assert(capture.push(Kind::Read, i, 1, 3, 0));
    assert(!capture.push(Kind::Disconnect, 100, 1, 0, 19));
    assert(capture.stats().seen == 33 && capture.stats().dropped == 1);
    assert(capture.stats().by_kind[unsigned(Kind::Disconnect)] == 1);
    for (unsigned i = 0; i < 32; ++i) {
      assert(capture.front() && capture.front()->sequence == i + 1);
      capture.pop();
    }
    assert(!capture.front());
    assert(capture.push(Kind::Mtu, 101, 2, 0, 247));
    assert(capture.front()->sequence == 34 && capture.front()->value == 247);
    assert(capture.stats().reported == 32);
  }
}
