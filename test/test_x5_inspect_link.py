import importlib.util
from pathlib import Path
import unittest


spec = importlib.util.spec_from_file_location(
    'x5_inspect', Path(__file__).resolve().parents[1] / 'tools/bench/x5_peripheral/inspect_link.py')
inspect_link = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspect_link)


class X5InspectLinkTests(unittest.TestCase):
    def test_callers_from_disassembly_tracks_current_symbol(self):
        disassembly = '''
00000000 <owner>:
00000000: call8 00000010 <target>
00000010 <target>:
00000010: ret
'''
        self.assertEqual(inspect_link.callers_from_disassembly(disassembly),
                         {'target': {'owner'}})


if __name__ == '__main__':
    unittest.main()
