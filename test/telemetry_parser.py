"""Independent strict parser for the documented mixed v2 contract; test tooling only."""
import csv

COMMON = ('session_id monotonic_ms monotonic_quality anchor_quality anchor_sequence '
          'anchor_receipt_ms anchor_utc_ms uncertainty_known uncertainty_ms anchor_age_ms '
          'has_utc_estimate utc_estimate_ms').split()
IMU = ('batch_sequence frame_sequence byte_position sensor_epoch receipt_known receipt_millis32 '
       'drain_known drain_start_millis32 drain_end_millis32 timing_flags acquisition_known acquisition_ms '
       'generation sensor_id mount_id calibration_id sensor_state mount_state calibration_state '
       'accel_range_mg gyro_range_mdps accel_scale_numerator accel_scale_denominator '
       'gyro_scale_numerator gyro_scale_denominator accel_odr_millihz gyro_odr_millihz '
       'accel_filter gyro_filter accel_offset_compensation gyro_offset_compensation calibration_method '
       'calibration_time_known calibration_utc_ms calibration_temperature_known calibration_temperature_millic '
       'calibration_offsets_known accel_offset_x accel_offset_y accel_offset_z gyro_offset_x gyro_offset_y gyro_offset_z '
       'calibration_gains_known accel_gain_x_numerator accel_gain_x_denominator accel_gain_y_numerator accel_gain_y_denominator accel_gain_z_numerator accel_gain_z_denominator gyro_gain_x_numerator gyro_gain_x_denominator gyro_gain_y_numerator gyro_gain_y_denominator gyro_gain_z_numerator gyro_gain_z_denominator accel_x accel_y accel_z gyro_x gyro_y gyro_z event_code event_length event_bytes '
       'sensor_time_present sensor_time_ticks24 event_count event_count_lower_bound '
       'accepted dropped rejected lost written flushed').split()


def parse(data):
    if not data.startswith('#ridesync_telemetry,2\n'):
        raise ValueError('unsupported version')
    if not data.endswith('\n'):
        raise ValueError('partial trailing row')
    rows = []
    for line in data.splitlines()[1:]:
        if line.startswith('#') or line.startswith('session_id,'):
            continue
        values = next(csv.reader([line]))
        if values[0] == 'gps':
            if len(values) != 36:
                raise ValueError('GPS column count')
            row = dict(zip(['kind'] + COMMON, values))
        elif values[0] in ('imu', 'config', 'health', 'control'):
            if len(values) != 1 + len(COMMON) + len(IMU):
                raise ValueError(f'IMU column count {len(values)}')
            row = dict(zip(['kind'] + COMMON + IMU, values))
            if row['acquisition_known'] != '0' or row['acquisition_ms']:
                raise ValueError('unsupported acquisition model')
            for flag, fields in [('receipt_known', ['receipt_millis32']),
                                 ('drain_known', ['drain_start_millis32', 'drain_end_millis32']),
                                 ('sensor_time_present', ['sensor_time_ticks24']),
                                 ('calibration_time_known', ['calibration_utc_ms']),
                                 ('calibration_temperature_known', ['calibration_temperature_millic'])]:
                if row[flag] not in ('0', '1') or any(bool(row[f]) != (row[flag] == '1') for f in fields):
                    raise ValueError('optional field presence')
            for flag, prefix, suffixes in [('calibration_offsets_known', ('accel', 'gyro'), ('offset_x','offset_y','offset_z')),
                                           ('calibration_gains_known', ('accel','gyro'), ('gain_x_numerator','gain_x_denominator','gain_y_numerator','gain_y_denominator','gain_z_numerator','gain_z_denominator'))]:
                if row[flag] not in ('0', '1'):
                    raise ValueError('coefficient presence')
                for p in prefix:
                    for suffix in suffixes:
                        if bool(row[p+'_'+suffix]) != (row[flag] == '1'):
                            raise ValueError('coefficient presence')
            channels = [row[p + '_' + axis] for p in ('accel', 'gyro') for axis in ('x', 'y', 'z')]
            if row['kind'] == 'imu':
                if any(not x or not -32768 <= int(x) <= 32767 for x in channels):
                    raise ValueError('signed raw count')
            elif any(channels):
                raise ValueError('event contains fabricated sample')
            payload = bytes.fromhex(row['event_bytes'])
            if len(payload) != int(row['event_length']) or len(payload) > 4:
                raise ValueError('event payload length')
            if row['sensor_time_present'] == '1':
                if row['kind'] != 'control' or row['event_code'] != '5' or len(payload) != 3:
                    raise ValueError('sensor time event')
                if int.from_bytes(payload, 'little') != int(row['sensor_time_ticks24']):
                    raise ValueError('sensor time payload')
        else:
            raise ValueError('unsupported record kind')
        if row['monotonic_quality'] != '0' or int(row['session_id']) <= 0:
            raise ValueError('invalid timestamp')
        if row['anchor_quality'] != '0':
            if int(row['monotonic_ms']) - int(row['anchor_receipt_ms']) != int(row['anchor_age_ms']):
                raise ValueError('anchor age')
        if row['has_utc_estimate'] == '1':
            if int(row['anchor_utc_ms']) + int(row['anchor_age_ms']) != int(row['utc_estimate_ms']):
                raise ValueError('UTC estimate')
        rows.append(row)
    return rows
