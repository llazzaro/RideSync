"""Concrete adapter with pinned Bosch declarations and synthetic bus/API faults.

API stand-ins are needed to inject readback/IO faults without sensor hardware.
Target compilation separately compiles the unmodified actual Bosch implementation.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PIN = 'https://github.com/sparkfun/SparkFun_BMI270_Arduino_Library.git#21ea234de321da07c552f7a43cb36f7df4f73a27'


class Bmi270TransportTest(unittest.TestCase):
    def test_strict_callbacks_profile_faults_and_qualified_lifecycle(self):
        vendor = ROOT / '.pio/libdeps/lilygo_t_a7670e_r2/SparkFun BMI270 Arduino Library/src'
        if not (vendor / 'bmi270_api/bmi270.h').exists():
            subprocess.run(['pio', 'pkg', 'install', '-d', str(ROOT), '-e',
                            'lilygo_t_a7670e_r2', '--library', PIN, '--no-save',
                            '--skip-dependencies'], check=True)
        files = {
            'Arduino.h': '#pragma once\n#include <cstdint>\ninline uint32_t millis(){return 11;}\ninline void delayMicroseconds(uint32_t){}\n',
            'freertos/FreeRTOS.h': '#pragma once\n#define pdPASS 1\n#define pdMS_TO_TICKS(n) (n)\n',
            'freertos/task.h': r'''
#pragma once
extern void (*task_fn)(void *); extern void *task_arg;
void testDelay(); void testDelete();
inline int xTaskCreatePinnedToCore(void (*fn)(void *),const char *,unsigned,void *arg,unsigned,void *,int){task_fn=fn;task_arg=arg;return pdPASS;}
inline void vTaskDelete(void *){testDelete();}
inline void vTaskDelay(unsigned){testDelay();}
''',
            'Wire.h': r'''
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
struct TwoWire {
  unsigned calls=0; bool short_read=false, short_write=false, nack=false, short_register=false; unsigned ends=0;
  uint8_t power=6; uint8_t reg=0; uint32_t available_bytes=0, offset=0; uint8_t payload[112]{};
  void beginTransmission(uint8_t){++calls;offset=0;}
  size_t write(uint8_t r){reg=r;return short_register?0:1;}
  size_t write(const uint8_t *,size_t n){return short_write?n-1:n;}
  uint8_t endTransmission(bool){++ends;return nack?2:0;}
  uint32_t requestFrom(uint8_t,size_t n,bool){++calls;offset=0;available_bytes=short_read?n-1:n;return available_bytes;}
  int available(){return available_bytes-offset;}
  int read(){if(reg==0x7d){++offset;return power;}return payload[offset++];}
};
''',
            'main.cpp': r'''
#include "bmi270_imu.h"
#include <cassert>
#include <cstring>
#include <type_traits>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <memory>
#include <chrono>
using namespace ridesync;
static_assert(!std::is_copy_constructible<Bmi270Imu>::value && !std::is_copy_assignable<Bmi270Imu>::value && !std::is_move_constructible<Bmi270Imu>::value && !std::is_move_assignable<Bmi270Imu>::value,"adapter ownership must be fixed");
static_assert(!std::is_copy_constructible<Bmi270Worker>::value && !std::is_copy_assignable<Bmi270Worker>::value && !std::is_move_constructible<Bmi270Worker>::value && !std::is_move_assignable<Bmi270Worker>::value,"worker ownership must be fixed");
void (*task_fn)(void *)=nullptr;void *task_arg=nullptr;
static std::mutex lifecycle_mutex;static std::condition_variable lifecycle_cv;
static bool service_entered=false,release_service=false,exit_entered=false,release_exit=false;
static unsigned delays=0;static Bmi270Worker *active_worker=nullptr;
void testDelay(){
  std::unique_lock<std::mutex> lock(lifecycle_mutex);
  assert(!active_worker->workerFinished()); // even manager finish cannot retire task objects
  ++delays;service_entered=true;lifecycle_cv.notify_all();
  assert(lifecycle_cv.wait_for(lock,std::chrono::seconds(5),[]{return release_service;}));
}
void testDelete(){
  // This RTOS boundary must never access the caller-owned worker/progress.
  std::unique_lock<std::mutex> lock(lifecycle_mutex);
  exit_entered=true;lifecycle_cv.notify_all();
  assert(lifecycle_cv.wait_for(lock,std::chrono::seconds(5),[]{return release_exit;}));
}
struct LifecyclePort : ImuPort {
  bool begin(ImuConfig &c) override {c.generation=1;return true;}
  bool read(uint8_t *,uint16_t,uint16_t &,uint8_t &) override {assert(false);return false;}
  bool flush() override {assert(false);return false;}
};
void worker_lifetime_boundary(){
  std::unique_ptr<LifecyclePort> port(new LifecyclePort);
  std::unique_ptr<ImuInbox> inbox(new ImuInbox);
  std::unique_ptr<HealthProgress> progress(new HealthProgress);
  std::unique_ptr<ImuManager> manager(new ImuManager(*port,*inbox,*progress,42,{}));
  std::unique_ptr<Bmi270Worker> worker(new Bmi270Worker(*manager,*progress));active_worker=worker.get();
  assert(!worker->start(false,false) && !worker->start(true,true) && !task_fn);
  assert(worker->start(true,false) && !worker->start(true,false));
  auto fn=task_fn;auto arg=task_arg;std::thread task([fn,arg]{fn(arg);});
  {
    std::unique_lock<std::mutex> lock(lifecycle_mutex);
    assert(lifecycle_cv.wait_for(lock,std::chrono::seconds(5),[]{return service_entered;}));
    assert(!worker->workerFinished() && progress->generation()==1);
    manager->stop();release_service=true;lifecycle_cv.notify_all();
    assert(lifecycle_cv.wait_for(lock,std::chrono::seconds(5),[]{return exit_entered;}));
    assert(worker->workerFinished() && progress->isFinished());
    // Destroy ALL caller-owned state while the real run function is at final RTOS exit.
    worker.reset();manager.reset();progress.reset();inbox.reset();port.reset();
    release_exit=true;lifecycle_cv.notify_all();
  }
  task.join();assert(delays==1);
}
static bmi2_sens_config profile[2]; static uint16_t fifo_config;
static uint8_t filters[3]{},downsampling[3]={2,2,2}; static bool mismatch=false;
static uint16_t fifo_length=13;
extern "C" {
int8_t bmi270_init(bmi2_dev *d){uint8_t p[2]={9,9}; int8_t r=d->read(0,p,2,d->intf_ptr);if(r)return r;return d->write(1,p,2,d->intf_ptr);}
int8_t bmi270_get_sensor_config(bmi2_sens_config *c,uint8_t,bmi2_dev *) {std::memcpy(c,profile,sizeof(profile));if(mismatch)c[0].cfg.acc.odr=1;return 0;}
int8_t bmi270_set_sensor_config(bmi2_sens_config *c,uint8_t,bmi2_dev *) {std::memcpy(profile,c,sizeof(profile));return 0;}
int8_t bmi270_sensor_enable(const uint8_t *,uint8_t,bmi2_dev *) {return 0;}
int8_t bmi2_set_accel_offset_comp(uint8_t,bmi2_dev *) {return 0;}
int8_t bmi2_set_gyro_offset_comp(uint8_t,bmi2_dev *) {return 0;}
int8_t bmi2_get_accel_offset_comp(uint8_t *v,bmi2_dev *) {*v=0;return 0;}
int8_t bmi2_get_gyro_offset_comp(uint8_t *v,bmi2_dev *) {*v=0;return 0;}
int8_t bmi2_set_fifo_config(uint16_t c,uint8_t enable,bmi2_dev *) {fifo_config=enable?c:0;return 0;}
int8_t bmi2_get_fifo_config(uint16_t *c,bmi2_dev *) {*c=fifo_config;return 0;}
int8_t bmi2_get_fifo_length(uint16_t *n,bmi2_dev *) {*n=fifo_length;return 0;}
int8_t bmi2_get_saturation_status(uint8_t *s,bmi2_dev *) {*s=1;return 0;}
int8_t bmi2_set_command_register(uint8_t,bmi2_dev *) {return 0;}
int8_t bmi2_set_adv_power_save(uint8_t,bmi2_dev *) {return 0;}
int8_t bmi2_get_adv_power_save(uint8_t *v,bmi2_dev *) {*v=0;return 0;}
int8_t bmi2_set_fifo_filter_data(uint8_t sensor,uint8_t value,bmi2_dev *) {filters[sensor]=value;return 0;}
int8_t bmi2_get_fifo_filter_data(uint8_t sensor,uint8_t *v,bmi2_dev *) {*v=filters[sensor];return 0;}
int8_t bmi2_set_fifo_down_sample(uint8_t sensor,uint8_t value,bmi2_dev *) {downsampling[sensor]=value;return 0;}
int8_t bmi2_get_fifo_down_sample(uint8_t sensor,uint8_t *v,bmi2_dev *) {*v=downsampling[sensor];return 0;}
}
int main(){
  TwoWire wire; Bmi270Qualification q; ImuConfig c;c.mount_id=99;
  Bmi270Imu disabled(wire,q);assert(!disabled.begin(c) && wire.calls==0 && c.generation==0);
  q.enabled=q.dedicated_bus=q.electrically_qualified=true;q.address=0x68;q.sensor_id=7;
  wire.short_register=true;Bmi270Imu short_register(wire,q);assert(!short_register.begin(c) && wire.ends==1);wire.short_register=false;
  wire.short_read=true;Bmi270Imu short_sensor(wire,q);assert(!short_sensor.begin(c) && c.generation==0);
  wire.short_read=false;wire.short_write=true;Bmi270Imu short_write(wire,q);assert(!short_write.begin(c));
  wire.short_write=false;wire.nack=true;Bmi270Imu absent(wire,q);assert(!absent.begin(c));wire.nack=false;
  mismatch=true;Bmi270Imu wrong_profile(wire,q);assert(!wrong_profile.begin(c) && c.generation==0);mismatch=false;
  wire.power=0;Bmi270Imu disabled_channels(wire,q);assert(!disabled_channels.begin(c) && c.generation==0);wire.power=6;
  Bmi270Imu sensor(wire,q);assert(sensor.begin(c));assert(c.generation==1 && c.mount_id==99 && c.sensor_id==7);
  assert(c.accel_scale_numerator==1 && c.accel_scale_denominator==2048 && c.gyro_scale_numerator==125 && c.gyro_scale_denominator==2048);
  assert(c.accel_filter==0x502 && c.gyro_filter==0x702); // self-contained effective FIFO filter settings
  uint8_t bytes[112];std::memset(bytes,9,sizeof(bytes));uint16_t n=0;uint8_t flags=0;
  wire.short_read=true;assert(!sensor.read(bytes,112,n,flags));assert(n==0 && bytes[0]==9);
  wire.short_read=false;wire.payload[0]=0x8c;assert(sensor.read(bytes,112,n,flags));assert(n==17 && flags==1 && bytes[0]==0x8c);
  unsigned calls=wire.calls;fifo_length=109;assert(!sensor.read(bytes,112,n,flags) && n==0 && calls==wire.calls);
  assert(sensor.capacityOverflows()==1);ImuEvidence overflow;sensor.describeReadFailure(overflow);assert(overflow.event_code==1 && overflow.event_length==2 && overflow.event_bytes[0]==109);
  fifo_length=13;assert(!sensor.read(bytes,3,n,flags));
  worker_lifetime_boundary();
}
''',
        }
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            for name, body in files.items():
                output = path / name
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_text(body)
            binary = path / 'test'
            subprocess.run([shutil.which('c++'), '-std=c++11', '-DARDUINO', '-pthread',
                            '-fsanitize=address', '-fno-omit-frame-pointer',
                            '-I', str(path), '-I', str(ROOT / 'include'), '-I', str(vendor),
                            str(path / 'main.cpp'), str(ROOT / 'src/bmi270_imu.cpp'),
                            str(ROOT / 'src/imu_manager.cpp'), str(ROOT / 'src/telemetry_admission.cpp'),
                            str(ROOT / 'src/storage.cpp'), str(ROOT/'src/motion_estimator.cpp'), str(ROOT / 'src/session_clock.cpp'),
                            str(ROOT / 'src/health_supervisor.cpp'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
