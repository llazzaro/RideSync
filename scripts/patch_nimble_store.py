"""Narrow pinned upstream compatibility fix; never alter application bond stores."""
from pathlib import Path
import hashlib

PIN = "dfb4ac561a06797081be9e752902a6582e7f029e"
RELATIVE = "src/nimble/nimble/host/store/config/src/ble_store_nvs.c"
ORIGINAL = """    ble_store_config_our_bond_count = ble_store_config_our_secs[ble_store_config_num_our_secs - 1].bond_count;
    ble_store_config_peer_bond_count = ble_store_config_peer_secs[ble_store_config_num_peer_secs - 1].bond_count;"""
FIXED = """    ble_store_config_our_bond_count = ble_store_config_num_our_secs ? ble_store_config_our_secs[ble_store_config_num_our_secs - 1].bond_count : 0;
    ble_store_config_peer_bond_count = ble_store_config_num_peer_secs ? ble_store_config_peer_secs[ble_store_config_num_peer_secs - 1].bond_count : 0;"""
# Exact upstream contents including license. Fail closed on any dependency drift.
SOURCE_SHA256 = "e4d61d3b6403e263d40f2f498c4d6ac64ee1727ccd3fd6f040ba0e1908c26144"


def guard_zero_counts(source):
    upstream = source.replace(FIXED, ORIGINAL)
    if upstream.count(ORIGINAL) != 1:
        raise RuntimeError("NimBLE restore guard match missing/ambiguous")
    return upstream.replace(ORIGINAL, FIXED)


def corrected(source):
    upstream = source.replace(FIXED, ORIGINAL)
    if hashlib.sha256(upstream.encode()).hexdigest() != SOURCE_SHA256:
        raise RuntimeError(f"NimBLE {PIN} restore source mismatch")
    if upstream.count(ORIGINAL) != 1:
        raise RuntimeError("NimBLE restore guard match missing/ambiguous")
    return guard_zero_counts(upstream)


PRIVACY_RELATIVE = "src/nimble/nimble/host/src/ble_hs_resolv.c"
PRIVACY_SHA256 = "d4392a827f71ad2dfb04a137c95d3d9886cd37f2c253ed535885a639adcab5f4"
PRIVACY_CHANGES = (
    ("(g_ble_hs_resolv_data.rl_cnt - position) * sizeof (struct",
     "(g_ble_hs_resolv_data.rl_cnt - position - 1) * sizeof (struct"),
    ("if ((!memcmp(rl->rl_identity_addr, addr, BLE_DEV_ADDR_LEN)) || (!memcmp(rl->rl_peer_rpa, addr, BLE_DEV_ADDR_LEN))) {",
     "if ((rl->rl_addr_type == addr_type && !memcmp(rl->rl_identity_addr, addr, BLE_DEV_ADDR_LEN)) || (addr_type == BLE_ADDR_RANDOM && !memcmp(rl->rl_peer_rpa, addr, BLE_DEV_ADDR_LEN))) {"),
)


def guard_privacy(source):
    for original, fixed in PRIVACY_CHANGES:
        source = source.replace(fixed, original)
        if source.count(original) != 1:
            raise RuntimeError("NimBLE privacy guard match missing/ambiguous")
        source = source.replace(original, fixed)
    return source


def corrected_privacy(source):
    upstream = source
    for original, fixed in PRIVACY_CHANGES:
        upstream = upstream.replace(fixed, original)
    if hashlib.sha256(upstream.encode()).hexdigest() != PRIVACY_SHA256:
        raise RuntimeError(f"NimBLE {PIN} privacy source mismatch")
    return guard_privacy(upstream)


def install(directory):
    source = Path(directory) / RELATIVE
    text = source.read_text()
    fixed = corrected(text)
    if text != fixed:
        source.write_text(fixed)
    source = Path(directory) / PRIVACY_RELATIVE
    text = source.read_text()
    fixed = corrected_privacy(text)
    if text != fixed:
        source.write_text(fixed)


if "Import" in globals():
    Import("env")
    # PlatformIO installs declared lib_deps before running extra scripts.
    libraries = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV")
    matches = list(libraries.glob("*/" + RELATIVE))
    if len(matches) != 1:
        raise RuntimeError("Exactly one pinned NimBLE dependency is required")
    install(matches[0].parents[7])
    print("RideSync: verified exact-pin NimBLE restore and typed privacy bounds guards before compilation")
