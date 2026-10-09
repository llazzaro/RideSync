"""Exercise the GPS audit against synthetic emitted-tool boundaries."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    'gps_audit', Path(__file__).resolve().parents[1]/'scripts/probe_gps_encoder_resources.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class ResourceAuditTests(unittest.TestCase):
    def test_literal_loaded_call_resolves_to_actual_target(self):
        dump = '''400d0000 <encoder>:
400d0000: 000081 l32r a8, 400c0000 <literal>
400d0003: 0008e0 callx8 a8
400d0010 <memcpy>:
400d0010: f01d retw.n
'''
        graph, instructions = audit.call_graph(
            dump, {0x400d0010: 'memcpy'}, lambda address: 0x400d0010)
        self.assertEqual(graph['encoder'], {'memcpy'})
        self.assertEqual(audit.audit_calls(graph, instructions, 'encoder'), {'encoder', 'memcpy'})

    def test_unresolved_register_call_fails_closed(self):
        graph, instructions = audit.call_graph(
            '400d0000 <encoder>:\n400d0000: 0008e0 callx8 a8\n', {}, lambda address: 0)
        with self.assertRaisesRegex(AssertionError, 'unresolved'):
            audit.audit_calls(graph, instructions, 'encoder')

    def test_clobbered_literal_register_does_not_certify_call(self):
        dump = '''400d0000 <encoder>:
400d0000: 000081 l32r a8, 400c0000 <literal>
400d0003: 888c movi.n a8, 0
400d0005: 0008e0 callx8 a8
'''
        graph, instructions = audit.call_graph(dump, {9: 'memcpy'}, lambda address: 9)
        with self.assertRaisesRegex(AssertionError, 'unresolved'):
            audit.audit_calls(graph, instructions, 'encoder')

    def test_compiler_integrity_trap_is_reported_as_exceptional_leaf(self):
        graph = {'encoder': {'__stack_chk_fail'}, '__stack_chk_fail': {'abort'}}
        instructions = {'encoder': [], '__stack_chk_fail': [], 'abort': []}
        self.assertEqual(audit.audit_calls(graph, instructions, 'encoder'),
                         {'encoder', '__stack_chk_fail'})

    def test_only_verified_rom_arithmetic_may_lack_disassembly(self):
        graph, instructions = {'encoder': {'__lshrdi3'}}, {'encoder': []}
        with self.assertRaisesRegex(AssertionError, 'missing disassembly'):
            audit.audit_calls(graph, instructions, 'encoder')
        self.assertEqual(audit.audit_calls(graph, instructions, 'encoder', {'__lshrdi3'}),
                         {'encoder', '__lshrdi3'})

    def test_indirect_tail_jump_cannot_evade_dependency_audit(self):
        dump = '400d0000 <encoder>:\n400d0000: 0008a0 jx a8\n'
        graph, instructions = audit.call_graph(dump, {}, lambda address: 0)
        with self.assertRaisesRegex(AssertionError, 'unresolved'):
            audit.audit_calls(graph, instructions, 'encoder')

    def test_allocator_or_driver_dependency_is_rejected(self):
        for name in ('malloc', 'digitalWrite', 'ble_gatts_notify_custom'):
            with self.subTest(name=name):
                with self.assertRaisesRegex(AssertionError, 'unexpected encoder dependency'):
                    audit.audit_calls({'encoder': {name}}, {'encoder': [], name: []}, 'encoder')


if __name__ == '__main__':
    unittest.main()
