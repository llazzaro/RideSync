"""Stream diagnostic records while retaining only fixed-size summary data."""
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
from typing import BinaryIO, TextIO

from .contracts import Diagnostic, EXPORTER_VERSION, ExportSummary


class HashingReader:
    """The reader's bounded readline interface with input-byte provenance."""

    def __init__(self, source: BinaryIO):
        self.source = source
        self.bytes_read = 0
        self._hash = hashlib.sha256()

    def readline(self, size: int = -1) -> bytes:
        if size < 0:
            raise ValueError('export input reads must have an explicit size bound')
        data = self.source.readline(size)
        self.bytes_read += len(data)
        self._hash.update(data)
        return data

    @property
    def sha256(self) -> str:
        return self._hash.hexdigest()


class ReportWriter:
    def __init__(self, output: TextIO):
        self.output = output
        self._first = True
        self.output.write('{"diagnostics":[')

    def diagnostic(self, diagnostic: Diagnostic) -> None:
        if not self._first:
            self.output.write(',')
        json.dump(asdict(diagnostic), self.output, ensure_ascii=True)
        self._first = False

    def finish(self, summary: ExportSummary, source: Path, observed: HashingReader,
               options: dict) -> None:
        self.output.write('],')
        values = {
            'report_version': 1,
            'exporter_version': EXPORTER_VERSION,
            'input': {'basename': source.name, 'bytes': observed.bytes_read,
                      'sha256': observed.sha256},
            'options': options,
            'summary': asdict(summary),
            'loss_limit': 'Observed exclusions and counters cannot count silent '
                          'power-cut loss or prove physical durability. Global '
                          'and per-kind counters overlap and must not be summed.',
            'time_limit': 'Receipt UTC estimates have unmeasured latency/drift; '
                          'exported times do not establish footage synchronization.',
        }
        for index, (key, value) in enumerate(values.items()):
            if index:
                self.output.write(',')
            self.output.write(json.dumps(key) + ':')
            json.dump(value, self.output, ensure_ascii=True)
        self.output.write('}\n')
