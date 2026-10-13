"""Immutable evidence exchanged by the bounded reader, GPX writer and CLI.

Constructors carry already validated evidence; they intentionally do not parse or
validate source rows. None denotes absence, including optional zero-valued data.
"""
from dataclasses import dataclass
from decimal import Decimal
from typing import Literal, TypeAlias

EXPORTER_VERSION = '1.0.0'
MAX_LINE_BYTES = 4096
GPS_COLUMNS = tuple(('session_id monotonic_ms monotonic_quality anchor_quality '
    'anchor_sequence anchor_receipt_ms anchor_utc_ms uncertainty_known uncertainty_ms '
    'anchor_age_ms has_utc_estimate utc_estimate_ms modem_state uart_health gnss_power '
    'receiver_ready fix_validity fix_age_ms fix_valid latitude_deg longitude_deg '
    'altitude_msl_m speed_m_s course_deg source_utc_date source_utc_time '
    'fix_receipt_ms satellites fix_quality accepted dropped rejected lost written flushed').split())
SCHEMAS = ('GpsV1', 'MixedV2', 'CameraV3', 'MotionV4', 'MotionV5')


@dataclass(frozen=True, slots=True)
class Metadata:
    schema: str
    version: int
    session_id: int
    firmware: str
    provenance: str
    policy: tuple[tuple[str, str], ...]
    line: int = 1


@dataclass(frozen=True, slots=True)
class Timestamp:
    session_id: int
    monotonic_ms: int
    monotonic_quality: int
    anchor_quality: int
    anchor_sequence: int | None
    anchor_receipt_ms: int | None
    anchor_utc_ms: int | None
    uncertainty_known: bool
    uncertainty_ms: int | None
    anchor_age_ms: int | None
    has_utc_estimate: bool
    utc_estimate_ms: int | None


@dataclass(frozen=True, slots=True)
class Counters:
    accepted: int
    dropped: int
    rejected: int
    lost: int
    written: int
    flushed: int


@dataclass(frozen=True, slots=True)
class GpsEvent:
    line: int
    timestamp: Timestamp
    counters: Counters
    modem_state: int
    uart_health: int
    gnss_power: bool
    receiver_ready: bool
    fix_validity: int
    fix_age_ms: int | None
    fix_valid: bool
    latitude_deg: Decimal | None
    longitude_deg: Decimal | None
    altitude_msl_m: Decimal | None
    speed_m_s: Decimal | None
    course_deg: Decimal | None
    source_utc_date: str | None
    source_utc_time: str | None
    fix_receipt_ms: int | None
    satellites: int | None
    fix_quality: int | None


@dataclass(frozen=True, slots=True)
class IgnoredEvent:
    line: int
    kind: str
    timestamp: Timestamp
    counters: Counters


@dataclass(frozen=True, slots=True)
class Diagnostic:
    line: int
    field: str
    reason: str
    remedy: str
    severity: Literal['warning', 'error'] = 'warning'


@dataclass(frozen=True, slots=True)
class Discontinuity:
    line: int
    reason: str


StreamEvent: TypeAlias = Metadata | GpsEvent | IgnoredEvent | Diagnostic | Discontinuity
ErrorCategory: TypeAlias = Literal['header', 'version', 'session', 'row', 'encoding',
                                   'line_length', 'tail', 'no_points']


class FormatError(ValueError):
    """Fatal input/track failure; no successful export may be published."""

    def __init__(self, diagnostic: Diagnostic, *, category: ErrorCategory = 'row'):
        self.diagnostic = diagnostic
        self.category = category
        super().__init__(f'line {diagnostic.line}: {diagnostic.field}: '
                         f'{diagnostic.reason}. {diagnostic.remedy}')


@dataclass(frozen=True, slots=True)
class CounterObservation:
    scope: str
    maxima: Counters
    initial_nonzero: tuple[str, ...] = ()
    increases: tuple[tuple[str, int], ...] = ()
    decreases: tuple[tuple[str, int], ...] = ()
    saturated: tuple[str, ...] = ()


@dataclass(frozen=True, slots=True)
class ExportSummary:
    """Fixed-size counters; tuples contain only bounded known kinds/reasons."""
    metadata: Metadata
    input_rows: int = 0
    gps_rows: int = 0
    emitted_points: int = 0
    excluded_points: int = 0
    duplicate_points: int = 0
    untimed_points: int = 0
    unknown_uncertainty_points: int = 0
    normalized_longitudes: int = 0
    segments: int = 0
    single_point_segments: int = 0
    skipped_rows: int = 0
    truncated_rows: int = 0
    ignored_kinds: tuple[tuple[str, int], ...] = ()
    segment_reasons: tuple[tuple[str, int], ...] = ()
    counter_maxima: Counters = Counters(0, 0, 0, 0, 0, 0)
    counter_initial_nonzero: tuple[str, ...] = ()
    counter_increases: tuple[tuple[str, int], ...] = ()
    counter_decreases: tuple[tuple[str, int], ...] = ()
    saturated_counters: tuple[str, ...] = ()
    counter_domains: tuple[CounterObservation, ...] = ()
