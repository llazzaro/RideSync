"""Deterministic final transport publication after admission's empty observation.

Only a temporary source copy has a scheduling barrier; no production hooks.
Synthetic host evidence, repository MIT, no physical timing claim.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]


class TelemetryStopTest(unittest.TestCase):
    def test_finish_acquire_rechecks_final_publication(self):
        source = (ROOT/'src/telemetry_admission.cpp').read_text()
        boundary = 'stopping_ && inbox_.finished_.load(std::memory_order_acquire)'
        self.assertEqual(1,source.count(boundary))
        source = 'void beforeFinish();\n' + source.replace(boundary,
                    'stopping_ && (beforeFinish(), inbox_.finished_.load(std::memory_order_acquire))')
        main = r'''
#include "telemetry_admission.h"
#include <cassert>
#include <thread>
#include <mutex>
#include <condition_variable>
std::mutex mutex; std::condition_variable cv;
bool observed=false, released=false;
void beforeFinish() {
 std::unique_lock<std::mutex> lock(mutex); observed=true; cv.notify_one();
 cv.wait(lock,[]{return released;});
}
struct C : ridesync::Clock { uint32_t now() const override {return 0;} };
struct S : ridesync::StorageSink {
 bool mount() override {return true;} bool openExclusive(const char *) override {return true;}
 size_t write(const char *,size_t n) override {return n;} bool flush() override {return true;}
 void close() override {}
};
int main() {
 using namespace ridesync;
 S sink; Storage storage(sink,{42,"fw","synthetic",2,4,StorageFormat::MixedV2});
 C raw; SessionClock clock(raw,42,1000); ImuInbox inbox; TelemetryAdmission admission(clock,storage,inbox);
 // No concurrent clock context: admission's entire execution is this thread.
 std::thread owner([&]{admission.requestStop(); assert(admission.tick()==0);});
 {std::unique_lock<std::mutex> lock(mutex);cv.wait(lock,[]{return observed;});}
 ImuBatch b; b.count=1; b.records[0].session_id=42;
 assert(inbox.publish(b)); inbox.finish();
 {std::lock_guard<std::mutex> lock(mutex);released=true;} cv.notify_one();owner.join();
 assert(!admission.stopped()); assert(admission.tick()==1); assert(admission.stopped());
 for(int i=0;i<100;++i) storage.workerStep();
 assert(storage.health().flushed==1 && storage.health().stopped);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)
            (path/'admission.cpp').write_text(source)
            (path/'main.cpp').write_text(main)
            subprocess.run(['c++','-std=c++11','-pthread','-I'+str(ROOT/'include'),str(path/'main.cpp'),
                            str(path/'admission.cpp'),str(ROOT/'src/storage.cpp'),
                            str(ROOT/'src/session_clock.cpp'),'-o',str(path/'race')],check=True)
            subprocess.run([str(path/'race')],check=True,timeout=10)
