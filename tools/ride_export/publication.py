"""Temporary output staging and exclusive publication without overwrites."""
from contextlib import contextmanager
import os
from pathlib import Path
import tempfile
from typing import Iterator, TextIO


def validate_paths(source: Path, output: Path, report: Path) -> None:
    """Reject path aliases and existing targets before any staging is created."""
    paths = (source, output, report)
    try:
        resolved = tuple(path.resolve() for path in paths)
    except RuntimeError as error:
        # Python 3.11 raises RuntimeError on symlink loops; later versions use
        # OSError or defer it. Keep the public error contract on every runtime.
        raise ValueError('path contains a symlink loop; choose accessible distinct paths') from error
    if len(set(resolved)) != len(paths):
        raise ValueError('input, GPX output and report must be distinct paths')
    for index, left in enumerate(paths):
        for right in paths[index + 1:]:
            if left.exists() and right.exists() and left.samefile(right):
                raise ValueError('input, GPX output and report cannot alias the same file')
    for target in (output, report):
        if os.path.lexists(target):
            raise ValueError(f'output exists; choose a new path: {target}')


@contextmanager
def staged_text(target: Path) -> Iterator[tuple[Path, TextIO]]:
    """Stage on the destination volume; cleanup only the private temporary name."""
    with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', newline='\n',
                                     prefix='.ridesync-export-', suffix='.tmp',
                                     dir=target.parent, delete=False) as stream:
        stage = Path(stream.name)
        try:
            yield stage, stream
            stream.flush()
        finally:
            stage.unlink(missing_ok=True)


def publish_exclusive(stage: Path, target: Path) -> None:
    """Link only if the destination is absent, including at publication time.

    A same-volume hard link publishes the complete staged bytes without a
    replacing rename. Filesystems that cannot do this produce an I/O error;
    there is no fallback that risks overwriting an existing file.
    """
    os.link(stage, target)
