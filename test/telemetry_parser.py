"""Independent strict parser for the documented mixed v2 contract; test tooling only."""
import csv
import math
import re

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


MOTION = 'motion_state motion_algorithm motion_snapshot_max_age_ms motion_convention motion_mount_qualified motion_residual_calibration_qualified motion_mount_id motion_calibration_id motion_accel_compensation motion_gyro_compensation motion_r_bs_00 motion_r_bs_01 motion_r_bs_02 motion_r_bs_10 motion_r_bs_11 motion_r_bs_12 motion_r_bs_20 motion_r_bs_21 motion_r_bs_22 motion_reference_stationary motion_reference_session_id motion_reference_generation motion_reference_batch motion_reference_declaration motion_measurements_valid motion_static_tilt_valid motion_dynamic_lean_valid motion_dynamic_acceleration_valid motion_force_x_mps2 motion_force_y_mps2 motion_force_z_mps2 motion_rate_x_rad_s motion_rate_y_rad_s motion_rate_z_rad_s motion_roll_rad motion_pitch_rad'.split()

def _motion(row):
    def integer(field, lo=0, hi=0xffffffff):
        value = row[field]
        if not value or not value.isascii() or not value.isdigit() or not lo <= int(value) <= hi:
            raise ValueError('invalid motion integer '+field)
        return int(value)

    def number(field):
        value = row[field]
        if not re.fullmatch(r'-?(?:[0-9]+(?:\.[0-9]+)?)(?:[eE][+-]?[0-9]+)?', value):
            raise ValueError('invalid motion number '+field)
        parsed = float(value)
        if not math.isfinite(parsed):
            raise ValueError('nonfinite motion number')
        return parsed

    state = integer('motion_state', 0, 3)
    flags = MOTION[24:28]
    if any(row[f] not in ('0', '1') for f in flags) or any(row[f] != '0' for f in flags[2:]):
        raise ValueError('motion result flags')
    if state != 2:
        if any(row[f] for f in MOTION[1:] if f not in flags) or any(row[f] != '0' for f in flags):
            raise ValueError('inactive motion presence')
        return
    for f in ('motion_algorithm','motion_convention','motion_mount_qualified','motion_residual_calibration_qualified'):
        if row[f] != '1': raise ValueError('motion qualification')
    integer('motion_snapshot_max_age_ms',1,60000)
    integer('motion_mount_id',1); integer('motion_calibration_id',1)
    integer('motion_accel_compensation',1,2); integer('motion_gyro_compensation',1,2)
    rotation = [number(f) for f in MOTION[10:19]]
    for i in range(3):
        for j in range(3):
            dot = sum(rotation[3*i+k]*rotation[3*j+k] for k in range(3))
            if abs(dot - (1 if i == j else 0)) > 0.0001: raise ValueError('motion rotation')
    a,b,c,d,e,f,g,h,i = rotation
    if a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g) <= 0: raise ValueError('motion reflection')
    if row['motion_reference_stationary'] not in ('0','1'): raise ValueError('reference flag')
    integer('motion_reference_session_id',0,0xffffffffffffffff)
    for f in MOTION[21:24]: integer(f)
    measured = row['motion_measurements_valid'] == '1'
    tilt = row['motion_static_tilt_valid'] == '1'
    for f in MOTION[28:34]:
        if measured: number(f)
        elif row[f]: raise ValueError('invalid vector presence')
    for f in MOTION[34:36]:
        if tilt: number(f)
        elif row[f]: raise ValueError('invalid angle presence')
    if row['kind'] != 'imu':
        if measured or tilt or any(row[f] != '0' for f in MOTION[19:24]):
            raise ValueError('motion event result')
        return
    if measured:
        for f in ('sensor_state','mount_state','calibration_state'):
            if row[f] != '2': raise ValueError('raw qualification')
        for raw, qualified in (('mount_id','motion_mount_id'),('calibration_id','motion_calibration_id'),
                               ('accel_offset_compensation','motion_accel_compensation'),
                               ('gyro_offset_compensation','motion_gyro_compensation')):
            if row[raw] != row[qualified]: raise ValueError('motion raw context')
        if not int(row['sensor_id']) or not int(row['generation']) or                 row['calibration_offsets_known'] != '1' or row['calibration_gains_known'] != '1':
            raise ValueError('missing raw calibration')
        for f in ('accel_scale_numerator','accel_scale_denominator','gyro_scale_numerator','gyro_scale_denominator'):
            if not int(row[f]): raise ValueError('motion scale')
        if int(row['timing_flags']) & 8 or any(abs(int(row[p+'_'+axis])) >= 32767
                                              for p in ('accel','gyro') for axis in ('x','y','z')):
            raise ValueError('saturated motion')
    if tilt:
        if not measured or row['motion_reference_stationary'] != '1' or                 row['receipt_known'] != '1' or int(row['timing_flags']) & 7 or                 not int(row['motion_reference_declaration']):
            raise ValueError('invalid static reference')
        for raw, reference in (('session_id','motion_reference_session_id'),
                               ('generation','motion_reference_generation'),
                               ('batch_sequence','motion_reference_batch')):
            if row[raw] != row[reference]: raise ValueError('stationary context')


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
    if version not in ('#ridesync_telemetry,2', '#ridesync_telemetry,3', '#ridesync_telemetry,4'):
        raise ValueError('unsupported version')
    if not data.endswith('\n'):
        raise ValueError('partial trailing row')
    if version in ('#ridesync_telemetry,3', '#ridesync_telemetry,4') and '#camera_layout,3,see_docs/log_format.md\n' not in data:
        raise ValueError('missing camera layout')
    if version == '#ridesync_telemetry,4' and '#imu_layout,4,see_docs/motion_logging.md\n' not in data:
        raise ValueError('missing motion layout')
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
            extra = MOTION if version == '#ridesync_telemetry,4' else []
            if len(values) != 1 + len(COMMON) + len(IMU) + len(extra):
                raise ValueError(f'IMU column count {len(values)}')
            row = dict(zip(['kind'] + COMMON + IMU + extra, values))
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
        elif values[0] == 'camera' and version in ('#ridesync_telemetry,3', '#ridesync_telemetry,4'):
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
        if version in ('#ridesync_telemetry,3', '#ridesync_telemetry,4'):
            _v3_timestamp(row)
        if version == "#ridesync_telemetry,4" and row["kind"] in ("imu","config","health","control"):
            _motion(row)
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
