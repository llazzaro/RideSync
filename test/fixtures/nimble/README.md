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
