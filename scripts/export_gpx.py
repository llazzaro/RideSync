#!/usr/bin/env python3
"""Export one RideSync CSV session to GPX 1.1 with an auditable JSON report."""
import argparse
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.ride_export.contracts import EXPORTER_VERSION, FormatError  # noqa: E402
from tools.ride_export.gpx import write_gpx  # noqa: E402
from tools.ride_export.publication import (  # noqa: E402
    publish_exclusive, staged_text, validate_paths,
)
from tools.ride_export.reader import read_events  # noqa: E402
from tools.ride_export.report import HashingReader, ReportWriter  # noqa: E402


def _arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', action='version', version=EXPORTER_VERSION)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--time-source', choices=('solution', 'receipt-estimate'),
                        default='solution', help='use solution UTC or explicit receipt estimates')
    parser.add_argument('--max-gap-ms', type=int, default=10000,
                        help='split when GPS receipt gap exceeds this positive threshold')
    parser.add_argument('--salvage', action='store_true',
                        help='discard malformed data rows/tail with diagnostics and segment breaks')
    args = parser.parse_args(argv)
    if args.max_gap_ms <= 0:
        parser.error('--max-gap-ms must be positive')
    return args


def _identity(stat):
    return stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns


def main(argv=None) -> int:
    args = _arguments(argv)
    published = False
    try:
        validate_paths(args.input, args.output, args.report)
        with args.input.open('rb') as source:
            before = os.fstat(source.fileno())
            observed = HashingReader(source)
            with staged_text(args.output) as (gpx_stage, gpx_output), \
                    staged_text(args.report) as (report_stage, report_output):
                report = ReportWriter(report_output)
                summary = write_gpx(read_events(observed, salvage=args.salvage), gpx_output,
                    time_source=args.time_source, max_gap_ms=args.max_gap_ms,
                    diagnostic_sink=report.diagnostic, salvage=args.salvage)
                if (_identity(before) != _identity(os.fstat(source.fileno())) or
                        observed.bytes_read != before.st_size):
                    raise OSError('input changed during export; retry from an immutable copy')
                options = {'time_source': args.time_source, 'max_gap_ms': args.max_gap_ms,
                           'salvage': args.salvage}
                report.finish(summary, args.input, observed, options)
                # Flush both complete staged files before publishing either. This
                # does not promise hardware durability or a two-file transaction.
                gpx_output.flush()
                report_output.flush()
                publish_exclusive(gpx_stage, args.output)
                published = True
                publish_exclusive(report_stage, args.report)
        discarded = summary.skipped_rows + summary.truncated_rows
        print(f'Exported {summary.emitted_points} points in {summary.segments} segments; '
              f'{summary.excluded_points} excluded, {summary.duplicate_points} duplicates, '
              f'{summary.untimed_points} untimed, {discarded} malformed rows discarded. '
              f'GPX: {args.output}; report: {args.report}', file=sys.stderr)
        return 3 if discarded else 0
    except FormatError as error:
        print(f'Export refused: {error}', file=sys.stderr)
        return 2
    except ValueError as error:
        print(f'Export refused: {error}', file=sys.stderr)
        return 2
    except OSError as error:
        print(f'Export I/O failure: {error}', file=sys.stderr)
        if published:
            print(f'GPX was already published at {args.output}; report publication failed. '
                  'Keep that GPX and choose new output paths for a retry.', file=sys.stderr)
        return 4


if __name__ == '__main__':
    raise SystemExit(main())
