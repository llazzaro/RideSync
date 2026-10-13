#include "bench_application.h"
#include "bench_console.h"
#include <Arduino.h>
#include <cassert>
using namespace ridesync;
FakeSerial Serial;
namespace ridesync {
SupervisedEsp32Application *application = nullptr;
unsigned stationary = 0, marks = 0, camera_commands = 0;
} // namespace ridesync
void pump(unsigned times = 200) {
  for (unsigned i = 0; i < times; ++i) {
    Serial.room = 5;
    benchConsoleService();
  }
}
int main() {
  SupervisedEsp32Application app;
  application = &app;
  // No transition generation: disabled service must stay quiet.
  pump();
  assert(Serial.output.empty() && Serial.blocking == 0);
  // Default-off power policy must preserve the actual parser/camera dispatch path.
  FakeTelemetry telemetry;
  app.t = &telemetry;
  assert(!app.s.policy.enabled && !app.s.policy.sealed && !app.s.policy.admission_allowed);
  Serial.input = "REC\n";
  benchConsoleService();
  assert(camera_commands == 1);
  pump();
  app.t = nullptr;
  Serial.output.clear();
  Serial.blocking = 0;
  Serial.input = "STATUS\n";
  Serial.room = 5;
  benchConsoleService();
  assert(Serial.blocking == 0);
  pump();
  assert(Serial.output.find("available=1") != std::string::npos && Serial.blocking == 2);
  Serial.output.clear();
  Serial.blocking = 0;
  app.s.policy.enabled = true;
  Serial.input = "STATUS\n";
  Serial.room = 0;
  benchConsoleService();
  pump();
  assert(Serial.blocking == 0 && Serial.output.find("available=1") != std::string::npos);
  app.s.policy.enabled = false;
  Serial.output.clear();
  Serial.input = "SHUTDOWN\n";
  Serial.room = 0;
  benchConsoleService();
  assert(app.requests == 1 && app.telemetry() == nullptr && Serial.output.empty());
  assert(Serial.blocking == 0);
  pump();
  assert(Serial.output.find("cause=8") != std::string::npos);
  auto bytes = Serial.output.size();
  pump();
  assert(Serial.output.size() == bytes); // Once per generation.
  for (const char *cmd : {"CLEAR\n", "STATIONARY\n", "MARK\n", "REC\n", "BOGUS\n"}) {
    Serial.input = cmd;
    Serial.room = 5;
    benchConsoleService();
    pump();
  }
  assert(stationary == 0 && marks == 0 && !benchConsoleTakeClear() && Serial.blocking == 0);
  Serial.input = "STATUS\n";
  benchConsoleService();
  pump();
  assert(Serial.output.size() > bytes);
  assert(Serial.blocking == 0); // Sealed STATUS does not use blocking ordinary printf report.
  app.s.generation = 2;
  Serial.room = 0;
  benchConsoleService();
  app.s.generation = 3;
  app.s.storage.lost = 8;
  app.s.observation.reserve = PowerReserve::NotReady;
  app.s.storage_error = -74;
  Serial.room = 0;
  benchConsoleService();
  Serial.output.clear();
  pump();
  assert(Serial.output.find("generation=3") != std::string::npos);
  assert(Serial.output.find("generation=2") == std::string::npos &&
         Serial.output.find("lost=8") != std::string::npos);
  assert(Serial.output.find("reserve_validity=1") != std::string::npos &&
         Serial.output.find("storage_error=-74") != std::string::npos);
  Serial.output.clear();
  app.s.generation = 4;
  Serial.short_limit = 0;
  Serial.room = 10;
  benchConsoleService();
  assert(Serial.output.empty());
  Serial.short_limit = 2;
  pump(400);
  assert(Serial.output.find("generation=4") != std::string::npos && Serial.output.back() == '\n');
  Serial.output.clear();
  app.s.generation = 5;
  Serial.room = -1;
  benchConsoleService();
  assert(Serial.output.empty());
  Serial.short_limit = SIZE_MAX;
  pump();
  assert(Serial.output.find("generation=5") != std::string::npos);
}
