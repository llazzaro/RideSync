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
CAMERA = ('peer_slot peer_id model group_generation intent_id connection_generation operation_generation '
          'event_kind operation error recording ack_domain ack_action delivery_admitted '
          'time_domain event_receipt_known event_receipt_ms event_receipt_age_ms '
          'radio_receipt_known radio_receipt_ms acquisition_known acquisition_ms '
          'accepted dropped rejected lost written flushed').split()


def _v3_timestamp(row):
    def unsigned(field, lower, upper):
        value = row[field]
        if not value or not value.isascii() or not value.isdigit():
            raise ValueError(f'invalid {field}')
        number = int(value)
        if not lower <= number <= upper:
            raise ValueError(f'out-of-range {field}')
        return number

    unsigned('session_id', 1, 0xffffffffffffffff)
    monotonic = unsigned('monotonic_ms', 0, 31536000000)
    if row['monotonic_quality'] != '0' or row['anchor_quality'] not in ('0', '1', '2') or \
            row['uncertainty_known'] not in ('0', '1') or \
            row['has_utc_estimate'] not in ('0', '1'):
        raise ValueError('invalid V3 timestamp flags')
    if row['anchor_quality'] == '0':
        if any(row[field] for field in ('anchor_sequence', 'anchor_receipt_ms',
                                        'anchor_utc_ms', 'uncertainty_ms', 'anchor_age_ms',
                                        'utc_estimate_ms')) or row['uncertainty_known'] != '0' or \
                row['has_utc_estimate'] != '0':
            raise ValueError('unexpected anchor fields')
        return
    unsigned('anchor_sequence', 1, 0xffffffff)
    receipt = unsigned('anchor_receipt_ms', 0, monotonic)
    utc = unsigned('anchor_utc_ms', 946684800000, 4102444799999)
    if unsigned('anchor_age_ms', 0, 31536000000) != monotonic - receipt:
        raise ValueError('anchor age')
    if row['uncertainty_known'] == '1':
        unsigned('uncertainty_ms', 0, 0xffffffff)
    elif row['uncertainty_ms']:
        raise ValueError('unexpected uncertainty')
    if row['has_utc_estimate'] == '1':
        if row['anchor_quality'] != '1' or unsigned('utc_estimate_ms', 0, 0x7fffffffffffffff) != \
                utc + monotonic - receipt:
            raise ValueError('invalid UTC estimate')
    elif row['utc_estimate_ms']:
        raise ValueError('unexpected UTC estimate')


def parse(data):
    version = data.split('\n', 1)[0]
    if version not in ('#ridesync_telemetry,2', '#ridesync_telemetry,3'):
        raise ValueError('unsupported version')
    if not data.endswith('\n'):
        raise ValueError('partial trailing row')
    if version == '#ridesync_telemetry,3' and '#camera_layout,3,see_docs/log_format.md\n' not in data:
        raise ValueError('missing camera layout')
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
        elif values[0] == 'camera' and version == '#ridesync_telemetry,3':
            if len(values) != 1 + len(COMMON) + len(CAMERA):
                raise ValueError('camera column count')
            row = dict(zip(['kind'] + COMMON + CAMERA, values))
            if row['time_domain'] != 'owner_admission' or row['radio_receipt_known'] != '0' or \
                    row['radio_receipt_ms'] or row['acquisition_known'] != '0' or row['acquisition_ms']:
                raise ValueError('camera time provenance')
            if row['event_receipt_known'] not in ('0', '1') or \
                    bool(row['event_receipt_ms']) != (row['event_receipt_known'] == '1') or \
                    bool(row['event_receipt_age_ms']) != (row['event_receipt_known'] == '1'):
                raise ValueError('camera event receipt presence')
            if row['event_receipt_known'] == '1':
                receipt = int(row['event_receipt_ms'])
                age = int(row['event_receipt_age_ms'])
                if receipt < 0 or age < 0 or age > 60000 or \
                        receipt + age != int(row['monotonic_ms']):
                    raise ValueError('camera event receipt age')
            numeric = ('peer_slot peer_id model group_generation intent_id connection_generation '
                       'operation_generation event_kind operation error recording ack_domain '
                       'ack_action delivery_admitted').split()
            if any(not row[field] or not 0 <= int(row[field]) <= 0xffffffff
                   for field in numeric):
                raise ValueError('camera numeric fields')
            if int(row['peer_slot']) not in range(8) or int(row['peer_id']) == 0 or \
                    int(row['model']) not in range(1, 5) or \
                    int(row['event_kind']) not in range(10) or int(row['operation']) not in range(5) or \
                    int(row['error']) not in range(11) or int(row['recording']) not in range(3) or \
                    int(row['ack_domain']) not in range(3) or int(row['ack_action']) not in range(16) or \
                    row['delivery_admitted'] not in ('0', '1'):
                raise ValueError('camera enum fields')
            kind = int(row['event_kind'])
            if kind == 4:
                if any(int(row[field]) == 0 for field in ('intent_id','connection_generation',
                                                          'operation_generation','ack_domain','ack_action')):
                    raise ValueError('uncorrelated camera ACK')
                action = int(row['ack_action'])
                domain = int(row['ack_domain'])
                if (action in (1, 2) and domain != 2) or (action not in (1, 2) and domain != 1):
                    raise ValueError('camera ACK domain')
            elif int(row['ack_domain']) or int(row['ack_action']):
                raise ValueError('unexpected ACK fields')
            if kind == 5:
                if int(row['recording']) == 0 or int(row['connection_generation']) == 0:
                    raise ValueError('invalid camera observation')
            elif int(row['recording']):
                raise ValueError('unexpected camera observation')
            if kind in (0, 1) and (int(row['intent_id']) == 0 or int(row['error'])):
                raise ValueError('invalid admitted request')
            if kind == 2 and (int(row['intent_id']) or int(row['error']) == 0):
                raise ValueError('invalid refused request')
            if kind == 1 and int(row['operation_generation']):
                raise ValueError('queued request borrowed operation')
            if kind in (0, 1, 2) and (int(row['connection_generation']) or
                                      int(row['operation_generation']) or row['delivery_admitted'] != '0'):
                raise ValueError('request borrowed token')
            if kind == 3 and (int(row['intent_id']) == 0 or
                              int(row['connection_generation']) == 0 or
                              int(row['operation_generation']) == 0 or
                              (row['error'] != ('0' if row['delivery_admitted'] == '1' else '8'))):
                raise ValueError('invalid transport attempt')
            if kind != 3 and row['delivery_admitted'] != '0':
                raise ValueError('unexpected delivery admission')
        else:
            raise ValueError('unsupported record kind')
        if version == '#ridesync_telemetry,3':
            _v3_timestamp(row)
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
