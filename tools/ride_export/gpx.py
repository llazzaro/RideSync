"""Streaming GPX 1.1 projection of validated RideSync evidence (stdlib only)."""
from collections import Counter
from dataclasses import fields
from datetime import datetime, timedelta, timezone
from typing import Callable, Iterable, TextIO
from xml.sax.saxutils import escape

from .contracts import (
    CounterObservation, Counters, Diagnostic, Discontinuity, ExportSummary, FormatError, GpsEvent,
    IgnoredEvent, Metadata, StreamEvent,
)

GPX_NAMESPACE = 'http://www.topografix.com/GPX/1/1'
EXPORT_NAMESPACE = 'urn:ridesync:gpx:export:1'
_COUNTERS = tuple(f.name for f in fields(Counters))
_KINDS = ('imu', 'config', 'health', 'control', 'camera')
_BREAKS = {'skipped_row', 'truncated_tail', 'bad_fix', 'loss_increase',
           'counter_decrease', 'backward_monotonic', 'backward_receipt', 'receipt_gap',
           'time_change', 'nonincreasing_utc', 'anchor_change', 'conflicting_solution'}


def _tag(name, value, prefix=''):
    return f'<{prefix}{name}>{escape(str(value))}</{prefix}{name}>'


def _time(event, mode):
    ts = event.timestamp
    if mode == 'solution':
        if event.source_utc_date and event.source_utc_time:
            value = event.source_utc_date + 'T' + event.source_utc_time + 'Z'
            return value, datetime.fromisoformat(value), 'solution'
    elif (ts.has_utc_estimate and ts.anchor_quality == 1 and ts.monotonic_quality == 0
          and ts.utc_estimate_ms is not None):
        try:
            date = datetime(1970, 1, 1, tzinfo=timezone.utc) + timedelta(milliseconds=ts.utc_estimate_ms)
        except (OverflowError, ValueError) as exc:
            raise FormatError(Diagnostic(event.line, 'utc_estimate_ms', 'unrepresentable_utc',
                                        'Use solution mode or repair the source.', 'error')) from exc
        return date.isoformat(timespec='milliseconds').replace('+00:00', 'Z'), date, 'receipt_estimate'
    return None, None, 'absent'


def _point_xml(event, time_text, basis, longitude):
    ts = event.timestamp
    result = [f'      <trkpt lat="{event.latitude_deg:.8f}" lon="{longitude:.8f}">']
    for name, value in (('ele', event.altitude_msl_m), ('time', time_text), ('sat', event.satellites)):
        if value is not None:
            result.append(_tag(name, format(value, 'f') if name == 'ele' else value))
    result.append('<extensions><rs:point>')
    evidence = [('session_id', ts.session_id), ('input_line', event.line),
                ('monotonic_ms', ts.monotonic_ms), ('monotonic_quality', ts.monotonic_quality),
                ('fix_receipt_ms', event.fix_receipt_ms), ('fix_age_ms', event.fix_age_ms),
                ('fix_validity', event.fix_validity), ('fix_valid', int(event.fix_valid)),
                ('fix_quality', event.fix_quality), ('time_basis', basis),
                ('source_utc_date', event.source_utc_date), ('source_utc_time', event.source_utc_time),
                ('anchor_quality', ts.anchor_quality), ('anchor_sequence', ts.anchor_sequence),
                ('anchor_receipt_ms', ts.anchor_receipt_ms), ('anchor_utc_ms', ts.anchor_utc_ms),
                ('anchor_age_ms', ts.anchor_age_ms), ('has_utc_estimate', int(ts.has_utc_estimate)),
                ('utc_estimate_ms', ts.utc_estimate_ms), ('uncertainty_known', int(ts.uncertainty_known)),
                ('uncertainty_ms', ts.uncertainty_ms), ('speed_m_s', event.speed_m_s),
                ('course_deg', event.course_deg)]
    for name, value in evidence:
        if value is not None:
            result.append(_tag(name, format(value, 'f') if name in ('speed_m_s', 'course_deg') else value, 'rs:'))
    result.append('</rs:point></extensions></trkpt>\n')
    return ''.join(result)


def write_gpx(events: Iterable[StreamEvent], output: TextIO, *, time_source: str = 'solution',
              max_gap_ms: int = 10000, diagnostic_sink: Callable[[Diagnostic], None] | None = None,
              salvage: bool = False) -> ExportSummary:
    """Keep one prior solution and fixed domain counters; never collect XML/events.

    A strict failure may leave partial XML in the caller's temporary output. Only
    publish it after this function returns successfully. Reader validation owns
    field ranges, calendars and association equations; constructors are trusted.
    """
    if time_source not in ('solution', 'receipt-estimate') or max_gap_ms < 0:
        raise ValueError('Invalid time source or negative maximum gap')
    metadata = None
    counts = Counter()
    reasons = Counter()
    ignored = Counter()
    previous_counters = {}
    observations = {}
    prior_monotonic = None
    prior_solution = None
    prior_point = None
    pending = set()
    segment_size = 0

    def diagnose(line, field, reason, remedy, severity='warning'):
        diagnostic = Diagnostic(line, field, reason, remedy, severity)
        if diagnostic_sink:
            diagnostic_sink(diagnostic)
        return diagnostic

    def close_segment():
        nonlocal segment_size
        if segment_size:
            output.write('    </trkseg>\n')
            counts['single_point_segments'] += segment_size == 1
            segment_size = 0

    for event in events:
        if isinstance(event, Metadata):
            if metadata is not None:
                raise FormatError(Diagnostic(event.line, 'metadata', 'duplicate_metadata', 'Export one session.', 'error'), category='header')
            metadata = event
            output.write(f'<?xml version="1.0" encoding="UTF-8"?>\n<gpx xmlns="{GPX_NAMESPACE}" xmlns:rs="{EXPORT_NAMESPACE}" version="1.1" creator="RideSync offline exporter">\n  <trk>\n')
            output.write('    ' + _tag('name', f'RideSync session {metadata.session_id}') + '\n')
            continue
        if metadata is None:
            raise FormatError(Diagnostic(1, 'metadata', 'missing_metadata', 'Supply a validated reader stream.', 'error'), category='header')
        if isinstance(event, Diagnostic):
            if diagnostic_sink:
                diagnostic_sink(event)
            continue
        if isinstance(event, Discontinuity):
            reason = {'salvaged_tail': 'truncated_tail', 'rejected_row': 'skipped_row'}.get(event.reason, event.reason)
            reason = reason if reason in _BREAKS else 'skipped_row'
            pending.add(reason)
            counts['truncated_rows' if reason == 'truncated_tail' else 'skipped_rows'] += 1
            counts['input_rows'] += 1
            continue
        counts['input_rows'] += 1
        ts = event.timestamp
        if ts.session_id != metadata.session_id:
            raise FormatError(diagnose(event.line, 'session_id', 'session_mismatch', 'Export the original single session.', 'error'), category='session')
        if prior_monotonic is not None and ts.monotonic_ms < prior_monotonic:
            pending.add('backward_monotonic')
            diagnose(event.line, 'monotonic_ms', 'backward_monotonic', 'Check reset or session identity misuse.')
        prior_monotonic = ts.monotonic_ms
        domain = event.kind if isinstance(event, IgnoredEvent) else 'global'
        if domain not in (*_KINDS, 'global'):
            raise ValueError('Unknown counter domain')
        previous = previous_counters.get(domain)
        maxima, increases, decreases, initial, saturated = observations.setdefault(
            domain, (dict.fromkeys(_COUNTERS, 0), Counter(), Counter(), set(), set()))
        for name in _COUNTERS:
            value = getattr(event.counters, name)
            maxima[name] = max(maxima[name], value)
            if value == 4294967295:
                saturated.add(name)
            if previous is None and value and name in ('dropped', 'rejected', 'lost'):
                initial.add(name)
                pending.add('loss_increase')
                diagnose(event.line, name, 'initial_nonzero_loss', 'Counters are lower-bound observations, not GPS loss counts.')
            elif previous is not None:
                old = getattr(previous, name)
                if value < old:
                    decreases[name] += 1
                    pending.add('counter_decrease')
                    diagnose(event.line, name, 'counter_decrease', 'Check corruption or session identity misuse.')
                elif value > old:
                    increases[name] += 1
                    if name in ('dropped', 'rejected', 'lost'):
                        pending.add('loss_increase')
                        diagnose(event.line, name, 'loss_increase', 'Inspect logger loss; do not infer missing GPS point counts.')
        previous_counters[domain] = event.counters
        if isinstance(event, IgnoredEvent):
            ignored[event.kind] += 1
            continue
        counts['gps_rows'] += 1
        if event.fix_validity != 1 or not event.fix_valid:
            counts['excluded_points'] += 1
            pending.add('bad_fix')
            continue
        payload = tuple(getattr(event, name) for name in ('latitude_deg', 'longitude_deg',
                         'altitude_msl_m', 'speed_m_s', 'course_deg', 'source_utc_date',
                         'source_utc_time', 'satellites', 'fix_quality'))
        if prior_solution and event.fix_receipt_ms == prior_solution[0]:
            if payload != prior_solution[1]:
                diagnostic = diagnose(event.line, 'fix_receipt_ms', 'conflicting_solution',
                                      'Repair source evidence or explicitly salvage.', 'error')
                if not salvage:
                    raise FormatError(diagnostic)
                counts['skipped_rows'] += 1
                pending.add('conflicting_solution')
                continue
            if not pending:
                counts['duplicate_points'] += 1
                continue
        try:
            time_text, time_value, basis = _time(event, time_source)
        except FormatError as error:
            if diagnostic_sink:
                diagnostic_sink(error.diagnostic)
            if not salvage:
                raise
            counts['skipped_rows'] += 1
            pending.add('skipped_row')
            continue
        if time_source == 'solution' and bool(event.source_utc_date) != bool(event.source_utc_time):
            diagnose(event.line, 'source_utc_time', 'partial_solution_utc', 'Retain this untimed point; no clock is invented.')
        if prior_point:
            receipt, previous_time, previous_basis, sequence = prior_point
            if event.fix_receipt_ms < receipt:
                pending.add('backward_receipt')
                diagnose(event.line, 'fix_receipt_ms', 'backward_receipt', 'Check source clock continuity.')
            elif event.fix_receipt_ms - receipt > max_gap_ms:
                pending.add('receipt_gap')
            if basis != previous_basis:
                pending.add('time_change')
            if time_value is not None and previous_time is not None and time_value <= previous_time:
                pending.add('nonincreasing_utc')
            if time_source == 'receipt-estimate' and ts.anchor_sequence != sequence:
                pending.add('anchor_change')
        if pending or not segment_size:
            close_segment()
            if counts['emitted_points']:
                reasons.update(pending)
            pending.clear()
            output.write('    <trkseg>\n')
            counts['segments'] += 1
        longitude = event.longitude_deg
        if longitude == 180:
            longitude = -longitude
            counts['normalized_longitudes'] += 1
            diagnose(event.line, 'longitude_deg', 'longitude_normalized', 'Exactly +180 is represented as -180 for GPX.')
        output.write(_point_xml(event, time_text, basis, longitude))
        counts['emitted_points'] += 1
        counts['untimed_points'] += time_text is None
        if basis == 'receipt_estimate' and not ts.uncertainty_known:
            counts['unknown_uncertainty_points'] += 1
            diagnose(event.line, 'uncertainty_known', 'unmeasured_receipt_estimate', 'Freshness does not measure UART latency or oscillator drift.')
        segment_size += 1
        prior_solution = (event.fix_receipt_ms, payload)
        prior_point = (event.fix_receipt_ms, time_value, basis, ts.anchor_sequence)
    if metadata is None or not counts['emitted_points']:
        raise FormatError(Diagnostic(0, 'points', 'no_points', 'Supply a log with a valid current GPS fix.', 'error'), category='no_points')
    close_segment()
    output.write('  </trk>\n</gpx>\n')
    domains = tuple(CounterObservation(scope=scope, maxima=Counters(**values[0]),
                    initial_nonzero=tuple(sorted(values[3])), increases=tuple(sorted(values[1].items())),
                    decreases=tuple(sorted(values[2].items())), saturated=tuple(sorted(values[4])))
                    for scope, values in sorted(observations.items()))
    global_observation = next((value for value in domains if value.scope == 'global'),
                              CounterObservation('global', Counters(0, 0, 0, 0, 0, 0)))
    return ExportSummary(metadata=metadata, **counts, ignored_kinds=tuple(sorted(ignored.items())),
                         segment_reasons=tuple(sorted(reasons.items())),
                         counter_maxima=global_observation.maxima,
                         counter_initial_nonzero=global_observation.initial_nonzero,
                         counter_increases=global_observation.increases,
                         counter_decreases=global_observation.decreases,
                         saturated_counters=global_observation.saturated, counter_domains=domains)
