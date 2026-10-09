if(scenario==16 || scenario==17){
 WakeManager manager(radio);
 WakePeerConfig config;config.enabled=config.source_qualified=true;config.profile=insta360::WakeProfile::M5WakeV1;config.identifier={{'1','2','3','4','5','6'}};
 WakePolicy policy;policy.total_ms=100;policy.slice_ms=30;
 for(unsigned i=0;i<4;++i){WakeOperation request=op;request.peer=i;assert(manager.request(request,config,policy,0)==CameraError::None);}
 manager.service(0);
 std::mutex lock;std::condition_variable condition;bool entered=false,resume=false;
 auto stall=[&](){std::unique_lock<std::mutex> hold(lock);entered=true;condition.notify_one();condition.wait(hold,[&](){return resume;});};
 if(scenario==16)data_hook=stall;else start_hook=stall;
 std::thread workerThread([](){runWorker();});
 {std::unique_lock<std::mutex> hold(lock);condition.wait(hold,[&](){return entered;});}
 manager.service(100);
 for(unsigned i=0;i<4;++i)assert(manager.status(i).phase==WakePhase::Timeout);
 assert(!manager.status(0).released);assert(host.wakeReserved());
 {std::lock_guard<std::mutex> hold(lock);resume=true;}condition.notify_one();workerThread.join();
 if(scenario==16)assert(starts==0);else assert(starts==1&&stops==1);
 pump();manager.service(101);assert(manager.status(0).released);return 0;
}
if(scenario==18){
 BleContext link;link.peer=0;link.generation=42;link.phase=BlePhase::Connect;link.terminal=false;
 BleCommand command;command.phase=BlePhase::Connect;command.duration_ms=100;command.identity.verified=true;command.identity.type=IdentityType::Public;command.identity.address[0]=1;
 assert(host.submit(command,link)==0);
 ble_gap_event event{};event.type=BLE_GAP_EVENT_CONNECT;event.connect.conn_handle=10;gap_callbacks[0](&event,gap_args[0]);link.connection.store(10);
 assert(radio.begin(op,bytes,3000,0)==WakeSubmit::Accepted);runWorker();
 BleContext read;read.peer=0;read.generation=42;read.phase=BlePhase::Read;read.connection.store(10);read.terminal=false;
 command.phase=BlePhase::Read;command.connection=10;command.handle=3;assert(host.submit(command,read)==0);
 BondIdentity target;target.verified=true;target.type=IdentityType::Public;target.address[0]=1;
 assert(host.requestBondReset(target,1,100,0)==BondResetSubmission::Busy);
 incoming(10);runWorker();pump();assert(terminated.empty());assert(!radio.poll(op,0).released);return 0;
}
assert(radio.begin(op,bytes,3000,0)==WakeSubmit::Accepted);
if(scenario==1)radio.cancel(op);
if(scenario==2)fake_time=3000;
if(scenario==3)data_hook=[&](){radio.cancel(op);};
if(scenario==4)rsp_hook=[&](){fake_time=3000;};
if(scenario==5)start_hook=[&](){radio.cancel(op);};
if(scenario==6)start_error=77;
runWorker();
if(scenario>=1&&scenario<=4){assert(starts==0);pump();assert(radio.poll(op,3000).released);return 0;}
assert(captured_adv.size()==31&&captured_rsp.size()==25);
assert(captured_adv==std::vector<uint8_t>(bytes.advertisement.begin(),bytes.advertisement.end()));
assert(captured_duration>0&&captured_duration<=3000);
if(scenario==6){pump();assert(radio.poll(op,0).released);assert(!radio.poll(op,0).submitted);return 0;}
if(scenario>=9&&scenario<=13){
 if(scenario==12)terminate_error=90;
 if(scenario==13)find_error=90;
 incoming(19);
 if(scenario==10){
  for(int type: {1,2,3,6,7,8}){
   ble_store_value value{};ble_store_key key{};
   if(type==6){value.rpa_rec.peer_rpa_addr.type=1;value.rpa_rec.peer_rpa_addr.val[0]=8;key.rpa_rec.peer_rpa_addr=value.rpa_rec.peer_rpa_addr;}
   else{value.sec.peer_addr.val[0]=9;key.sec.peer_addr.val[0]=9;}
   assert(ble_hs_cfg.store_write_cb(type,&value)==BLE_HS_ESTORE_FAIL);
   assert(ble_hs_cfg.store_read_cb(type,&key,&value)==BLE_HS_ESTORE_FAIL);
   assert(ble_hs_cfg.store_delete_cb(type,&key)==BLE_HS_ESTORE_FAIL);
   key.sec.peer_addr.val[0]=1;value.sec.peer_addr.val[0]=1;
   if(type==6){key.rpa_rec.peer_rpa_addr.val[0]=1;key.rpa_rec.peer_rpa_addr.val[5]=0xc0;value.rpa_rec.peer_rpa_addr.val[0]=1;value.rpa_rec.peer_rpa_addr.val[5]=0xc0;value.rpa_rec.peer_addr.val[0]=1;}
   assert(ble_hs_cfg.store_write_cb(type,&value)==0);
  }
  ble_store_value alias{};alias.rpa_rec.peer_addr.val[0]=9;alias.rpa_rec.peer_rpa_addr.val[0]=2;alias.rpa_rec.peer_rpa_addr.type=1;
  assert(!host.wakeStoreAllowed(6,nullptr,&alias));
  ble_store_key unresolved{};unresolved.sec.peer_addr.type=1;unresolved.sec.peer_addr.val[0]=4;
  assert(!host.wakeStoreAllowed(2,&unresolved,nullptr));
  ble_store_key wildcard{};assert(!host.wakeStoreAllowed(2,&wildcard,nullptr));
  ble_hs_dev_records rec{};rec.peer_sec.peer_addr.val[0]=9;
  assert(!host.wakeStoreAllowed(4,nullptr,&rec));rec.peer_sec.peer_addr.val[0]=1;assert(host.wakeStoreAllowed(4,nullptr,&rec));
  ble_gap_event repeat{};repeat.type=BLE_GAP_EVENT_REPEAT_PAIRING;assert(adv_callback(&repeat,adv_arg)==BLE_GAP_REPEAT_PAIRING_IGNORE);
  ble_store_status_event full;full.event_code=BLE_STORE_EVENT_FULL;full.full.conn_handle=19;assert(host.onStoreStatus(&full,nullptr)!=0);assert(host.state()==BleHostState::Ready);
 }
 runWorker();assert(terminated.size()==1&&terminated[0]==19);pump();assert(!radio.poll(op,0).released);
 if(scenario==11){disconnect(18);pump();assert(!radio.poll(op,0).released);}
 if(scenario==12 || scenario==13){disconnect(19);pump();assert(!radio.poll(op,0).released);return 0;}
 disconnect(19);assert(!radio.poll(op,0).released);pump();runWorker();assert(radio.poll(op,0).released);return 0;
}
if(scenario==14){fake_time=3000;runWorker();pump();assert(stops==1&&radio.poll(op,3000).released);return 0;}
if(scenario==15){complete();runWorker();assert(!radio.poll(op,0).released);pump();assert(radio.poll(op,0).released);assert(stops==0);return 0;}
if(scenario==7)stop_error=78;
if(scenario==8)queue_full=true;
radio.cancel(op);runWorker();
if(scenario==7){pump();assert(!radio.poll(op,0).released);runWorker();assert(stops==1);return 0;}
if(scenario==8){pump();assert(!radio.poll(op,0).released);assert(queue_wait==0);return 0;}
assert(!radio.poll(op,0).released);complete();complete();pump();runWorker();assert(radio.poll(op,0).released);
assert(stops==1);assert(radio.poll(op,0).released);
