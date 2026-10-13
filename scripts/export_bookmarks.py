#!/usr/bin/env python3
"""Stream BookmarkV6 logs to a portable sidecar and optional REAPER script."""

import argparse
import csv
from decimal import Decimal, InvalidOperation
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile

try:
    from .bookmark_row_validation import validate_row
except ImportError:  # Direct CLI execution from scripts/.
    from bookmark_row_validation import validate_row


SCHEMA = 'ridesync-markers-v1'
SOURCE_VERSIONS = ('#ridesync_telemetry,6', '#ridesync_telemetry,7')
ROW_LIMIT = 3072
ALIGNMENT_LIMIT = 65536
MAX_CLIPS = 16
MAX_SESSION_MS = 31536000000
UINT32 = 0xffffffff
UINT64 = 0xffffffffffffffff
HEADER = ('schema,session_id,bookmark_id,monotonic_ms,source,gesture,anchor_quality,'
          'anchor_sequence,anchor_receipt_ms,anchor_utc_ms,anchor_age_ms,'
          'uncertainty_known,uncertainty_ms,utc_estimate_ms\n')
INPUT_HEADER = ('session_id,monotonic_ms,monotonic_quality,anchor_quality,'
                'anchor_sequence,anchor_receipt_ms,anchor_utc_ms,uncertainty_known,'
                'uncertainty_ms,anchor_age_ms,has_utc_estimate,utc_estimate_ms,'
                'modem_state,uart_health,gnss_power,receiver_ready,fix_validity,'
                'fix_age_ms,fix_valid,latitude_deg,longitude_deg,altitude_msl_m,'
                'speed_m_s,course_deg,source_utc_date,source_utc_time,fix_receipt_ms,'
                'satellites,fix_quality,accepted,dropped,rejected,lost,written,flushed,'
                'bookmark_id,bookmark_source,bookmark_gesture')
LAYOUTS = {'#imu_layout,4,see_docs/motion_logging.md',
           '#camera_layout,3,see_docs/log_format.md',
           '#dynamic_layout,1,see_docs/dynamic_motion_estimator.md',
           '#bookmark_layout,1,see_docs/log_format.md'}
HEALTH_LAYOUT = '#camera_health_layout,1,see_docs/log_format.md'
ROW_WIDTHS = {'gps': 39, 'imu': 150, 'config': 150, 'health': 150,
              'control': 150, 'camera': 44, 'bookmark': 39, 'marker': 32,
              'camera_health': 54}
QUALITY = ('Missing', 'Fresh', 'Expired')
OUTPUT_NAMES = ('ridesync-markers-v1.csv', 'ridesync-markers-report-v1.json',
                'ridesync-reaper-v1.lua')


class ExportError(ValueError):
    """Invalid input or publication state."""


def integer(value, field, lower, upper):
    if not value or not value.isascii() or not value.isdecimal():
        raise ExportError(f'invalid {field}')
    parsed = int(value)
    if not lower <= parsed <= upper:
        raise ExportError(f'out-of-range {field}')
    return parsed


def decimal(value, field, *, nonnegative=True):
    if not isinstance(value, str) or not re.fullmatch(r'[0-9]+(?:\.[0-9]+)?', value):
        raise ExportError(f'invalid {field}')
    try:
        parsed = Decimal(value)
    except InvalidOperation as error:
        raise ExportError(f'invalid {field}') from error
    if parsed > Decimal('31536000000') or (nonnegative and parsed < 0):
        raise ExportError(f'out-of-range {field}')
    return parsed


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(65536), b''):
            digest.update(chunk)
    return digest.hexdigest()


def session_metadata(line):
    fields = line.split(',')
    if len(fields) != 6 or fields[0] != '#session' or fields[2] != 'firmware' or \
            fields[4] != 'provenance' or \
            any(not re.fullmatch(r'[A-Za-z0-9_.-]{1,48}', fields[index]) for index in (3, 5)):
        raise ExportError('invalid session metadata')
    return integer(fields[1], 'session_id', 1, UINT64)


def validate_policy(line):
    fields = line.split(',')
    names = ('queue_records', 'flush_records', 'idle_flush', 'chunk_bytes',
             'mount_attempts', 'write_retries')
    limits = ((8, 8), (1, 8), (1, 1), (256, 256), (1, 3), (0, 0))
    if len(fields) != 13 or fields[0] != '#policy' or tuple(fields[1::2]) != names:
        raise ExportError('invalid storage policy metadata')
    for name, value, (lower, upper) in zip(names, fields[2::2], limits):
        integer(value, name, lower, upper)


def load_alignment(path, session_id):
    if path is None:
        return []
    with path.open('rb') as stream:
        data = stream.read(ALIGNMENT_LIMIT + 1)
    if len(data) > ALIGNMENT_LIMIT:
        raise ExportError('alignment file exceeds 65536 bytes')
    try:
        mapping = json.loads(data)
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
        raise ExportError('invalid alignment JSON') from error
    if not isinstance(mapping, dict) or set(mapping) != {'schema', 'session_id', 'clips'} or \
            mapping['schema'] != 'ridesync-alignment-v1' or \
            type(mapping['session_id']) is not int or mapping['session_id'] != session_id or \
            not isinstance(mapping['clips'], list) or len(mapping['clips']) > MAX_CLIPS:
        raise ExportError('invalid alignment contract or session')
    clips = []
    for index, clip in enumerate(mapping['clips'], 1):
        if not isinstance(clip, dict) or set(clip) != {
                'path', 'sha256', 'duration_seconds', 'clip_zero_session_ms',
                'project_start_seconds', 'method', 'uncertainty_ms'}:
            raise ExportError(f'invalid clip {index} fields')
        if not isinstance(clip['path'], str) or not clip['path'] or len(clip['path']) > 1024 or \
                not isinstance(clip['sha256'], str) or not re.fullmatch(r'[0-9a-f]{64}', clip['sha256']):
            raise ExportError(f'invalid clip {index} identity')
        clip_path = Path(clip['path'])
        if not clip_path.is_file() or file_sha256(clip_path) != clip['sha256']:
            raise ExportError(f'clip {index} hash mismatch')
        if type(clip['clip_zero_session_ms']) is not int or \
                not 0 <= clip['clip_zero_session_ms'] <= MAX_SESSION_MS or \
                type(clip['uncertainty_ms']) is not int or \
                not 0 <= clip['uncertainty_ms'] <= UINT32 or \
                not isinstance(clip['method'], str) or \
                not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]{0,63}', clip['method']) or \
                clip['method'] == 'unknown':
            raise ExportError(f'invalid clip {index} alignment')
        duration = decimal(clip['duration_seconds'], 'duration_seconds')
        start = decimal(clip['project_start_seconds'], 'project_start_seconds')
        if duration <= 0:
            raise ExportError('clip duration must be positive')
        clips.append({'index': index, 'path': str(clip_path), 'sha256': clip['sha256'],
                      'duration_seconds': duration,
                      'clip_zero_session_ms': clip['clip_zero_session_ms'],
                      'project_start_seconds': start, 'method': clip['method'],
                      'uncertainty_ms': clip['uncertainty_ms']})
    return clips


def validate_timestamp(cells, session_id):
    session = integer(cells[1], 'session_id', 1, UINT64)
    if session != session_id:
        raise ExportError('bookmark row has wrong session')
    monotonic = integer(cells[2], 'monotonic_ms', 0, MAX_SESSION_MS)
    if cells[3] != '0':
        raise ExportError('bookmark monotonic quality is invalid')
    quality = integer(cells[4], 'anchor_quality', 0, 2)
    known = integer(cells[8], 'uncertainty_known', 0, 1)
    has_utc = integer(cells[11], 'has_utc_estimate', 0, 1)
    if has_utc != (quality == 1):
        raise ExportError('UTC estimate presence disagrees with anchor quality')
    if quality == 0:
        if any(cells[index] for index in (5, 6, 7, 9, 10, 12)) or known or has_utc:
            raise ExportError('missing anchor has associated fields')
    else:
        integer(cells[5], 'anchor_sequence', 1, UINT32)
        receipt = integer(cells[6], 'anchor_receipt_ms', 0, monotonic)
        utc = integer(cells[7], 'anchor_utc_ms', 946684800000, 4102444799999)
        age = integer(cells[10], 'anchor_age_ms', 0, MAX_SESSION_MS)
        if age != monotonic - receipt:
            raise ExportError('anchor age mismatch')
        if known:
            integer(cells[9], 'uncertainty_ms', 0, UINT32)
        elif cells[9]:
            raise ExportError('unexpected uncertainty')
        if has_utc:
            if quality != 1 or integer(cells[12], 'utc_estimate_ms', 0, 0x7fffffffffffffff) != utc + age:
                raise ExportError('invalid UTC estimate')
        elif cells[12]:
            raise ExportError('unexpected UTC estimate')
    return monotonic, quality, known


def parse_bookmark(cells, session_id):
    if len(cells) != 39 or cells[0] != 'bookmark':
        raise ExportError('invalid bookmark row width')
    if any(cells[13:30]):
        raise ExportError('bookmark row has unrelated GPS payload')
    monotonic, quality, known = validate_timestamp(cells, session_id)
    for index, name in enumerate(('accepted', 'dropped', 'rejected', 'lost', 'written', 'flushed'), 30):
        integer(cells[index], name, 0, UINT32)
    bookmark_id = integer(cells[36], 'bookmark_id', 1, UINT32)
    source = integer(cells[37], 'bookmark_source', 1, 2)
    gesture = integer(cells[38], 'bookmark_gesture', 0, 3)
    if (source == 1 and gesture != 0) or (source == 2 and gesture == 0):
        raise ExportError('source and gesture disagree')
    return {
        'schema': SCHEMA, 'session_id': str(session_id), 'bookmark_id': str(bookmark_id),
        'monotonic_ms': str(monotonic), 'source': str(source), 'gesture': str(gesture),
        'anchor_quality': QUALITY[quality], 'anchor_sequence': cells[5],
        'anchor_receipt_ms': cells[6], 'anchor_utc_ms': cells[7],
        'anchor_age_ms': cells[10], 'uncertainty_known': str(known),
        'uncertainty_ms': cells[9], 'utc_estimate_ms': cells[12],
    }


def rows(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        number = 0
        while True:
            raw = stream.readline(ROW_LIMIT + 1)
            if not raw:
                return
            number += 1
            if len(raw) > ROW_LIMIT:
                raise ExportError(f'line {number} exceeds {ROW_LIMIT} bytes')
            digest.update(raw)
            if not raw.endswith(b'\n'):
                yield number, None, digest.hexdigest(), raw
                return
            if b'\r' in raw or not raw.isascii():
                raise ExportError(f'line {number} is not ASCII LF')
            yield number, raw[:-1].decode('ascii'), digest.hexdigest(), None


def lua_start(stream):
    stream.write('-- RideSync generated REAPER v1 marker import. Review positions below before running.\n')
    stream.write('if not reaper.AddProjectMarker2 then error("REAPER AddProjectMarker2 unavailable") end\n')
    stream.write('local function exists(name, position)\n')
    stream.write('  local _, markers, regions = reaper.CountProjectMarkers(0)\n')
    stream.write('  for i = 0, markers + regions - 1 do\n')
    stream.write('    local ok, isregion, pos, _, oldname = reaper.EnumProjectMarkers3(0, i)\n')
    stream.write('    if ok and not isregion and oldname == name and math.abs(pos-position) < 0.0000001 then return true end\n')
    stream.write('  end\n')
    stream.write('  return false\nend\n')
    stream.write('reaper.Undo_BeginBlock()\n')


def lua_end(stream):
    stream.write('reaper.Undo_EndBlock("Import RideSync bookmarks", -1)\n')
    stream.write('reaper.UpdateArrange()\n')


def export(source, outdir, alignment=None):
    if not source.is_file() or not outdir.is_dir():
        raise ExportError('source file and output directory must exist')
    outputs = [outdir / name for name in OUTPUT_NAMES[:2]]
    if alignment is not None:
        outputs.append(outdir / OUTPUT_NAMES[2])
    if any(path.exists() for path in outputs):
        raise ExportError('output already exists')
    if source.resolve() in (path.resolve() for path in outputs):
        raise ExportError('source path is an output')
    if alignment is not None and alignment.resolve() in (path.resolve() for path in outputs):
        raise ExportError('alignment path is an output')
    with tempfile.TemporaryDirectory(prefix='.ridesync-markers-', dir=outdir) as temp_name:
        temp = Path(temp_name)
        marker_stage = temp / OUTPUT_NAMES[0]
        script_stage = temp / OUTPUT_NAMES[2]
        accepted = excluded_tail = input_rows = last_id = 0
        counter_max = {name: 0 for name in ('accepted', 'dropped', 'rejected', 'lost', 'written', 'flushed')}
        report = {'schema': SCHEMA, 'source_schema': None, 'source_sha256': '',
                  'session_id': None, 'input_rows': 0, 'accepted_markers': 0,
                  'excluded_tail_rows': 0, 'loss_evidence': counter_max,
                  'editor': {'alignment': 'unknown' if alignment is None else 'explicit',
                             'placed': 0, 'outside_clip': 0, 'preview': []}}
        seen = set()
        session_id = None
        source_version = None
        clips = []
        with marker_stage.open('w', encoding='ascii', newline='') as sidecar:
            sidecar.write(HEADER)
            script_context = script_stage.open('w', encoding='ascii', newline='') if alignment else None
            try:
                if script_context:
                    lua_start(script_context)
                started = False
                for line_number, line, source_hash, tail in rows(source):
                    report['source_sha256'] = source_hash
                    if tail is not None:
                        required_layouts = LAYOUTS | ({HEALTH_LAYOUT} if source_version == SOURCE_VERSIONS[1] else set())
                        metadata_complete = session_id is not None and required_layouts.issubset(seen) and \
                            'header' in seen and 'policy' in seen
                        if metadata_complete and tail.split(b',', 1)[0] in {b'gps', b'imu', b'config', b'health',
                                                                   b'control', b'camera', b'bookmark', b'marker',
                                                                   b'camera_health'}:
                            excluded_tail = 1
                            break
                        raise ExportError(f'line {line_number} is unterminated metadata')
                    if line_number == 1:
                        if line not in SOURCE_VERSIONS:
                            raise ExportError('unsupported source schema')
                        source_version = line
                        report['source_schema'] = line
                        continue
                    if not started:
                        if line.startswith('#session,'):
                            if session_id is not None:
                                raise ExportError('duplicate session metadata')
                            session_id = session_metadata(line)
                        elif line == INPUT_HEADER:
                            if 'header' in seen:
                                raise ExportError('duplicate header')
                            seen.add('header')
                        elif line in LAYOUTS or (source_version == SOURCE_VERSIONS[1] and line == HEALTH_LAYOUT):
                            if line in seen:
                                raise ExportError('duplicate layout marker')
                            seen.add(line)
                        elif line.startswith('#policy,'):
                            if 'policy' in seen:
                                raise ExportError('duplicate policy metadata')
                            validate_policy(line)
                            seen.add('policy')
                        elif line.startswith('#'):
                            raise ExportError(f'unsupported metadata line {line_number}')
                        else:
                            required_layouts = LAYOUTS | ({HEALTH_LAYOUT} if source_version == SOURCE_VERSIONS[1] else set())
                            if session_id is None or not required_layouts.issubset(seen) or \
                                    'header' not in seen or 'policy' not in seen:
                                raise ExportError('missing bookmark source metadata')
                            clips = load_alignment(alignment, session_id)
                            started = True
                    if not started:
                        continue
                    if line.startswith('#'):
                        raise ExportError(f'metadata after records at line {line_number}')
                    if '"' in line or any(ord(char) < 32 for char in line):
                        raise ExportError(f'invalid row encoding at line {line_number}')
                    cells = next(csv.reader([line], strict=True))
                    kind = cells[0] if cells else ''
                    if kind not in ROW_WIDTHS or len(cells) != ROW_WIDTHS[kind]:
                        raise ExportError(f'invalid {kind or "empty"} row at line {line_number}')
                    input_rows += 1
                    row_session = integer(cells[1], 'session_id', 1, UINT64)
                    if row_session != session_id:
                        raise ExportError(f'wrong session at line {line_number}')
                    try:
                        validate_row(cells, source_version, session_id, INPUT_HEADER.split(',')[:-3])
                    except ValueError as error:
                        raise ExportError(f'line {line_number}: {error}') from error
                    if kind != 'bookmark':
                        if kind == 'marker':
                            continue
                        if any(cells[-3:]):
                            raise ExportError(f'nonbookmark payload at line {line_number}')
                        continue
                    row = parse_bookmark(cells, session_id)
                    key = int(row['bookmark_id'])
                    if key <= last_id:
                        raise ExportError(f'duplicate or out-of-order bookmark ID at line {line_number}')
                    last_id = key
                    for offset, name in enumerate(counter_max, 30):
                        counter_max[name] = max(counter_max[name], int(cells[offset]))
                    sidecar.write(','.join(row[name] for name in HEADER.strip().split(',')) + '\n')
                    accepted += 1
                    for clip in clips:
                        clip_time = (Decimal(row['monotonic_ms']) - clip['clip_zero_session_ms']) / 1000
                        if not 0 <= clip_time < clip['duration_seconds']:
                            report['editor']['outside_clip'] += 1
                            continue
                        position = clip['project_start_seconds'] + clip_time
                        position_text = format(position, 'f')
                        name = f"RideSync {session_id}:{key} [clip {clip['index']}]"
                        report['editor']['placed'] += 1
                        if len(report['editor']['preview']) < 10:
                            report['editor']['preview'].append({
                                'bookmark_id': key, 'clip_index': clip['index'],
                                'clip_seconds': format(clip_time, 'f'),
                                'project_seconds': position_text, 'name': name})
                        script_context.write(f'-- {name}: clip {format(clip_time, "f")} s, project {position_text} s\n')
                        script_context.write(f'if not exists("{name}", {position_text}) then\n')
                        script_context.write(f'  assert(reaper.AddProjectMarker2(0, false, {position_text}, 0, "{name}", -1, 0) >= 0)\nend\n')
                if not started:
                    required_layouts = LAYOUTS | ({HEALTH_LAYOUT} if source_version == SOURCE_VERSIONS[1] else set())
                    if session_id is None or not required_layouts.issubset(seen) or 'header' not in seen or 'policy' not in seen:
                        raise ExportError('missing bookmark source metadata')
                    clips = load_alignment(alignment, session_id)
                if script_context:
                    lua_end(script_context)
            finally:
                if script_context:
                    script_context.close()
        report['session_id'] = session_id
        report['input_rows'] = input_rows
        report['accepted_markers'] = accepted
        report['excluded_tail_rows'] = excluded_tail
        report['loss_evidence_available'] = accepted > 0
        report['editor']['clips'] = [{key: (format(value, 'f') if isinstance(value, Decimal) else value)
                                      for key, value in clip.items()} for clip in clips]
        (temp / OUTPUT_NAMES[1]).write_text(json.dumps(report, indent=2, sort_keys=True) + '\n', encoding='ascii')
        published = []
        try:
            for destination in outputs:
                os.link(temp / destination.name, destination)
                published.append(destination)
        except OSError:
            for destination in published:
                destination.unlink()
            raise
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='BookmarkV6 RideSync CSV')
    parser.add_argument('output_directory', type=Path, help='existing directory for exclusive outputs')
    parser.add_argument('--alignment', type=Path, help='explicit ridesync-alignment-v1 JSON')
    args = parser.parse_args(argv)
    try:
        export(args.source, args.output_directory, args.alignment)
    except (ExportError, OSError, csv.Error) as error:
        parser.exit(2, f'bookmark export: {error}\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
