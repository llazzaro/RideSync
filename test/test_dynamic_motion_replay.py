"""Independent trajectory and failure checks through the actual replay executable."""
import csv
import io
import math
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HEADER = 'session_id,sensor_id,config_generation,mount_id,calibration_id,sensor_epoch,sequence,sample_time_us,timing_source,measurements_valid,discontinuity,stationary,declaration,force_x_mps2,force_y_mps2,force_z_mps2,rate_x_rad_s,rate_y_rad_s,rate_z_rad_s'


def sample(sequence=0, timestamp=0, stationary=1, declaration=1, **changes):
    row = dict(zip(HEADER.split(','),
                   [1, 1, 1, 1, 1, 1, sequence, timestamp, 'modelled', 1, 0,
                    stationary, declaration, 0, 0, 9.80665, 0, 0, 0]))
    row.update(changes)
    return row


def encode(rows):
    result = io.StringIO()
    writer = csv.DictWriter(result, fieldnames=HEADER.split(','), lineterminator='\n')
    writer.writeheader()
    writer.writerows(rows)
    return result.getvalue()


class DynamicReplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.executable = Path(cls.directory.name) / 'replay'
        subprocess.run([shutil.which('c++') or 'g++', '-std=c++17', '-O2', '-Wall',
                        '-Wextra', '-Werror', '-I', str(ROOT / 'include'),
                        str(ROOT / 'src/dynamic_motion_estimator.cpp'),
                        str(ROOT / 'tools/motion_replay/main.cpp'),
                        '-o', str(cls.executable)], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def run_rows(self, rows):
        result = subprocess.run([str(self.executable), '20000', '2000000'],
                                input=encode(rows), text=True, capture_output=True, check=True)
        return list(csv.DictReader(io.StringIO(result.stdout)))

    def test_independent_roll_trajectory_and_body_translation(self):
        rows = [sample()]
        # Analytic rotation about X, 90 degrees in 1s. A known 2m/s² body-X
        # translation does not alter gravity direction or act as a tilt correction.
        for sequence in range(1, 101):
            angle = math.pi * sequence / 200
            rows.append(sample(sequence, sequence * 10000, 0, 0,
                               force_x_mps2=2,
                               force_y_mps2=9.80665 * math.sin(angle),
                               force_z_mps2=9.80665 * math.cos(angle),
                               rate_x_rad_s=math.pi / 2))
        output = self.run_rows(rows)
        final = output[-1]
        self.assertAlmostEqual(float(final['roll_rad']), math.pi / 2, places=5)
        self.assertAlmostEqual(float(final['q_w']), math.sqrt(.5), places=5)
        self.assertAlmostEqual(float(final['q_x']), math.sqrt(.5), places=5)
        self.assertAlmostEqual(float(final['linear_x_mps2']), 2, places=5)
        self.assertAlmostEqual(float(final['linear_y_mps2']), 0, places=4)
        self.assertAlmostEqual(float(final['linear_z_mps2']), 0, places=4)
        for row in output:
            self.assertEqual(row['quality'], 'unreliable')
            self.assertEqual(row['dynamic_lean_valid'], '0')
            self.assertEqual(row['dynamic_acceleration_valid'], '0')

    def test_gap_clears_numbers_until_new_stationary_declaration(self):
        output = self.run_rows([sample(), sample(2, 10000, 0, 0),
                                sample(3, 20000, 0, 0), sample(4, 30000, 1, 2)])
        self.assertEqual(output[1]['fault'], 'sequence')
        self.assertEqual(output[2]['fault'], 'uninitialized')
        for row in output[1:3]:
            self.assertEqual(row['q_w'], '')
            self.assertEqual(row['linear_x_mps2'], '')
            self.assertEqual(row['quality'], 'invalid')
        self.assertEqual(output[3]['numeric_available'], '1')

    def test_bias_drifts_and_vibration_is_not_tilt_correction(self):
        rows = [sample()]
        for n in range(1, 101):
            rows.append(sample(n, n * 20000, 0, 0,
                               force_z_mps2=9.80665 + (1 if n % 2 else -1),
                               rate_x_rad_s=.01))
        final = self.run_rows(rows)[-1]
        # Constant .01rad/s uncorrected gyro bias yields .02rad drift in 2s;
        # alternating acceleration cannot magically correct that drift.
        self.assertAlmostEqual(float(final['roll_rad']), .02, places=6)
        self.assertAlmostEqual(float(final['linear_z_mps2']),
                               8.80665 - 9.80665 * math.cos(.02), places=5)
        self.assertEqual(final['quality'], 'unreliable')

    def test_precise_large_timestamps_and_unknown_timing(self):
        start = 18446744073709500000
        output = self.run_rows([sample(timestamp=start), sample(1, start + 10000, 0, 0)])
        self.assertEqual(output[-1]['elapsed_us'], '10000')
        unknown = self.run_rows([sample(timing_source='unknown')])[0]
        self.assertEqual(unknown['fault'], 'timing')
        self.assertEqual(unknown['numeric_available'], '0')

    def test_malformed_input_is_failure(self):
        for changes in ({'session_id': '18446744073709551616'},
                        {'rate_x_rad_s': 'nan'}, {'stationary': 2},
                        {'timing_source': 'receipt'}):
            with self.subTest(changes=changes):
                result = subprocess.run([str(self.executable), '20000', '2000000'],
                                        input=encode([sample(**changes)]), text=True,
                                        capture_output=True)
                self.assertNotEqual(result.returncode, 0)

    def test_wrapper_does_not_publish_failed_or_replace_existing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'input.csv'
            output = Path(directory) / 'output.csv'
            source.write_text(encode([sample(rate_x_rad_s='nan')]))
            command = [shutil.which('python3'), str(ROOT / 'scripts/replay_dynamic_motion.py'),
                       '--experimental', '--input', str(source), '--output', str(output)]
            result = subprocess.run(command, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())
            output.write_text('preserve me')
            source.write_text(encode([sample()]))
            result = subprocess.run(command, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_text(), 'preserve me')


if __name__ == '__main__':
    unittest.main()
