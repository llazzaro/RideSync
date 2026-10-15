import subprocess
import tempfile
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BenchApplicationTests(unittest.TestCase):
    def test_power_pins_match_complete_active_reservations(self):
        program = r'''
#include "bench_application.h"
#include <cassert>
using namespace ridesync;
int main() {
  BenchBusPins p;
  p.power_enable=12;p.spi_sck=14;p.spi_miso=2;p.spi_mosi=15;p.spi_cs=13;
  p.modem_tx=26;p.modem_rx=27;p.modem_key=4;
  PowerBackendConfig c;
  assert(validateBenchPowerPins(p,c)); // Default disabled, no power pins claimed.
  p.power_acc=34;assert(!validateBenchPowerPins(p,c));
  p.power_acc=-1;c.enabled=true;
  c.electrical_qualified=c.reservations_complete=c.supply_profile_qualified=true;
  c.independent_cutoff_qualified=c.off_enabled=c.off_qualified=c.off_startup_safe=true;
  c.electrical_profile="synthetic";c.provenance="fixture";c.independent_timer_ms=7000;
  c.acc.pin=34;c.power_fail.pin=39;c.reserve_ready.pin=23;c.off_pin=32;
  c.acc.polarity=c.power_fail.polarity=c.reserve_ready.polarity=c.off_polarity=PowerPolarity::ActiveHigh;
  c.acc.pull=c.power_fail.pull=c.reserve_ready.pull=PowerExternalPull::Down;
  p.power_acc=34;p.power_fail=39;p.power_ready=23;p.power_off=32;
  assert(!validateBenchPowerPins(p,c)); // Claim "complete" cannot omit active bus pins.
  for(int pin : {12,14,2,15,13,26,27,4})c.reserved_pins|=uint64_t(1)<<pin;
  assert(validateBenchPowerPins(p,c));
  p.mark_button=19;assert(!validateBenchPowerPins(p,c));
  c.reserved_pins|=uint64_t(1)<<19;assert(validateBenchPowerPins(p,c));
  p.mark_button=34;assert(!validateBenchBusPins(p));
  p.mark_button=-1;c.reserved_pins&=~(uint64_t(1)<<19);
  auto solar=c;auto solar_pins=p;
  solar.acc.pin=36;solar.reserve_ready.pin=23;
  solar_pins.power_acc=36;solar_pins.power_ready=23;
  assert(!validateBenchPowerPins(solar_pins,solar)); // Hardwired SOLAR_IN cannot become digital ACC.
  for(int pin : {12,14,2,15,13,26,27,4}) {
    auto incomplete=c;incomplete.reserved_pins&=~(uint64_t(1)<<pin);
    assert(!validateBenchPowerPins(p,incomplete));
  }
  p.led[0]=32;assert(!validateBenchBusPins(p));assert(!validateBenchPowerPins(p,c));
  p.led[0]=18;assert(!validateBenchPowerPins(p,c));c.reserved_pins|=uint64_t(1)<<18;
  assert(validateBenchPowerPins(p,c));
  p.imu_enabled=true;p.i2c_sda=21;p.i2c_scl=22;p.imu_interrupt=23;
  assert(!validateBenchBusPins(p)); // IRQ23 deliberately conflicts with power-ready23.
  assert(!validateBenchPowerPins(p,c));
  p.imu_interrupt=19; // Separate synthetic IRQ pin in the non-conflicting baseline.
  assert(!validateBenchPowerPins(p,c));
  for(int pin : {21,22,19})c.reserved_pins|=uint64_t(1)<<pin;
  assert(validateBenchPowerPins(p,c));
  p.imu_interrupt=34;assert(!validateBenchBusPins(p));assert(!validateBenchPowerPins(p,c));
  p.imu_interrupt=19;p.power_ready=39;assert(!validateBenchBusPins(p));
  p.power_ready=23;p.power_off=34;assert(!validateBenchBusPins(p));
  p.power_off=32;p.button=34;assert(!validateBenchPowerPins(p,c));
  p.imu_interrupt=-1;c.reserved_pins&=~(uint64_t(1)<<19); // Retire IRQ before reusing19 as button.
  p.button=19;assert(!validateBenchPowerPins(p,c));c.reserved_pins|=uint64_t(1)<<19;
  assert(validateBenchPowerPins(p,c));
  c.off_enabled=false;c.off_pin=-1;c.off_polarity=PowerPolarity::Unspecified;p.power_off=-1;
  assert(validateBenchPowerPins(p,c));
  p.power_off=32;assert(!validateBenchPowerPins(p,c));
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'check.cpp'
            binary = Path(directory) / 'check'
            source.write_text(program)
            subprocess.run(['c++', '-std=c++11', '-I', str(ROOT / 'include'),
                            str(source), str(ROOT / 'src/bench_application.cpp'),
                            str(ROOT / 'src/power_shutdown_esp32.cpp'),
                            str(ROOT / 'src/motion_estimator.cpp'),
                            str(ROOT / 'src/dynamic_motion_estimator.cpp'),
                            str(ROOT / 'src/status_led.cpp'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_bus_conflicts_and_optional_imu(self):
        program = r'''
#include "bench_application.h"
#include <cassert>
int main() {
  ridesync::BenchBusPins p;
  p.power_enable=12; p.spi_sck=14; p.spi_miso=2;
  p.spi_mosi=15; p.spi_cs=13; p.modem_tx=26; p.modem_rx=27;
  assert(ridesync::validateBenchBusPins(p));
  p.mark_button=19; assert(ridesync::validateBenchBusPins(p));
  p.button=19; assert(!ridesync::validateBenchBusPins(p));
  p.button=32; assert(ridesync::validateBenchBusPins(p));
  p.mark_button=14; assert(!ridesync::validateBenchBusPins(p));
  p.mark_button=18; p.led[0]=18; assert(!ridesync::validateBenchBusPins(p));
  p.led[0]=-1; p.power_acc=18; assert(!ridesync::validateBenchBusPins(p));
  p.power_acc=-1; p.mark_button=-1;
  p.button=14; assert(!ridesync::validateBenchBusPins(p));
  p.button=-1; p.modem_key=14;
  assert(!ridesync::validateBenchBusPins(p));
  p.modem_key=4; assert(ridesync::validateBenchBusPins(p));
  p.imu_enabled=true;
  assert(!ridesync::validateBenchBusPins(p));
  p.i2c_sda=21; p.i2c_scl=22;
  assert(ridesync::validateBenchBusPins(p));
  p.i2c_scl=21; assert(!ridesync::validateBenchBusPins(p));
  p.i2c_scl=22; p.spi_sck=6;
  assert(!ridesync::validateBenchBusPins(p));
  p.spi_sck=14; p.led[0]=26;
  assert(!ridesync::validateBenchBusPins(p));
  ridesync::MotionEstimatorConfig m;
  m.mount_qualified=m.residual_calibration_qualified=true;
  m.mount_id=7; m.calibration_id=8;
  m.convention=ridesync::MotionCalibrationConvention::ResidualCountsOffsetThenGain;
  m.accel_compensation=m.gyro_compensation=1;
  ridesync::BenchDynamicReference reference;
  reference.configure(m);
  ridesync::ImuEvidence e;
  assert(reference.referenceFor(e).declaration==0); // No implicit boot declaration.
  assert(reference.arm(0)); assert(!reference.arm(0));
  e.kind=ridesync::RecordKind::ImuControl;
  assert(reference.referenceFor(e).declaration==0);
  e.kind=ridesync::RecordKind::ImuSample;
  assert(reference.referenceFor(e).declaration==0); // Invalid calibration does not consume.
  e.session_id=1; e.receipt_known=true;
  auto &c=e.config;
  c.generation=c.sensor_id=1; c.mount_id=7; c.calibration_id=8;
  c.sensor_state=c.mount_state=c.calibration_state=ridesync::Qualification::Qualified;
  c.calibration_offsets_known=c.calibration_gains_known=true;
  c.accel_offset_compensation=c.gyro_offset_compensation=1;
  c.accel_scale_numerator=1; c.accel_scale_denominator=2048;
  c.gyro_scale_numerator=125; c.gyro_scale_denominator=2048;
  for(unsigned i=0;i<3;++i){c.accel_gain_numerator[i]=c.accel_gain_denominator[i]=1;
    c.gyro_gain_numerator[i]=c.gyro_gain_denominator[i]=1;}
  e.accel[2]=2048;
  e.timing_flags=1; assert(reference.referenceFor(e).declaration==0);
  e.timing_flags=0;
  auto first=reference.referenceFor(e);
  assert(first.externally_stationary && first.declaration==1);
  assert(reference.referenceFor(e).declaration==0);
  assert(reference.arm(0));
  assert(reference.referenceFor(e).declaration==2);
  assert(reference.arm(100));
  e.receipt_millis32=99; assert(reference.referenceFor(e).declaration==0);
  e.receipt_millis32=100; e.kind=ridesync::RecordKind::ImuControl;
  assert(reference.referenceFor(e).declaration==0); e.kind=ridesync::RecordKind::ImuSample;
  e.receipt_millis32=101; assert(reference.referenceFor(e).declaration==3);
  assert(reference.arm(200));
  e.receipt_millis32=1200; assert(reference.referenceFor(e).declaration==4); // Inclusive expiry.
  assert(reference.arm(1300));
  e.receipt_millis32=2301; assert(reference.referenceFor(e).declaration==0);
  e.receipt_millis32=2302; assert(reference.referenceFor(e).declaration==0); // No late replay.
  assert(reference.arm(3000)); assert(!reference.arm(4000));
  assert(reference.arm(4001)); // A fresh explicit command may replace an expired declaration.
  e.receipt_millis32=4001; assert(reference.referenceFor(e).declaration==7);
  assert(reference.arm(0xfffffff0u));
  e.receipt_millis32=0xffffffe0u; assert(reference.referenceFor(e).declaration==0);
  e.receipt_millis32=20; assert(reference.referenceFor(e).declaration==8); // Clock wrap.
  ridesync::MotionAdmissionConfig options;
  options.requested=options.imu_qualified=true;
  options.estimator=m; options.snapshot_max_age_ms=1000;
  assert(ridesync::validateBenchMotionMetadata(options,e.config));
  e.config.calibration_id=9;
  assert(!ridesync::validateBenchMotionMetadata(options,e.config));
  e.config.calibration_id=8; e.config.calibration_gains_known=false;
  assert(!ridesync::validateBenchMotionMetadata(options,e.config));
  e.config.calibration_gains_known=true; e.config.gyro_gain_denominator[1]=0;
  assert(!ridesync::validateBenchMotionMetadata(options,e.config));
  e.config.gyro_gain_denominator[1]=1; e.config.mount_state=ridesync::Qualification::Unknown;
  assert(!ridesync::validateBenchMotionMetadata(options,e.config));
  e.config.mount_state=ridesync::Qualification::Qualified;
  options.estimator.accel_compensation=2;
  assert(!ridesync::validateBenchMotionMetadata(options,e.config));
  options.estimator.accel_compensation=1;
  options.dynamic.enabled=true; options.dynamic_cadence_us=5000;
  assert(!ridesync::validateBenchMotionMetadata(options,e.config)); // Reference absent.
  options.dynamic_reference=&reference;
  assert(ridesync::validateBenchMotionMetadata(options,e.config));
  ridesync::LedWiring led;
  led.mode=ridesync::LedMode::Mono; led.board_qualified=led.reservations_complete=true;
  led.pins[0]=18;
  assert(!ridesync::validateLedWiring(led,true));
  led.polarity[0]=ridesync::LedPolarity::ActiveHigh;
  assert(ridesync::validateLedWiring(led,true));
  led.pins[1]=19; assert(!ridesync::validateLedWiring(led,true));
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'check.cpp'
            binary = Path(directory) / 'check'
            source.write_text(program)
            subprocess.run(['c++', '-std=c++11', '-I', str(ROOT / 'include'),
                            str(source), str(ROOT / 'src/bench_application.cpp'),
                            str(ROOT / 'src/power_shutdown_esp32.cpp'),
                            str(ROOT / 'src/motion_estimator.cpp'),
                            str(ROOT / 'src/dynamic_motion_estimator.cpp'),
                            str(ROOT / 'src/status_led.cpp'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
