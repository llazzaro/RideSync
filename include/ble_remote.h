#pragma once
#include "health_supervisor.h"
#include "pairing_reset.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ridesync {
constexpr size_t kBlePeers = 4, kBleServices = 3, kBleEndpoints = 8;
constexpr size_t kBleDiscoveredChars = 32, kBlePayload = 64, kBleQueue = 32;
constexpr uint16_t kBleNoHandle = 0xffff;
// Private transport backpressure: no SDK reference was acquired. Consumers defer
// the copied operation; this must never become a permanent peer transport fault.
constexpr int kBleHostReserved = -30001;
constexpr uint16_t kBleMtuReserved = 0xffff;
struct BleUuid {
  uint8_t size = 2; // 2 or 16, little endian as the pinned host represents UUIDs.
  std::array<uint8_t, 16> bytes{};
  static BleUuid shortUuid(uint16_t value);
  bool operator==(const BleUuid &other) const;
};
struct BleEndpointSpec {
  BleUuid uuid;
  uint8_t service = 0, properties = 0;
  // 0 = no subscription, 1 = notify, 2 = indicate. Verified by ATT write + read.
  uint8_t subscribe = 0;
};
struct BleProfileSpec {
  std::array<BleUuid, kBleServices> services{};
  std::array<BleEndpointSpec, kBleEndpoints> endpoints{};
  uint8_t service_count = 0, endpoint_count = 0;
  bool require_authenticated = false;
};
enum class BlePhase : uint8_t {
  Empty,
  Queued,
  Connect,
  Security,
  Services,
  Characteristics,
  Descriptors,
  Subscribe,
  VerifySubscription,
  ReadyForProfile,
  Read,
  Write,
  Retiring,
  Quarantined,
  Closed,
  Scan
};
enum class BleFault : uint8_t {
  None,
  NotQualified,
  Host,
  Store,
  Identity,
  Capacity,
  Admission,
  Timeout,
  Overflow,
  Malformed,
  MissingService,
  MissingProperty,
  MissingCccd,
  Att,
  Stopped
};
enum class BleEventKind : uint8_t {
  Advertisement,
  ScanComplete,
  Connected,
  Security,
  Disconnected,
  Service,
  Characteristic,
  Descriptor,
  Complete,
  Notification
};
// The pinned NimBLE legacy GAP report types. Unknown is the safe default for
// synthetic events and any future report format not explicitly mapped by host.
enum class BleAdvertisementType : uint8_t {
  ConnectableUndirected = 0,
  ConnectableDirected = 1,
  Scannable = 2,
  NonConnectable = 3,
  ScanResponse = 4,
  Unknown = 0xff
};
struct BleEvent {
  BleEventKind kind = BleEventKind::Complete;
  uint8_t peer = 0, properties = 0, size = 0;
  uint32_t generation = 0, procedure = 0;
  BlePhase phase = BlePhase::Empty;
  int status = 0; // Host error retained, never a camera ACK.
  uint16_t connection = kBleNoHandle, handle = 0, start = 0, end = 0;
  BleUuid uuid;
  BleAdvertisementType advertisement_type = BleAdvertisementType::Unknown;
  BondIdentity identity;
  bool encrypted = false, authenticated = false, bonded = false;
  std::array<uint8_t, kBlePayload> bytes{};
};
struct BleContext;
class BleCallbacks {
public:
  virtual ~BleCallbacks() = default;
  // Payload has already been copied. Called by host; never calls a camera manager.
  virtual void copied(BleContext &, BleEvent) = 0;
};
// Stable until host terminal callback AND its scheduled host-queue barrier.
// Contexts never move, including when a deadline expires or terminate returns 0.
struct BleContext {
  BleCallbacks *receiver = nullptr;
  uint8_t peer = 0;
  uint32_t generation = 0, procedure = 0;
  BlePhase phase = BlePhase::Empty;
  uint16_t expected_handle = 0;
  std::atomic<uint16_t> connection{kBleNoHandle};
  std::atomic<bool> sealed{false}, terminal{true};
  std::atomic<int> terminal_status{0};
  std::atomic<BleFault> fault{BleFault::None};
};
struct BleCommand {
  BlePhase phase = BlePhase::Empty;
  BondIdentity identity;
  BleUuid uuid;
  uint16_t connection = kBleNoHandle, start = 0, end = 0, handle = 0;
  uint32_t duration_ms = 0;
  uint8_t size = 0;
  std::array<uint8_t, kBlePayload> bytes{};
};
struct BleBondAdmission {
  bool stack_ready = false, restore_verified = false, refusal_installed = false;
  bool existing_verified_identity = false, identity_matches = false, persistence_allowed = false;
  unsigned used = 0, capacity = 0;
  bool reserved = false;
  bool allowed() const;
};
enum class BleHostState : uint8_t { Disabled, Starting, Ready, Failed };
class BleHost {
public:
  virtual ~BleHost() = default;
  // No application wait for sync. SDK calls themselves need latency qualification.
  virtual BleHostState start(bool enabled, bool source_qualified) = 0;
  virtual BleHostState state() const = 0;
  virtual BleFault fault() const = 0;
  virtual void sealStartup() = 0;
  virtual BleBondAdmission bondAdmission(const BondIdentity &) = 0;
  virtual int submit(const BleCommand &, BleContext &) = 0;
  virtual int retire(BleContext &) = 0;
  virtual int cancelScan(BleContext &) = 0;
  // True only after terminal callback and host-queue quiescence barrier.
  virtual bool quiescent(const BleContext &) const = 0;
  virtual bool releaseContext(BleContext &) = 0;
  virtual uint16_t mtu(uint16_t connection) const = 0;
};
enum class BleResultKind : uint8_t {
  TransportReady,
  ReadComplete,
  WriteComplete,
  Notification,
  Retired,
  LinkClosed,
  Advertisement,
  ScanComplete,
  ScanCancelFailed
};
struct BleResult {
  BleResultKind kind = BleResultKind::Retired;
  BleFault fault = BleFault::None;
  BleEvent event;
  uint8_t endpoint = 0;
};
class BleResultSink {
public:
  virtual ~BleResultSink() = default;
  // Owner context, drain fault/retirement results before profile events or tick.
  virtual void result(const BleResult &) = 0;
};
class BleCentral final : public BleCallbacks {
public:
  explicit BleCentral(BleHost &, BleResultSink &, HealthProgress * = nullptr);
  ~BleCentral();
  BleCentral(const BleCentral &) = delete;
  BleCentral &operator=(const BleCentral &) = delete;
  BleCentral(BleCentral &&) = delete;
  BleCentral &operator=(BleCentral &&) = delete;
  bool begin(bool enabled, bool source_qualified, uint32_t now);
  bool connect(uint8_t peer, uint32_t generation, const BondIdentity &, const BleProfileSpec &);
  bool scan(uint32_t duration_ms, uint32_t now);
  // Cancels only the shared discovery lease; existing peer links remain open.
  // The lease is reusable only after its terminal callback and host barrier.
  bool cancelScan();
  // True only after terminal delivery, quiescence and actual host context release.
  bool scanReleased() const { return !scanning_ && !scan_.receiver; }
  bool read(uint8_t peer, uint8_t endpoint, uint32_t now);
  bool write(uint8_t peer, uint8_t endpoint, const uint8_t *, size_t, uint32_t now);
  void disconnect(uint8_t peer);
  void stop(); // Admission sealed immediately; cleanup is asynchronous.
  void service(uint32_t now);
  void copied(BleContext &, BleEvent) override;
  BlePhase phase(uint8_t peer) const;
  int error(uint8_t peer) const;
  // Result of the single cancellation attempt; retained until the next scan.
  int scanCancelError() const { return scan_cancel_error_; }
  uint32_t scanGeneration() const { return scan_.generation; }
  bool admissionOpen(uint8_t peer) const;
  bool stopped() const { return stopping_; }
  bool canDestroy() const; // All callbacks retired/quiescent; caller must check.
  static constexpr uint32_t kPhaseDeadlineMs = 5000, kStartupDeadlineMs = 1000;

private:
  struct Characteristic {
    BleUuid uuid;
    uint16_t declaration = 0, value = 0;
    uint8_t properties = 0, service = 0;
  };
  struct Endpoint {
    uint16_t value = 0, cccd = 0, end = 0;
  };
  struct Peer {
    BleContext link, procedure;
    BondIdentity identity;
    BleProfileSpec spec;
    BlePhase phase = BlePhase::Empty;
    std::array<Characteristic, kBleDiscoveredChars> chars{};
    std::array<Endpoint, kBleEndpoints> endpoints{};
    std::array<uint16_t, kBleServices> service_start{}, service_end{};
    uint8_t service = 0, endpoint = 0, char_count = 0;
    uint32_t deadline = 0, next_procedure = 0;
    bool active = false, complete = false, retired_reported = false, terminate_submitted = false;
    bool cancel_submitted = false, deferred = false, security_waiting = false,
         connect_waiting = false;
    BleCommand deferred_command;
    int error = 0;
    BleFault retirement = BleFault::None;
  };
  BleHost &host_;
  BleResultSink &sink_;
  HealthProgress *health_;
  std::array<Peer, kBlePeers> peers_{};
  BleContext scan_;
  std::array<BleEvent, kBleQueue> queue_{};
  std::atomic_flag queue_lock_ = ATOMIC_FLAG_INIT;
  size_t head_ = 0, count_ = 0;
  uint32_t startup_deadline_ = 0, scan_deadline_ = 0, scan_duration_ = 0;
  uint8_t initiating_ = kBlePeers, cursor_ = 0;
  bool begun_ = false, enabled_ = false, stopping_ = false, startup_failed_ = false;
  bool scanning_ = false, scan_reported_ = false, startup_confirmed_ = false;
  bool scan_cancel_attempted_ = false, scan_pending_ = false;
  int scan_cancel_error_ = 0;
  void cancelScanOnce();
  bool pop(BleEvent &);
  void resetContext(BleContext &, uint8_t, uint32_t, BlePhase, uint32_t, uint16_t, uint16_t);
  void retire(uint8_t, BleFault, int = 0);
  void event(const BleEvent &);
  bool launch(uint8_t, BleCommand, uint32_t);
  void advance(uint8_t, uint32_t);
  void finishDiscovery(uint8_t);
  void emit(uint8_t, BleResultKind, const BleEvent & = BleEvent{});
};
} // namespace ridesync
