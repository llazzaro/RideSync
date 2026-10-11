"""Prepare modelled-cadence offline input from strict MotionV4 evidence.

No acquisition timestamps or trustworthy dynamic validity are inferred. MotionV4
has no global evidence sequence; omitted controls cannot always be detected.
"""
import argparse
import csv
from fractions import Fraction
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'test'))
from telemetry_parser import parse, IMU, MOTION  # noqa: E402

COLUMNS = ('session_id sensor_id config_generation mount_id calibration_id sensor_epoch '
           'sequence sample_time_us timing_source measurements_valid discontinuity stationary '
           'declaration force_x_mps2 force_y_mps2 force_z_mps2 rate_x_rad_s rate_y_rad_s '
           'rate_z_rad_s').split()
VECTORS = MOTION[28:34]
# Configuration includes coefficients and rotation, not merely declared IDs.
CONTEXT = ['session_id', 'sensor_epoch'] + IMU[12:56] + MOTION[1:19]
LOSS_COUNTERS = ('dropped', 'rejected', 'lost')


def _motion_rows(data, anchor_frame, timing_model):
    if data.split('\n', 1)[0] != '#ridesync_telemetry,4':
        raise ValueError('MotionV4 input required')
    if timing_model != 'nominal-odr' or anchor_frame <= 0:
        raise ValueError('explicit positive anchor and nominal-odr model required')
    try:
        rows = parse(data)
    except (IndexError, csv.Error) as error:
        raise ValueError("malformed MotionV4 record") from error
    if sum(row["kind"] == "imu" and int(row["frame_sequence"]) == anchor_frame
           for row in rows) != 1:
        raise ValueError("anchor frame must occur exactly once")
    return rows


def _anchor_rate(row, measured):
    qualified = ('motion_static_tilt_valid', 'motion_reference_stationary')
    references = (('session_id', 'motion_reference_session_id'),
                  ('generation', 'motion_reference_generation'),
                  ('batch_sequence', 'motion_reference_batch'))
    if not measured or any(row[field] != '1' for field in qualified):
        raise ValueError('anchor requires matching qualified stationary evidence')
    if int(row['timing_flags']) or not int(row['motion_reference_declaration']):
        raise ValueError('anchor requires matching qualified stationary evidence')
    if any(row[actual] != row[reference] for actual, reference in references):
        raise ValueError('anchor requires matching qualified stationary evidence')
    odr = int(row['gyro_odr_millihz'])
    if odr <= 0 or int(row['accel_odr_millihz']) != odr:
        raise ValueError('anchor requires equal nonzero accelerometer/gyro ODR')
    return odr


class ReplaySequence:
    """Track continuity separately from the explicitly declared anchor/cadence."""

    def __init__(self, anchor_frame):
        self.anchor_frame = anchor_frame
        self.context = None
        self.counters = {}
        self.previous_frame = None
        self.previous_position = None
        self.pending_break = False
        self.odr = None
        self.sample_count = 0

    def observe(self, row):
        kind = row['kind']
        current_context = tuple(row[field] for field in CONTEXT)
        current_counters = tuple(int(row[field]) for field in LOSS_COUNTERS)
        if kind in self.counters and current_counters != self.counters[kind]:
            self.pending_break = True
        if any(value == 0xffffffff for value in current_counters):
            self.pending_break = True
        self.counters[kind] = current_counters
        if self.context is not None and current_context != self.context:
            self.pending_break = True
        self.context = current_context
        self.observe_position(row)

    def observe_position(self, row):
        if int(row['timing_flags']):
            self.pending_break = True
        position = (int(row['batch_sequence']), int(row['byte_position']))
        if self.previous_position is not None and position <= self.previous_position:
            self.pending_break = True
        self.previous_position = position

    def accept(self, row):
        kind = row['kind']
        if kind not in ('imu', 'config', 'health', 'control'):
            return None
        self.observe(row)
        if kind != 'imu':
            # Normal trailers/end markers preserve the nominal sample chain.
            if kind != 'control' or int(row['event_code']) not in (0, 5):
                self.pending_break = True
            return None
        frame = int(row['frame_sequence'])
        if self.previous_frame is not None and frame != self.previous_frame + 1:
            self.pending_break = True
        self.previous_frame = frame
        measured = row['motion_state'] == '2' and row['motion_measurements_valid'] == '1'
        if self.odr is None and frame != self.anchor_frame:
            self.pending_break = False
            return None
        return self.sample(row, measured)

    def sample(self, row, measured):
        stationary = int(self.odr is None)
        declaration = int(row['motion_reference_declaration']) if stationary else 0
        if stationary:
            self.odr = _anchor_rate(row, measured)
            # An explicit qualified anchor starts a new segment after earlier faults.
            self.pending_break = False
        else:
            self.sample_count += 1
            if any(int(row[field]) != self.odr for field in
                   ('gyro_odr_millihz', 'accel_odr_millihz')):
                self.pending_break = True
        if not measured:
            self.pending_break = True
        result = {
            'session_id': int(row['session_id']), 'sensor_id': int(row['sensor_id']),
            'config_generation': int(row['generation']), 'mount_id': int(row['mount_id']),
            'calibration_id': int(row['calibration_id']), 'sensor_epoch': int(row['sensor_epoch']),
            'sequence': int(row['frame_sequence']),
            'sample_time_us': round(Fraction(self.sample_count * 1000000000, self.odr)),
            'timing_source': 'modelled', 'measurements_valid': int(measured),
            'discontinuity': int(self.pending_break), 'stationary': stationary,
            'declaration': declaration,
        }
        for target, source in zip(COLUMNS[13:], VECTORS):
            result[target] = row[source] if measured else '0'
        self.pending_break = False
        return result


def prepare(data, anchor_frame, timing_model):
    rows = _motion_rows(data, anchor_frame, timing_model)
    sequence = ReplaySequence(anchor_frame)
    output = []
    for row in rows:
        result = sequence.accept(row)
        if result is not None:
            output.append(result)
    if sequence.odr is None:
        raise ValueError('anchor frame not found')
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--anchor-frame', required=True, type=int)
    parser.add_argument('--timing-model', required=True, choices=['nominal-odr'])
    args = parser.parse_args()
    try:
        rows = prepare(args.input.read_text(), args.anchor_frame, args.timing_model)
        with args.output.open('x', newline='') as destination:
            writer = csv.DictWriter(destination, COLUMNS)
            writer.writeheader()
            writer.writerows(rows)
    except (OSError, ValueError) as error:
        parser.exit(2, f'{error}\n')


if __name__ == '__main__':
    main()
