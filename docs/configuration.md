# NVS startup admission

The pinned firmware wraps the C SDK `nvs_flash_init()` and
`esp_partition_erase_range(const esp_partition_t *, size_t, size_t)` at link time.
Before `setup()`, Arduino initializes default NVS. Its no-free-pages/new-version
fallback attempts a full format; the guard returns `ESP_ERR_NOT_SUPPORTED`
for offset zero and the exact nonzero size of every DATA/NVS partition, regardless
of label. Partial page erases, non-NVS operations, null partitions and invalid
ranges retain the real SDK's validation/result. No SDK or partition layout edits
are required. The guard also refuses explicit whole-NVS formats.

`ridesync::nvsBootStatus()` exposes `init_observed`, `init_in_progress`,
`first_init_failure`, `last_init_result`, `format_refused` and `refusal_error`.
The first initialization failure and format refusal remain latched for this boot;
a later successful init does not clear them. `persistenceAllowed()` requires an
observed successful default init, no init in progress, no earlier init failure,
and no refused format. Evidence uses constant-initialized, lock-free integer
atomics, fixed publication steps and one first-failure compare/exchange; it does
not allocate, log, or access NVS handles before setup.

This snapshot is observational, not a transaction with an SDK operation. Issue
#18 must check it in the owning context at every config/BLE admission and
serialize initialization, erase and handle use; overlapping initializations are
not supported by the progress flag. Named partition initialization must supply
its own owner status. A denied IDF erase may already have deinitialized NVS and
invalidated handles: never continue with cached handles or automatically retry,
save defaults, format, reset or reboot. Successful default init does not prove
BLE bond restoration; stack restore outcomes still need separate observation.

Current `setup()` reports config/BLE admission and keeps diagnostic supervision
running on missing/failed init or refused formatting. All physical drivers remain
disabled; there is no persistent config store or BLE host yet. RAM defaults must
not admit camera control while this gate is closed. NVS failure is a device/config
fault, not scheduler starvation. Future independently qualified standalone
GNSS/SD/IMU worker lifetimes must not depend on camera-NVS readiness.

This is whole-partition format interception, not complete forensic or physical
preservation. IDF initialization can still repair/write/discard corrupt pages;
partial GC erases remain enabled. Sequential partial erases, raw flash APIs,
bootloader/provisioning/upload paths and same-object/internal future SDK calls
can bypass the guard. Encryption/key partitions are not qualified. SDK/compiler/
LTO changes require new archive and final-ELF proof. Settings/bond persistence,
reboot and power-failure acceptance remain physical gates in #18/#31.
