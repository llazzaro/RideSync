"""Exercise the Arduino adapter's side effects with a host GPIO substitute."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class ButtonGpioTest(unittest.TestCase):
    def test_no_gpio_access_before_qualified_opt_in(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            (work / "Arduino.h").write_text("""
#pragma once
#define INPUT 0
#define INPUT_PULLUP 1
#define INPUT_PULLDOWN 2
#define LOW 0
extern int modes, reads, last_pin, last_mode, level;
inline void pinMode(int p, int m) { ++modes; last_pin=p; last_mode=m; }
inline int digitalRead(int p) { ++reads; last_pin=p; return level; }
""")
            (work / "test.cpp").write_text("""
#include "button_manager.h"
#include <cassert>
int modes=0, reads=0, last_pin=-1, last_mode=-1, level=0;
int main() {
  ridesync::ArduinoButtonInput input;
  ridesync::ButtonGpioConfig c;
  assert(!input.pressed()); assert(reads==0);
  assert(!input.begin(c,true)); assert(modes==0);
  c.enabled=true; c.board_qualified=true; c.pin=32;
  c.pull=ridesync::ButtonPull::Up;
  assert(!input.begin(c,false)); assert(modes==0);
  c.pin=6; assert(!input.begin(c,true)); assert(modes==0);
  c.pin=34; assert(!input.begin(c,true)); assert(modes==0);
  c.pin=32; assert(input.begin(c,true));
  assert(modes==1 && last_pin==32 && last_mode==1);
  assert(input.pressed()); assert(reads==1 && last_pin==32);
  level=1; assert(!input.pressed());
  c.active_low=false; assert(input.begin(c,true)); assert(input.pressed());
  c.enabled=false; assert(!input.begin(c,true));
  int before=reads; assert(!input.pressed()); assert(reads==before);
}
""")
            subprocess.run([
                "c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                "-DARDUINO_ARCH_ESP32", "-I", str(work), "-I", str(ROOT / "include"),
                str(ROOT / "src/button_manager.cpp"), str(work / "test.cpp"),
                "-o", str(work / "check"),
            ], check=True)
            subprocess.run([str(work / "check")], check=True)


if __name__ == "__main__":
    unittest.main()
