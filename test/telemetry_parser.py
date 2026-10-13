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


HEALTH = ('peer_slot peer_id model group_generation intent_id connection_generation operation_generation '
          'episode transition_sequence transition_kind reason severity capability validity profile field historical value unit '
          'threshold_enter threshold_clear time_domain sample_receipt_known sample_receipt_ms sample_age_ms '
          'radio_receipt_known radio_receipt_ms acquisition_known acquisition_ms '
          'transition_dropped transition_coalesced history_incomplete accepted dropped rejected lost written flushed').split()


MOTION = 'motion_state motion_algorithm motion_snapshot_max_age_ms motion_convention motion_mount_qualified motion_residual_calibration_qualified motion_mount_id motion_calibration_id motion_accel_compensation motion_gyro_compensation motion_r_bs_00 motion_r_bs_01 motion_r_bs_02 motion_r_bs_10 motion_r_bs_11 motion_r_bs_12 motion_r_bs_20 motion_r_bs_21 motion_r_bs_22 motion_reference_stationary motion_reference_session_id motion_reference_generation motion_reference_batch motion_reference_declaration motion_measurements_valid motion_static_tilt_valid motion_dynamic_lean_valid motion_dynamic_acceleration_valid motion_force_x_mps2 motion_force_y_mps2 motion_force_z_mps2 motion_rate_x_rad_s motion_rate_y_rad_s motion_rate_z_rad_s motion_roll_rad motion_pitch_rad'.split()

DYNAMIC = 'dynamic_requested dynamic_cadence_us dynamic_declaration dynamic_quality dynamic_fault dynamic_timing_source dynamic_elapsed_us dynamic_numeric_available dynamic_angles_available dynamic_lean_valid dynamic_acceleration_valid dynamic_qw dynamic_qx dynamic_qy dynamic_qz dynamic_gravity_x_mps2 dynamic_gravity_y_mps2 dynamic_gravity_z_mps2 dynamic_accel_x_mps2 dynamic_accel_y_mps2 dynamic_accel_z_mps2 dynamic_roll_rad dynamic_pitch_rad'.split()

def _motion_number(row, field):
    value = row[field]
    if not re.fullmatch(r'-?(?:[0-9]+(?:\.[0-9]+)?)(?:[eE][+-]?[0-9]+)?', value):
        raise ValueError('invalid motion number '+field)
    parsed = float(value)
    if not math.isfinite(parsed):
        raise ValueError('nonfinite motion number')
    return parsed


def _motion_integer(row, field, lo=0, hi=0xffffffff):
    value = row[field]
    if not value or not value.isascii() or not value.isdigit() or not lo <= int(value) <= hi:
        raise ValueError('invalid motion integer '+field)
    return int(value)


def _motion_signed(row, field, lo, hi):
    value = row[field]
    if not re.fullmatch(r'-?[0-9]+', value) or not lo <= int(value) <= hi:
        raise ValueError('invalid signed raw integer '+field)


def _validate_motion_scalars(row):
    fields = ('batch_sequence frame_sequence byte_position sensor_epoch generation sensor_id '
              'mount_id calibration_id accel_range_mg gyro_range_mdps accel_scale_numerator '
              'accel_scale_denominator gyro_scale_numerator gyro_scale_denominator '
              'accel_odr_millihz gyro_odr_millihz accel_filter gyro_filter calibration_method '
              'event_count accepted dropped rejected lost written flushed').split()
    for field in fields:
        _motion_integer(row, field)
    for field in ('sensor_state','mount_state','calibration_state',
                  'accel_offset_compensation','gyro_offset_compensation'):
        _motion_integer(row, field, 0, 2)
    for field, upper in [('timing_flags', 15), ('event_code', 9),
                         ('event_length', 4), ('event_count_lower_bound', 1)]:
        _motion_integer(row, field, 0, upper)
    _validate_optional_motion_scalars(row)
    _validate_motion_coefficients(row)


def _validate_optional_motion_scalars(row):
    for field in ('receipt_millis32','drain_start_millis32','drain_end_millis32'):
        if row[field]: _motion_integer(row, field)
    if row['sensor_time_ticks24']: _motion_integer(row, 'sensor_time_ticks24', 0, 0xffffff)
    if row['calibration_utc_ms']: _motion_signed(row, 'calibration_utc_ms', -(1<<63), (1<<63)-1)
    if row['calibration_temperature_millic']:
        _motion_signed(row, 'calibration_temperature_millic', -(1<<31), (1<<31)-1)


def _validate_motion_coefficients(row):
    for prefix in ('accel','gyro'):
        for axis in ('x','y','z'):
            offset = prefix+'_offset_'+axis
            if row[offset]: _motion_signed(row, offset, -32768, 32767)
            for ratio in ('numerator','denominator'):
                field = prefix+'_gain_'+axis+'_'+ratio
                if row[field]: _motion_integer(row, field, 1)


def _validate_motion_state(row, state):
    if state != 2:
        return False
    for field in ('motion_algorithm','motion_convention','motion_mount_qualified',
                  'motion_residual_calibration_qualified'):
        if row[field] != '1': raise ValueError('motion qualification')
    _motion_integer(row, 'motion_snapshot_max_age_ms', 1, 60000)
    _motion_integer(row, 'motion_mount_id', 1)
    _motion_integer(row, 'motion_calibration_id', 1)
    _motion_integer(row, 'motion_accel_compensation', 1, 2)
    _motion_integer(row, 'motion_gyro_compensation', 1, 2)
    return True


def _validate_motion_rotation(row):
    rotation = [_motion_number(row, field) for field in MOTION[10:19]]
    for i in range(3):
        for j in range(3):
            dot = sum(rotation[3*i+k]*rotation[3*j+k] for k in range(3))
            if abs(dot - (1 if i == j else 0)) > 0.0001: raise ValueError('motion rotation')
    a,b,c,d,e,f,g,h,i = rotation
    if a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g) <= 0: raise ValueError('motion reflection')


def _validate_motion_vectors(row):
    measured = row['motion_measurements_valid'] == '1'
    tilt = row['motion_static_tilt_valid'] == '1'
    for field in MOTION[28:34]:
        if measured: _motion_number(row, field)
        elif row[field]: raise ValueError('invalid vector presence')
    for field in MOTION[34:36]:
        if tilt: _motion_number(row, field)
        elif row[field]: raise ValueError('invalid angle presence')
    return measured, tilt


def _validate_motion_result(row):
    state = _motion_integer(row, 'motion_state', 0, 3)
    flags = MOTION[24:28]
    if any(row[f] not in ('0', '1') for f in flags) or any(row[f] != '0' for f in flags[2:]):
        raise ValueError('motion result flags')
    if state != 2:
        _validate_inactive_motion(row, flags)
        return False, False, False
    _validate_motion_state(row, state)
    _validate_motion_rotation(row)
    if row['motion_reference_stationary'] not in ('0','1'): raise ValueError('reference flag')
    _motion_integer(row, 'motion_reference_session_id', 0, 0xffffffffffffffff)
    for field in MOTION[21:24]: _motion_integer(row, field)
    measured, tilt = _validate_motion_vectors(row)
    return True, measured, tilt


def _validate_inactive_motion(row, flags):
    fields = [field for field in MOTION[1:] if field not in flags]
    if any(row[field] for field in fields) or any(row[field] != '0' for field in flags):
        raise ValueError('inactive motion presence')


def _validate_motion_raw(row, measured):
    if not measured: return
    for field in ('sensor_state','mount_state','calibration_state'):
        if row[field] != '2': raise ValueError('raw qualification')
    for raw, qualified in (('mount_id','motion_mount_id'), ('calibration_id','motion_calibration_id'),
                           ('accel_offset_compensation','motion_accel_compensation'),
                           ('gyro_offset_compensation','motion_gyro_compensation')):
        if row[raw] != row[qualified]: raise ValueError('motion raw context')
    _validate_raw_calibration(row)
    for field in ('accel_scale_numerator','accel_scale_denominator','gyro_scale_numerator','gyro_scale_denominator'):
        _motion_integer(row, field, 1)
    _validate_motion_saturation(row)


def _validate_motion_saturation(row):
    if int(row['timing_flags']) & 8 or any(abs(int(row[p+'_'+axis])) >= 32767
                                             for p in ('accel','gyro') for axis in ('x','y','z')):
        raise ValueError('saturated motion')


def _validate_raw_calibration(row):
    _motion_integer(row, 'sensor_id', 1)
    _motion_integer(row, 'generation', 1)
    if any(row[field] != '1' for field in
           ('calibration_offsets_known', 'calibration_gains_known')):
        raise ValueError('missing raw calibration')


def _validate_motion_reference(row, measured, tilt):
    if not tilt: return
    if not measured or row['motion_reference_stationary'] != '1' or row['receipt_known'] != '1' or \
            int(row['timing_flags']) & 7 or not int(row['motion_reference_declaration']):
        raise ValueError('invalid static reference')
    for raw, reference in (('session_id','motion_reference_session_id'),
                           ('generation','motion_reference_generation'),
                           ('batch_sequence','motion_reference_batch')):
        if row[raw] != row[reference]: raise ValueError('stationary context')


def _motion(row):
    _validate_motion_scalars(row)
    active, measured, tilt = _validate_motion_result(row)
    if not active:
        return
    if row['kind'] != 'imu':
        if measured or tilt or any(row[f] != '0' for f in MOTION[19:24]):
            raise ValueError('motion event result')
        return
    _validate_motion_raw(row, measured)
    _validate_motion_reference(row, measured, tilt)


def _timestamp_unsigned(row, field, lower, upper):
    value = row[field]
    if not value or not value.isascii() or not value.isdigit():
        raise ValueError(f'invalid {field}')
    number = int(value)
    if not lower <= number <= upper:
        raise ValueError(f'out-of-range {field}')
    return number


def _v3_timestamp(row):
    _timestamp_unsigned(row, 'session_id', 1, 0xffffffffffffffff)
    monotonic = _timestamp_unsigned(row, 'monotonic_ms', 0, 31536000000)
    if row['monotonic_quality'] != '0' or row['anchor_quality'] not in ('0', '1', '2') or \
            row['uncertainty_known'] not in ('0', '1') or \
            row['has_utc_estimate'] not in ('0', '1'):
        raise ValueError('invalid V3 timestamp flags')
    if row['anchor_quality'] == '0':
        _validate_absent_anchor(row)
        return
    _validate_present_anchor(row, monotonic)


def _validate_absent_anchor(row):
    fields = ('anchor_sequence', 'anchor_receipt_ms', 'anchor_utc_ms',
              'uncertainty_ms', 'anchor_age_ms', 'utc_estimate_ms')
    if any(row[field] for field in fields) or row['uncertainty_known'] != '0' or \
            row['has_utc_estimate'] != '0':
        raise ValueError('unexpected anchor fields')


def _validate_present_anchor(row, monotonic):
    _timestamp_unsigned(row, 'anchor_sequence', 1, 0xffffffff)
    receipt = _timestamp_unsigned(row, 'anchor_receipt_ms', 0, monotonic)
    utc = _timestamp_unsigned(row, 'anchor_utc_ms', 946684800000, 4102444799999)
    if _timestamp_unsigned(row, 'anchor_age_ms', 0, 31536000000) != monotonic - receipt:
        raise ValueError('anchor age')
    if row['uncertainty_known'] == '1':
        _timestamp_unsigned(row, 'uncertainty_ms', 0, 0xffffffff)
    elif row['uncertainty_ms']:
        raise ValueError('unexpected uncertainty')
    if row['has_utc_estimate'] == '1':
        if row['anchor_quality'] != '1' or _timestamp_unsigned(row, 'utc_estimate_ms', 0, 0x7fffffffffffffff) != \
                utc + monotonic - receipt:
            raise ValueError('invalid UTC estimate')
    elif row['utc_estimate_ms']:
        raise ValueError('unexpected UTC estimate')


def _validate_presence(row, flag, fields, message):
    if row[flag] not in ('0', '1'):
        raise ValueError(message)
    if any(bool(row[field]) != (row[flag] == '1') for field in fields):
        raise ValueError(message)


def _validate_imu_presence(row):
    for flag, fields in [('receipt_known', ['receipt_millis32']),
                         ('drain_known', ['drain_start_millis32', 'drain_end_millis32']),
                         ('sensor_time_present', ['sensor_time_ticks24']),
                         ('calibration_time_known', ['calibration_utc_ms']),
                         ('calibration_temperature_known', ['calibration_temperature_millic'])]:
        _validate_presence(row, flag, fields, 'optional field presence')
    for flag, prefixes, suffixes in [
            ('calibration_offsets_known', ('accel', 'gyro'), ('offset_x','offset_y','offset_z')),
            ('calibration_gains_known', ('accel','gyro'),
             ('gain_x_numerator','gain_x_denominator','gain_y_numerator','gain_y_denominator',
              'gain_z_numerator','gain_z_denominator'))]:
        fields = [prefix+'_'+suffix for prefix in prefixes for suffix in suffixes]
        _validate_presence(row, flag, fields, 'coefficient presence')


def _validate_imu_payload(row):
    _validate_raw_counts(row)
    payload = bytes.fromhex(row['event_bytes'])
    if len(payload) != int(row['event_length']) or len(payload) > 4:
        raise ValueError('event payload length')
    if row['sensor_time_present'] == '1':
        _validate_sensor_time_payload(row, payload)


def _validate_raw_counts(row):
    channels = [row[prefix + '_' + axis] for prefix in ('accel', 'gyro') for axis in ('x', 'y', 'z')]
    if row['kind'] == 'imu':
        if any(not value or not -32768 <= int(value) <= 32767 for value in channels):
            raise ValueError('signed raw count')
    elif any(channels):
        raise ValueError('event contains fabricated sample')


def _validate_sensor_time_payload(row, payload):
    if row['kind'] != 'control' or row['event_code'] != '5' or len(payload) != 3:
        raise ValueError('sensor time event')
    if int.from_bytes(payload, 'little') != int(row['sensor_time_ticks24']):
        raise ValueError('sensor time payload')


def _parse_imu_row(values, version):
    extra = MOTION if version in ('#ridesync_telemetry,4', '#ridesync_telemetry,5', '#ridesync_telemetry,6', '#ridesync_telemetry,7') else []
    if version in ('#ridesync_telemetry,5', '#ridesync_telemetry,6', '#ridesync_telemetry,7'): extra = extra + DYNAMIC
    if len(values) != 1 + len(COMMON) + len(IMU) + len(extra):
        raise ValueError(f'IMU column count {len(values)}')
    row = dict(zip(['kind'] + COMMON + IMU + extra, values))
    if row['acquisition_known'] != '0' or row['acquisition_ms']:
        raise ValueError('unsupported acquisition model')
    _validate_imu_presence(row)
    _validate_imu_payload(row)
    return row


def _validate_camera_time(row):
    _validate_camera_provenance(row)
    _validate_presence(row, 'event_receipt_known',
                       ('event_receipt_ms', 'event_receipt_age_ms'),
                       'camera event receipt presence')
    if row['event_receipt_known'] == '1':
        receipt = int(row['event_receipt_ms'])
        age = int(row['event_receipt_age_ms'])
        if receipt < 0 or age < 0 or age > 60000 or receipt + age != int(row['monotonic_ms']):
            raise ValueError('camera event receipt age')


def _validate_camera_provenance(row):
    if row['time_domain'] != 'owner_admission' or row['radio_receipt_known'] != '0' or \
            row['radio_receipt_ms'] or row['acquisition_known'] != '0' or row['acquisition_ms']:
        raise ValueError('camera time provenance')


def _validate_camera_numbers(row):
    numeric = ('peer_slot peer_id model group_generation intent_id connection_generation '
               'operation_generation event_kind operation error recording ack_domain '
               'ack_action delivery_admitted').split()
    if any(not row[field] or not 0 <= int(row[field]) <= 0xffffffff for field in numeric):
        raise ValueError('camera numeric fields')
    ranges = {'peer_slot': (0, 7), 'peer_id': (1, 0xffffffff), 'model': (1, 8),
              'event_kind': (0, 9), 'operation': (0, 4), 'error': (0, 10),
              'recording': (0, 2), 'ack_domain': (0, 2), 'ack_action': (0, 15)}
    if any(not lower <= int(row[field]) <= upper
           for field, (lower, upper) in ranges.items()):
        raise ValueError('camera enum fields')
    if row['delivery_admitted'] not in ('0', '1'):
        raise ValueError('camera enum fields')


def _validate_camera_ack(row, kind):
    if kind == 4:
        if any(int(row[field]) == 0 for field in ('intent_id','connection_generation',
                                                  'operation_generation','ack_domain','ack_action')):
            raise ValueError('uncorrelated camera ACK')
        action, domain = int(row['ack_action']), int(row['ack_domain'])
        if (action in (1, 2) and domain != 2) or (action not in (1, 2) and domain != 1):
            raise ValueError('camera ACK domain')
    elif int(row['ack_domain']) or int(row['ack_action']):
        raise ValueError('unexpected ACK fields')


def _validate_camera_observation(row, kind):
    if kind == 5:
        if int(row['recording']) == 0 or int(row['connection_generation']) == 0:
            raise ValueError('invalid camera observation')
    elif int(row['recording']):
        raise ValueError('unexpected camera observation')


def _validate_camera_request(row, kind):
    if kind in (0, 1, 2):
        _validate_camera_admission(row, kind)
    if kind == 3:
        _validate_camera_attempt(row)
    elif row['delivery_admitted'] != '0':
        raise ValueError('unexpected delivery admission')


def _validate_camera_admission(row, kind):
    _validate_camera_intent(row, kind)
    if kind == 1 and int(row['operation_generation']):
        raise ValueError('queued request borrowed operation')
    if int(row['connection_generation']) or int(row['operation_generation']) or \
            row['delivery_admitted'] != '0':
        raise ValueError('request borrowed token')


def _validate_camera_intent(row, kind):
    if kind in (0, 1) and (int(row['intent_id']) == 0 or int(row['error'])):
        raise ValueError('invalid admitted request')
    if kind == 2 and (int(row['intent_id']) or int(row['error']) == 0):
        raise ValueError('invalid refused request')


def _validate_camera_attempt(row):
    if any(int(row[field]) == 0 for field in
           ('intent_id', 'connection_generation', 'operation_generation')):
        raise ValueError('invalid transport attempt')
    if row['error'] != ('0' if row['delivery_admitted'] == '1' else '8'):
        raise ValueError('invalid transport attempt')


def _validate_camera_state(row):
    _validate_camera_numbers(row)
    kind = int(row['event_kind'])
    _validate_camera_ack(row, kind)
    _validate_camera_observation(row, kind)
    _validate_camera_request(row, kind)


def _parse_camera_row(values):
    if len(values) != 1 + len(COMMON) + len(CAMERA):
        raise ValueError('camera column count')
    row = dict(zip(['kind'] + COMMON + CAMERA, values))
    _validate_camera_time(row)
    _validate_camera_state(row)
    return row


def _parse_health_row(values):
    if len(values) != 1 + len(COMMON) + len(HEALTH):
        raise ValueError('health column count')
    row = dict(zip(['kind'] + COMMON + HEALTH, values))
    bounds = {'peer_slot': (0, 7), 'peer_id': (1, 0xffffffff), 'model': (1, 8),
              'transition_sequence': (1, 0xffffffff), 'transition_kind': (0, 3),
              'reason': (0, 7), 'severity': (0, 3), 'capability': (0, 2),
              'validity': (0, 2), 'profile': (0, 3), 'unit': (0, 5),
              'history_incomplete': (0, 1), 'field': (0, 4), 'historical': (0, 1)}
    for field in HEALTH:
        if field in ('value', 'time_domain', 'sample_receipt_known', 'sample_receipt_ms',
                     'sample_age_ms', 'radio_receipt_known', 'radio_receipt_ms',
                     'acquisition_known', 'acquisition_ms'):
            continue
        _timestamp_unsigned(row, field, *bounds.get(field, (0, 0xffffffff)))
    model = int(row['model'])
    expected_profile = 1 if model == 1 else 3 if model == 4 else 2 if model >= 5 else 0
    if int(row['profile']) not in (0, expected_profile):
        raise ValueError('health model profile')
    kind = int(row['transition_kind'])
    if (kind in (2, 3)) != (row['severity'] == '0'):
        raise ValueError('health transition severity')
    if row['validity'] == '1' and (row['capability'] != '2' or row['profile'] == '0' or row['historical'] != '1'):
        raise ValueError('unqualified health sample')
    unit, reason = int(row['unit']), int(row['reason'])
    field = int(row['field'])
    if reason != 7 and field != {3: 1, 4: 2, 5: 4}.get(reason, 0):
        raise ValueError('health reason field')
    if unit and unit != [1, 2, 3, 4, 5][field]:
        raise ValueError('health field unit')
    if unit == 0 or row['historical'] == '0':
        if row['value'] or row['validity'] == '1':
            raise ValueError('health value presence')
    else:
        maximum = {1: 2, 2: 100, 5: 1}.get(unit, 0xffffffffffffffff)
        minimum = 1 if unit == 1 and row['validity'] == '1' else 0
        _timestamp_unsigned(row, 'value', minimum, maximum)
        expected = {3: 2, 4: 3, 5: 5}.get(reason, 1)
        if reason != 7 and unit != expected:
            raise ValueError('health reason unit')
    if row['time_domain'] != 'owner_admission' or row['radio_receipt_known'] != '0' or \
            row['radio_receipt_ms'] or row['acquisition_known'] != '0' or row['acquisition_ms']:
        raise ValueError('health time provenance')
    _validate_presence(row, 'sample_receipt_known', ('sample_receipt_ms','sample_age_ms'),
                       'health sample receipt presence')
    if row['sample_receipt_known'] == '1':
        if row['historical'] != '1': raise ValueError('health sample history')
        receipt = _timestamp_unsigned(row, 'sample_receipt_ms', 0, 31536000000)
        age = _timestamp_unsigned(row, 'sample_age_ms', 0, 0x7fffffff)
        if receipt + age != int(row['monotonic_ms']):
            raise ValueError('health sample age')
    if (row['history_incomplete'] == '1') != any(int(row[f]) for f in
            ('transition_dropped', 'transition_coalesced', 'dropped', 'rejected', 'lost')):
        raise ValueError('health loss declaration')
    return row


BOOKMARK = 'bookmark_id bookmark_source bookmark_gesture'.split()
MARKER = ('bookmark_id peer_slot peer_id model connection_generation operation_generation '
          'event_kind error delivery_admitted time_domain event_receipt_known event_receipt_ms '
          'event_receipt_age_ms accepted dropped rejected lost written flushed').split()

def _parse_bookmark(values):
    if len(values) != 39 or any(values[13:30]):
        raise ValueError('bookmark columns')
    row = dict(zip(['kind'] + COMMON, values[:13]))
    row.update(zip('accepted dropped rejected lost written flushed'.split(), values[30:36]))
    row.update(zip(BOOKMARK, values[36:39]))
    _timestamp_unsigned(row, 'bookmark_id', 1, 0xffffffff)
    pair = (row['bookmark_source'], row['bookmark_gesture'])
    if pair not in (('1','0'), ('2','1'), ('2','2'), ('2','3')):
        raise ValueError('bookmark source gesture')
    for field in ('accepted','dropped','rejected','lost','written','flushed'):
        _timestamp_unsigned(row, field, 0, 0xffffffff)
    return row

def _parse_marker(values):
    if len(values) != 32: raise ValueError('marker columns')
    row = dict(zip(['kind'] + COMMON + MARKER, values))
    bounds = {'bookmark_id': (1, 0xffffffff), 'peer_slot': (0, 7), 'peer_id': (1, 0xffffffff),
              'model': (1, 8), 'event_kind': (0, 4), 'error': (0, 10), 'delivery_admitted': (0, 1)}
    for field in MARKER:
        if field in ('time_domain','event_receipt_known','event_receipt_ms','event_receipt_age_ms'): continue
        _timestamp_unsigned(row, field, *bounds.get(field, (0, 0xffffffff)))
    if row['time_domain'] != 'owner_admission': raise ValueError('marker provenance')
    _validate_presence(row, 'event_receipt_known', ('event_receipt_ms','event_receipt_age_ms'),
                       'marker receipt presence')
    if row['event_receipt_known'] == '1':
        receipt = _timestamp_unsigned(row, 'event_receipt_ms', 0, 31536000000)
        age = _timestamp_unsigned(row, 'event_receipt_age_ms', 0, 60000)
        if receipt + age != int(row['monotonic_ms']): raise ValueError('marker receipt age')
    kind = int(row['event_kind'])
    if kind in (1, 2) and any(row[f] == '0' for f in ('connection_generation','operation_generation')):
        raise ValueError('marker token')
    if (kind == 2 and row['error'] != '0') or (kind != 1 and row['delivery_admitted'] != '0'):
        raise ValueError('marker result')
    if kind == 1 and row['error'] != ('0' if row['delivery_admitted'] == '1' else '8'):
        raise ValueError('marker attempt')
    return row

def _parse_record(values, version):
    if not values: raise ValueError('empty record')
    if version in ('#ridesync_telemetry,6', '#ridesync_telemetry,7'):
        if values[0] == 'bookmark': return _parse_bookmark(values)
        if values[0] == 'marker': return _parse_marker(values)
        if len(values) < 4 or any(values[-3:]): raise ValueError('unexpected bookmark fields')
        values = values[:-3]
    return _parse_record_base(values, version)


def _parse_record_base(values, version):
    if values[0] == 'gps':
        if len(values) != 36:
            raise ValueError('GPS column count')
        return dict(zip(['kind'] + COMMON, values))
    if values[0] in ('imu', 'config', 'health', 'control'):
        return _parse_imu_row(values, version)
    if values[0] == 'camera' and version in ('#ridesync_telemetry,3', '#ridesync_telemetry,4', '#ridesync_telemetry,5', '#ridesync_telemetry,6', '#ridesync_telemetry,7'):
        return _parse_camera_row(values)
    if values[0] == 'camera_health' and version == '#ridesync_telemetry,7':
        return _parse_health_row(values)
    raise ValueError('unsupported record kind')


def _validate_dynamic_flags(row):
    for field in DYNAMIC[:11]:
        _motion_integer(row, field)
    for field in ('dynamic_requested', 'dynamic_numeric_available', 'dynamic_angles_available'):
        if row[field] not in ('0', '1'): raise ValueError('dynamic flags')
    if row['dynamic_lean_valid'] != '0' or row['dynamic_acceleration_valid'] != '0':
        raise ValueError('unqualified trusted dynamic output')
    if int(row['dynamic_fault']) > 10 or int(row['dynamic_quality']) > 1 or int(row['dynamic_timing_source']) > 2:
        raise ValueError('dynamic enum')


def _dynamic(row):
    _validate_dynamic_flags(row)
    numeric = row['dynamic_numeric_available'] == '1'
    angles = row['dynamic_angles_available'] == '1'
    if numeric:
        _validate_dynamic_evidence(row)
    elif angles or row['dynamic_quality'] != '0':
        raise ValueError('invalid dynamic presence')
    _validate_dynamic_numbers(row, DYNAMIC[11:21], numeric, 'unavailable dynamic number')
    _validate_dynamic_numbers(row, DYNAMIC[21:], numeric and angles, 'unavailable dynamic angle')


def _validate_dynamic_evidence(row):
    required = {'dynamic_requested': '1', 'motion_measurements_valid': '1',
                'kind': 'imu', 'dynamic_quality': '1', 'dynamic_fault': '0',
                'dynamic_timing_source': '1'}
    if any(row[field] != value for field, value in required.items()):
        raise ValueError('dynamic evidence')
    if not int(row['sensor_epoch']) or not int(row['dynamic_cadence_us']) or \
            int(row['dynamic_elapsed_us']) > 2000000:
        raise ValueError('dynamic evidence')


def _validate_dynamic_numbers(row, fields, present, message):
    for field in fields:
        if present:
            _motion_number(row, field)
        elif row[field]:
            raise ValueError(message)


def _validate_record(row, version):
    if version in ('#ridesync_telemetry,3', '#ridesync_telemetry,4', '#ridesync_telemetry,5', '#ridesync_telemetry,6', '#ridesync_telemetry,7'):
        _v3_timestamp(row)
    if version in ('#ridesync_telemetry,4', '#ridesync_telemetry,5', '#ridesync_telemetry,6', '#ridesync_telemetry,7') and row['kind'] in ('imu', 'config', 'health', 'control'):
        _motion(row)
        if version in ('#ridesync_telemetry,5', '#ridesync_telemetry,6', '#ridesync_telemetry,7'): _dynamic(row)
    _validate_record_timestamp(row)


def _validate_record_timestamp(row):
    if row['monotonic_quality'] != '0' or int(row['session_id']) <= 0:
        raise ValueError('invalid timestamp')
    if row['anchor_quality'] != '0' and \
            int(row['monotonic_ms']) - int(row['anchor_receipt_ms']) != int(row['anchor_age_ms']):
        raise ValueError('anchor age')
    if row['has_utc_estimate'] == '1' and \
            int(row['anchor_utc_ms']) + int(row['anchor_age_ms']) != int(row['utc_estimate_ms']):
        raise ValueError('UTC estimate')


def _validate_layout(data):
    version = data.split('\n', 1)[0]
    if version not in ('#ridesync_telemetry,2', '#ridesync_telemetry,3', '#ridesync_telemetry,4', '#ridesync_telemetry,5', '#ridesync_telemetry,6', '#ridesync_telemetry,7'):
        raise ValueError('unsupported version')
    if not data.endswith('\n'):
        raise ValueError('partial trailing row')
    layouts = {'#camera_layout,3,see_docs/log_format.md\n': (3, 'camera'),
               '#imu_layout,4,see_docs/motion_logging.md\n': (4, 'motion'),
               '#dynamic_layout,1,see_docs/dynamic_motion_estimator.md\n': (5, 'dynamic'),
               '#camera_health_layout,1,see_docs/log_format.md\n': (7, 'camera health')}
    number = int(version.rsplit(',', 1)[1])
    for marker, (minimum, name) in layouts.items():
        if number >= minimum and marker not in data:
            raise ValueError('missing '+name+' layout')
    if number in (6, 7) and '#bookmark_layout,1,see_docs/log_format.md\n' not in data:
        raise ValueError('missing bookmark layout')
    return version


def parse(data):
    version = _validate_layout(data)
    rows = []
    for line in data.splitlines()[1:]:
        if line.startswith('#') or line.startswith('session_id,'):
            continue
        values = next(csv.reader([line]))
        row = _parse_record(values, version)
        _validate_record(row, version)
        rows.append(row)
    return rows
