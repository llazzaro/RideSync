import subprocess
import tempfile
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BenchApplicationTests(unittest.TestCase):
    def test_bus_conflicts_and_optional_imu(self):
        program = r'''
#include "bench_application.h"
#include <cassert>
int main() {
  ridesync::BenchBusPins p;
  p.power_enable=12; p.spi_sck=14; p.spi_miso=2;
  p.spi_mosi=15; p.spi_cs=13; p.modem_tx=26; p.modem_rx=27;
  assert(ridesync::validateBenchBusPins(p));
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
                            str(ROOT / 'src/motion_estimator.cpp'),
                            str(ROOT / 'src/dynamic_motion_estimator.cpp'),
                            str(ROOT / 'src/status_led.cpp'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
