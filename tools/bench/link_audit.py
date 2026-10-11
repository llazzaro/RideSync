"""Shared direct-call extraction for pinned compile-artifact audits."""
import re


def callers_from_disassembly(disassembly):
    callers = {}
    owner = None
    for line in disassembly.splitlines():
        label = re.match(r'[0-9a-f]+ <(.+)>:', line)
        if label:
            owner = label.group(1)
        call = re.search(r'\bcall\w*\s+[0-9a-f]+ <([^>]+)>', line)
        if call:
            callers.setdefault(call.group(1), set()).add(owner)
    return callers
