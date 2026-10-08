# NimBLE restore source fixture

`restore_function.c.txt` preserves the exact `ble_nvs_restore_sec_keys` body and
its source copyright/license header from NimBLE-Arduino 2.3.6 commit
`dfb4ac561a06797081be9e752902a6582e7f029e`,
`src/nimble/nimble/host/store/config/src/ble_store_nvs.c`.

The function is compiled unchanged in the RED sanitizer harness, and with only
the two zero-count guards in GREEN. NVS population and record containers are
explicit synthetic boundary stand-ins; this is not a camera/device capture or
physical NVS test. Empty and either one-sided security store exercise an index -1
UBSan failure upstream; ASan alone can miss adjacent addressable globals.

Retained Apache-2.0 LICENSE and upstream project/NimBLE NOTICE files cover this
source fixture. The build-time patch independently validates the complete
upstream source SHA256 before modifying the installed dependency. See
[BLE transport sources](../../../docs/sources.md) and
[transport contract](../../../docs/ble_transport.md).


## Targeted reset excerpts

All excerpts use the same exact upstream revision above and retain its file
copyright/license header. Paths below are relative to `src/nimble/nimble/`:

| Fixture | Source and exact function bodies |
| --- | --- |
| `config_reset.c.txt` | `host/store/config/src/ble_store_config.c`: delete_obj, find_sec, delete_sec, delete_our_sec, delete_peer_sec, find_cccd, delete_cccd, find_rpa_rec, delete_rpa_rec, find_csfc, delete_csfc (all `ble_store_config_` prefixed). |
| `nvs_lookup.c.txt` | `host/store/config/src/ble_store_nvs.c`: `get_nvs_matching_index`, `get_nvs_db_attribute`. Exact original bodies, plus the separately applied guarded private-record stride correction in GREEN. |
| `nvs_reset.c.txt` | `host/store/config/src/ble_store_nvs.c`: `ble_nvs_delete_value`, `ble_store_config_persist_{cccds,csfcs,rpa_recs,peer_secs,our_secs}`, `ble_store_persist_peer_records`. |
| `privacy_reset.c.txt` | `host/src/ble_hs_resolv.c`: `ble_hs_is_on_resolv_list`, `ble_hs_resolv_list_find`, `ble_hs_resolv_list_rmv`, `ble_rpa_remove_peer_dev_rec`. |
| `privacy_add.c.txt` | Same privacy source: `ble_hs_resolv_list_add`. |
| `delete_dispatch.c.txt` | `host/src/ble_store.c`: `ble_store_delete`, including the host mutex and real callback dispatch. |

`reset_sdk_prefix.c.txt` and `reset_sdk_suffix.c.txt` are RideSync-authored
synthetic ABI/table/dispatch boundary declarations. They do not replace the SDK
mutation algorithm. NVS open/read/erase/commit and radio reports are synthetic;
actual source removes RAM first and propagates (or masks, for private device
records) persistence errors. Tests pause inside actual `ble_store_delete` while
its host mutex is held. Direct-host and combined real `BleCentral`/host tests
cover deferred operations, never-attached and retained slots, deadlines,
cancellation, full tables, orphan records, partial writes and final release.

Privacy tests run the exact original bodies for independent ASan and typed alias
RED, then only the two approved corrections for GREEN. Whole-source SHA guards
apply at installation; partial excerpts alone cannot qualify an SDK dependency.
No addresses, keys or camera captures are recorded in test output.


Repair fixtures use real one-based durable keys, zero-based RAM and the pinned
uint8_t private bitfield (24-byte peer security, 44-byte enclosing record, offset20).
The actual NVS key-selection helpers execute without a correct-behavior substitute.
Two foreign private peers precede/follow the target across first/middle/last
permutations. Original target-last lookup erases a foreign durable key and fails;
original first/middle controls pass, and all permutations pass after correction.
Cold maximum-index/hole restoration, all resolving identity/pseudo/RPA aliases
in both orders, and exact/beyond/rollover reserved-connect deadlines are covered.
The read-only resolving SDK addition is authored by RideSync in the guarded patch,
not represented as an original upstream source excerpt.

Duplicate target private members and a foreign identity carrying target peer_sec
refuse before mutation; equal unrelated foreign members with a unique target
remain removable and preserved.

Duplicated durable target members hiding a different live target member also
refuse before mutation: target membership must be a durable/live bijection.

Modes83..87 cover identical target duplicates followed by a foreign record in each
of our-security, peer-security, CCCD, CSFC and RPA. Actual SDK membership selection
previously erased the foreign key; the backend now refuses before any mutation.
Modes88..92 retain unique targets with equal foreign duplicates. Modes93..97
refuse durable target duplicates hiding distinct live target values;98..102
allow the corresponding bijective distinct targets. Every refusal checks all
live arrays, durable records and foreign resolving entries unchanged, no SDK
deletes, no mutation flag and no global Store fault. SDK helper bodies are unchanged.
