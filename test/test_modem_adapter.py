"""Compile and exercise opt-in Arduino adapter with host-only hardware callbacks."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ModemAdapterTest(unittest.TestCase):
    def test_qualified_pins_and_write_capacity_are_required(self):
        code = r'''
#include "modem_arduino.h"
#include <cassert>
int pin_calls = 0;
int levels[40] = {};
int main() {
  using namespace ridesync;
  QualifiedModemPins pins;
  HardwareSerial serial;
  ArduinoModemUart uart(serial);
  assert(!uart.begin(pins));
  assert(serial.begins == 0 && uart.write("AT\r",3) == 0);
  ArduinoGnssPower blocked(pins);
  assert(!blocked.begin() && pin_calls == 0);
  pins.pins_qualified = true;
  pins.documentary_profile_opt_in = true;
  pins.tx = 18; pins.rx = 19; pins.supply = 21; pins.key = 22;
  pins.baud = 115200;
  pins.supply_active_high = true; pins.key_active_high = false;
  assert(uart.begin(pins) && serial.begins == 1);
  assert(uart.write("12345",5) == 0 && serial.writes == 0);
  assert(uart.write("AT\r",3) == 3 && serial.writes == 1);
  ArduinoGnssPower power(pins);
  assert(power.begin());
  assert(levels[21] == 0 && levels[22] == 1);
  power.enableSupply(); power.key(true);
  assert(levels[21] == 1 && levels[22] == 0);
  power.key(false); assert(levels[22] == 1);
  ArduinoGnssPower retained(pins);
  assert(retained.begin(true));
  assert(levels[21] == 1 && levels[22] == 1);
  retained.key(true); assert(levels[21] == 1 && levels[22] == 0);
  retained.key(false); assert(levels[21] == 1 && levels[22] == 1);
  pins.key = pins.tx;
  ArduinoGnssPower conflict(pins);
  int before = pin_calls;
  assert(!conflict.begin() && pin_calls == before);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "adapter.cpp"
            executable = Path(folder) / "adapter"
            source.write_text(code)
            subprocess.run([
                "c++", "-std=c++11", "-DARDUINO", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "test/fixtures/arduino"), "-I", str(ROOT / "include"),
                str(source), "-o", str(executable),
            ], check=True, capture_output=True)
            result = subprocess.run([str(executable)], capture_output=True)
            self.assertEqual(0, result.returncode, result.stderr.decode())
