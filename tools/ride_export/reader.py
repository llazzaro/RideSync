"""Bounded, strict decoder for current RideSync ASCII telemetry formats."""
from collections.abc import Iterator
from datetime import date
from decimal import Decimal, InvalidOperation
import re
from typing import BinaryIO

from .contracts import (MAX_LINE_BYTES, GPS_COLUMNS, SCHEMAS, Counters, Diagnostic,
    Discontinuity, FormatError, GpsEvent, IgnoredEvent, Metadata, StreamEvent, Timestamp)

_DURATION = 31536000000
_U32 = 0xffffffff
_U64 = 0xffffffffffffffff
_COUNTERS = GPS_COLUMNS[-6:]
# GPS firmware serializes fixed decimal fields, never exponent notation.
_NUMBER = re.compile(r'-?[0-9]+(?:\.[0-9]+)?\Z')
_TOKEN = re.compile(r'[A-Za-z0-9_.-]{1,48}\Z')


def _fail(line, field, reason, category='row'):
    raise FormatError(Diagnostic(line, field, reason,
        'Use an intact log with the documented schema and source evidence.', 'error'),
        category=category)


def _uint(value, line, field, upper=_U32, lower=0):
    if not re.fullmatch(r'[0-9]+', value):
        _fail(line, field, 'expected decimal unsigned integer')
    number = int(value)
    if not lower <= number <= upper:
        _fail(line, field, 'integer outside permitted range')
    return number


def _flag(value, line, field):
    if value not in ('0', '1'):
        _fail(line, field, 'expected boolean 0 or 1')
    return value == '1'


def _optional_uint(value, line, field, upper=_U32):
    return None if value == '' else _uint(value, line, field, upper)


def _timestamp(values, line, session):
    row = dict(zip(GPS_COLUMNS[:12], values))
    try:
        row_session = _uint(row['session_id'], line, 'session_id', _U64, 1)
    except FormatError as error:
        raise FormatError(error.diagnostic, category='session') from None
    if row_session != session:
        _fail(line, 'session_id', 'row does not belong to declared session', 'session')
    mono = _uint(row['monotonic_ms'], line, 'monotonic_ms', _DURATION)
    if row['monotonic_quality'] != '0':
        _fail(line, 'monotonic_quality', 'only valid monotonic time is supported')
    quality = _uint(row['anchor_quality'], line, 'anchor_quality', 2)
    known = _flag(row['uncertainty_known'], line, 'uncertainty_known')
    has = _flag(row['has_utc_estimate'], line, 'has_utc_estimate')
    sequence = receipt = utc = uncertainty = age = estimate = None
    if quality == 0:
        absent = ('anchor_sequence', 'anchor_receipt_ms', 'anchor_utc_ms',
                  'uncertainty_ms', 'anchor_age_ms', 'utc_estimate_ms')
        if known or has or any(row[field] for field in absent):
            _fail(line, 'anchor_quality', 'missing anchor has present association fields')
    else:
        sequence = _uint(row['anchor_sequence'], line, 'anchor_sequence', _U32, 1)
        receipt = _uint(row['anchor_receipt_ms'], line, 'anchor_receipt_ms', mono)
        utc = _uint(row['anchor_utc_ms'], line, 'anchor_utc_ms', 4102444799999, 946684800000)
        age = _uint(row['anchor_age_ms'], line, 'anchor_age_ms', _DURATION)
        if age != mono - receipt:
            _fail(line, 'anchor_age_ms', 'age disagrees with receipt and monotonic time')
        if known:
            uncertainty = _uint(row['uncertainty_ms'], line, 'uncertainty_ms')
        elif row['uncertainty_ms']:
            _fail(line, 'uncertainty_ms', 'unknown uncertainty must be absent')
        if has:
            estimate = _uint(row['utc_estimate_ms'], line, 'utc_estimate_ms', (1 << 63) - 1)
            if quality != 1 or estimate != utc + age:
                _fail(line, 'utc_estimate_ms', 'estimate disagrees with fresh anchor association')
        elif row['utc_estimate_ms']:
            _fail(line, 'utc_estimate_ms', 'unavailable estimate must be absent')
        if (quality == 1) != has:
            _fail(line, 'has_utc_estimate', 'estimate availability disagrees with anchor quality')
    return Timestamp(session, mono, 0, quality, sequence, receipt, utc, known,
                     uncertainty, age, has, estimate)


def _metric(value, line, field, lower, upper):
    if not value:
        return None
    if not _NUMBER.fullmatch(value):
        _fail(line, field, 'expected finite decimal number')
    try:
        number = Decimal(value)
    except InvalidOperation:
        _fail(line, field, 'decimal cannot be represented')
    if not number.is_finite() or not lower <= number <= upper:
        _fail(line, field, 'number outside permitted finite range')
    return number


def _calendar(value, line, field):
    if not value:
        return None
    if field == 'source_utc_date':
        if not re.fullmatch(r'20[0-9]{2}-[0-9]{2}-[0-9]{2}', value):
            _fail(line, field, 'expected Gregorian date in 2000 through 2099')
        try:
            date.fromisoformat(value)
        except ValueError:
            _fail(line, field, 'invalid Gregorian date')
    else:
        if not re.fullmatch(r'[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{2}', value):
            _fail(line, field, 'expected ordinary UTC time with centiseconds')
        if int(value[:2]) > 23 or int(value[3:5]) > 59 or int(value[6:8]) > 59:
            _fail(line, field, 'invalid UTC time; leap seconds are unsupported')
    return value


def _gps(values, line, stamp, counters):
    row = dict(zip(GPS_COLUMNS, values))
    state = _uint(row['modem_state'], line, 'modem_state', 6)
    health = _uint(row['uart_health'], line, 'uart_health', 6)
    power = _flag(row['gnss_power'], line, 'gnss_power')
    ready = _flag(row['receiver_ready'], line, 'receiver_ready')
    validity = _uint(row['fix_validity'], line, 'fix_validity', 4)
    valid = _flag(row['fix_valid'], line, 'fix_valid')
    # Firmware uses %.8f for coordinates. Reject extra source precision before
    # projection can round a near-antimeridian longitude to forbidden GPX +180.
    for field in ('latitude_deg', 'longitude_deg'):
        if '.' in row[field] and len(row[field].split('.', 1)[1]) > 8:
            _fail(line, field, 'coordinate exceeds eight fractional decimal places')
    lat = _metric(row['latitude_deg'], line, 'latitude_deg', -90, 90)
    lon = _metric(row['longitude_deg'], line, 'longitude_deg', -180, 180)
    if (lat is not None) != valid or (lon is not None) != valid or (validity == 1 and not valid):
        _fail(line, 'fix_valid', 'coordinate presence or current validity disagrees with flag')
    altitude = _metric(row['altitude_msl_m'], line, 'altitude_msl_m', -100000, 100000)
    speed = _metric(row['speed_m_s'], line, 'speed_m_s', 0, 100000)
    course = _metric(row['course_deg'], line, 'course_deg', 0, 360)
    day = _calendar(row['source_utc_date'], line, 'source_utc_date')
    time = _calendar(row['source_utc_time'], line, 'source_utc_time')
    receipt = _optional_uint(row['fix_receipt_ms'], line, 'fix_receipt_ms', stamp.monotonic_ms)
    age = _optional_uint(row['fix_age_ms'], line, 'fix_age_ms', _DURATION)
    needs_receipt = valid or day is not None or time is not None or age is not None
    if needs_receipt != (receipt is not None):
        _fail(line, 'fix_receipt_ms', 'receipt presence disagrees with GPS evidence')
    if age is not None and age != stamp.monotonic_ms - receipt:
        _fail(line, 'fix_age_ms', 'GPS age disagrees with receipt and monotonic time')
    sat = _optional_uint(row['satellites'], line, 'satellites', 65535)
    quality = _optional_uint(row['fix_quality'], line, 'fix_quality', 255)
    return GpsEvent(line, stamp, counters, state, health, power, ready, validity,
        age, valid, lat, lon, altitude, speed, course, day, time, receipt, sat, quality)


def _row(text, line, metadata):
    values = text.split(',')
    kind = 'gps' if metadata.version == 1 else values.pop(0)
    # Session ownership precedes salvageable syntax/layout failures: even an
    # otherwise malformed row cannot hide a parseable foreign session ID.
    if values and re.fullmatch(r'[0-9]+', values[0]):
        row_session = int(values[0])
        if not 1 <= row_session <= _U64 or row_session != metadata.session_id:
            _fail(line, 'session_id', 'row does not belong to declared session', 'session')
    if kind == 'gps':
        expected, offset = 35, 29
    elif kind in ('imu', 'config', 'health', 'control') and metadata.version >= 2:
        expected = 87 + (36 if metadata.version >= 4 else 0) + (23 if metadata.version == 5 else 0)
        offset = 81
    elif kind == 'camera' and metadata.version >= 3:
        expected, offset = 40, 34
    else:
        _fail(line, 'kind', 'unsupported record kind for schema', 'version')
    if '"' in text or any(ord(char) < 32 for char in text):
        _fail(line, 'row', 'quoted fields or control characters are unsupported')
    if len(values) != expected:
        _fail(line, 'row', 'incorrect version-specific column count')
    stamp = _timestamp(values[:12], line, metadata.session_id)
    counters = Counters(*(_uint(value, line, field) for field, value in
                          zip(_COUNTERS, values[offset:offset + 6])))
    if kind == 'gps':
        return _gps(values, line, stamp, counters)
    return IgnoredEvent(line, kind, stamp, counters)


def _physical_lines(source):
    line = 0
    while True:
        raw = source.readline(MAX_LINE_BYTES)
        if not raw:
            return
        line += 1
        if len(raw) == MAX_LINE_BYTES and not raw.endswith(b'\n'):
            _fail(line, 'line', 'physical line exceeds byte limit', 'line_length')
        try:
            text = raw.decode('ascii')
        except UnicodeDecodeError:
            _fail(line, 'encoding', 'input must be ASCII', 'encoding')
        if '\r' in text:
            _fail(line, 'encoding', 'input requires LF without carriage returns', 'encoding')
        yield line, text[:-1] if raw.endswith(b'\n') else text, raw.endswith(b'\n')


def read_events(source: BinaryIO, *, salvage: bool = False) -> Iterator[StreamEvent]:
    """Yield metadata then bounded validated evidence; never own the input stream."""
    lines = iter(_physical_lines(source))
    last_line = 0

    def required():
        nonlocal last_line
        try:
            number, text, complete = next(lines)
        except StopIteration:
            _fail(last_line + 1, 'header', 'required header or layout missing', 'header')
        last_line = number
        if not complete:
            _fail(number, 'header', 'unterminated required header', 'header')
        return number, text

    line, schema = required()
    markers = ['#ridesync_gps,1'] + [f'#ridesync_telemetry,{v}' for v in range(2, 6)]
    if schema not in markers:
        _fail(line, 'schema', 'unsupported schema marker', 'version')
    version = markers.index(schema) + 1
    line, text = required()
    fields = text.split(',')
    if len(fields) != 6 or fields[0] != '#session' or fields[2] != 'firmware' or fields[4] != 'provenance':
        _fail(line, 'session', 'invalid session and provenance metadata', 'header')
    try:
        session = _uint(fields[1], line, 'session_id', _U64, 1)
    except FormatError as error:
        raise FormatError(error.diagnostic, category='session') from None
    if not _TOKEN.fullmatch(fields[3]) or not _TOKEN.fullmatch(fields[5]):
        _fail(line, 'metadata', 'firmware and provenance must be safe ASCII tokens', 'header')
    firmware, provenance = fields[3], fields[5]
    line, text = required()
    policy = text.split(',')
    names = ['queue_records', 'flush_records', 'idle_flush', 'chunk_bytes', 'mount_attempts', 'write_retries']
    if len(policy) != 13 or policy[0] != '#policy' or policy[1::2] != names:
        _fail(line, 'policy', 'invalid policy layout', 'header')
    expected = [(8, 8), (1, 8), (1, 1), (256, 256), (1, 3), (0, 0)]
    for field, value, (lower, upper) in zip(names, policy[2::2], expected):
        try:
            _uint(value, line, field, upper, lower)
        except FormatError as error:
            raise FormatError(error.diagnostic, category='header') from None
    line, text = required()
    if text != ','.join(GPS_COLUMNS):
        _fail(line, 'header', 'exact named GPS header required', 'header')
    layouts = []
    if version >= 2:
        layouts.append('#imu_layout,2,see_docs/mixed_telemetry.md' if version < 4 else '#imu_layout,4,see_docs/motion_logging.md')
    if version >= 3:
        layouts.append('#camera_layout,3,see_docs/log_format.md')
    if version == 5:
        layouts.append('#dynamic_layout,1,see_docs/dynamic_motion_estimator.md')
    for expected_layout in layouts:
        line, text = required()
        if text != expected_layout:
            _fail(line, 'layout', 'missing or conflicting version-specific layout', 'header')
    metadata = Metadata(SCHEMAS[version - 1], version, session, firmware, provenance,
                        tuple(zip(names, policy[2::2])))
    yield metadata
    for line, text, complete in lines:
        if text.startswith('#') or text.startswith('session_id,'):
            _fail(line, 'header', 'unexpected repeated header or embedded file', 'header')
        if not complete:
            if not salvage:
                _fail(line, 'tail', 'unterminated final data row', 'tail')
            yield Diagnostic(line, 'tail', 'discarded unterminated final row',
                             'Retain the original bytes; obtain an intact log if available.')
            yield Discontinuity(line, 'salvaged_tail')
            return
        try:
            event = _row(text, line, metadata)
        except FormatError as error:
            if not salvage or error.category != 'row':
                raise
            yield error.diagnostic
            yield Discontinuity(line, 'rejected_row')
        else:
            yield event
