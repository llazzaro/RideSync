"""Exercise offline provisioning against actual host files, then real allocator IO."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("commission", ROOT / "scripts/commission_session_ledger.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class CommissionTest(unittest.TestCase):
    def test_offline_slots_are_consumed_by_real_cpp_allocator_without_reset(self):
        # A wrong encoding/CRC, implicit overwrite or missing persistent readback
        # fails the real allocator or changes the preexisting commissioning files.
        source = r'''
#include "session_identity.h"
#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <string>
struct IO : ridesync::IdentityLedgerIO {
  std::string root; int fd = -1, err = 0;
  explicit IO(const char *p) : root(p) {}
  bool open(unsigned slot, bool write, bool create) override {
    const auto path = root + (slot ? "/.session-id-b" : "/.session-id-a");
    fd = ::open(path.c_str(), write ? O_WRONLY | (create ? O_CREAT|O_EXCL : 0) : O_RDONLY,0600);
    if (fd < 0) err = errno; return fd >= 0;
  }
  int read(uint8_t *b, size_t n) override { return int(::read(fd,b,n)); }
  int write(const uint8_t *b, size_t n) override { return int(::write(fd,b,n)); }
  bool sync() override { return ::fsync(fd) == 0; }
  bool close() override { int f = fd; fd=-1; return ::close(f) == 0; }
  int error() const override { return err; }
};
int main(int argc,char **argv) {
  assert(argc == 2); IO io(argv[1]);
  ridesync::SessionIdentityAllocator a(io,7);
  assert(a.reserve().id == 0x700000001ULL);
  ridesync::SessionIdentityAllocator b(io,7);
  assert(b.reserve().id == 0x700000002ULL);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            module.commission(directory, 7)
            original = [(directory / name).read_bytes() for name in (".session-id-a", ".session-id-b")]
            with self.assertRaises(FileExistsError):
                module.commission(directory, 7)
            self.assertEqual(original, [(directory / name).read_bytes() for name in (".session-id-a", ".session-id-b")])
            cpp = directory / "main.cpp"
            cpp.write_text(source)
            binary = directory / "test"
            subprocess.run(["c++", "-std=c++11", "-I", str(ROOT / "include"), str(cpp),
                            str(ROOT / "src/session_identity.cpp"), "-o", str(binary)], check=True)
            subprocess.run([str(binary), folder], check=True, timeout=10)

    def test_invalid_namespace_never_creates_any_file(self):
        with tempfile.TemporaryDirectory() as folder:
            for namespace in (0, -1, 0x100000000):
                with self.assertRaises(ValueError):
                    module.commission(Path(folder), namespace)
            self.assertEqual([], list(Path(folder).iterdir()))
