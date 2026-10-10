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


def prepare(data, anchor_frame, timing_model):
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
    output = []
    context = None
    counters = {}
    previous_frame = None
    previous_batch = None
    previous_position = None
    pending_break = False
    anchored = False
    odr = None
    sample_count = 0
    for row in rows:
        kind = row['kind']
        if kind not in ('imu', 'config', 'health', 'control'):
            continue
        current_context = tuple(row[field] for field in CONTEXT)
        current_counters = tuple(int(row[field]) for field in LOSS_COUNTERS)
        if kind in counters and current_counters != counters[kind]:
            pending_break = True
        if any(value == 0xffffffff for value in current_counters):
            pending_break = True
        counters[kind] = current_counters
        if context is not None and current_context != context:
            pending_break = True
        context = current_context
        if int(row['timing_flags']):
            pending_break = True
        batch, position = int(row['batch_sequence']), int(row['byte_position'])
        if previous_batch is not None and (batch < previous_batch or
                (batch == previous_batch and position <= previous_position)):
            pending_break = True
        previous_batch, previous_position = batch, position
        if kind != 'imu':
            # Normal trailers/end markers preserve the nominal sample chain.
            if kind != 'control' or int(row['event_code']) not in (0, 5):
                pending_break = True
            continue
        frame = int(row['frame_sequence'])
        if previous_frame is not None and frame != previous_frame + 1:
            pending_break = True
        previous_frame = frame
        measured = row['motion_state'] == '2' and row['motion_measurements_valid'] == '1'
        if not anchored:
            if frame != anchor_frame:
                pending_break = False
                continue
            if not measured or row['motion_static_tilt_valid'] != '1' or \
                    row['motion_reference_stationary'] != '1' or int(row['timing_flags']) or \
                    row['session_id'] != row['motion_reference_session_id'] or \
                    row['generation'] != row['motion_reference_generation'] or \
                    row['batch_sequence'] != row['motion_reference_batch'] or \
                    not int(row['motion_reference_declaration']):
                raise ValueError('anchor requires matching qualified stationary evidence')
            # An explicit qualified anchor starts a new segment after earlier faults.
            pending_break = False
            odr = int(row['gyro_odr_millihz'])
            if odr <= 0 or int(row['accel_odr_millihz']) != odr:
                raise ValueError('anchor requires equal nonzero accelerometer/gyro ODR')
            anchored = True
            stationary = 1
            declaration = int(row['motion_reference_declaration'])
        else:
            sample_count += 1
            stationary, declaration = 0, 0
            if int(row['gyro_odr_millihz']) != odr or int(row['accel_odr_millihz']) != odr:
                pending_break = True
        if not measured:
            pending_break = True
        result = {
            'session_id': int(row['session_id']), 'sensor_id': int(row['sensor_id']),
            'config_generation': int(row['generation']), 'mount_id': int(row['mount_id']),
            'calibration_id': int(row['calibration_id']), 'sensor_epoch': int(row['sensor_epoch']),
            'sequence': frame,
            'sample_time_us': round(Fraction(sample_count * 1000000000, odr)),
            'timing_source': 'modelled', 'measurements_valid': int(measured),
            'discontinuity': int(pending_break), 'stationary': stationary,
            'declaration': declaration,
        }
        for target, source in zip(COLUMNS[13:], VECTORS):
            result[target] = row[source] if measured else '0'
        output.append(result)
        pending_break = False
    if not anchored:
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
