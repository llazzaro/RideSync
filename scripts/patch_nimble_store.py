"""Exact-pin SDK compatibility and opt-in wake refusal at private boundaries."""
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


PRIVATE_ORIGINAL = """                    err = get_nvs_matching_index(&p_dev_rec.peer_sec,
                                                 &((struct ble_hs_dev_records *)value)->peer_sec,
                                                 num_value,
                                                 sizeof(struct ble_hs_peer_sec));"""
PRIVATE_FIXED = """                    /* Device records have a larger stride than peer_sec. */
                    int peer_index;
                    err = -1;
                    for (peer_index = 0; peer_index < num_value; peer_index++) {
                        if (get_nvs_matching_index(&p_dev_rec.peer_sec,
                                &((struct ble_hs_dev_records *)value)[peer_index].peer_sec,
                                1, sizeof(struct ble_hs_peer_sec)) == 0) {
                            err = peer_index;
                            break;
                        }
                    }"""


def guard_private_stride(source):
    source = source.replace(PRIVATE_FIXED, PRIVATE_ORIGINAL)
    if source.count(PRIVATE_ORIGINAL) != 1:
        raise RuntimeError("NimBLE private-record stride match missing/ambiguous")
    return source.replace(PRIVATE_ORIGINAL, PRIVATE_FIXED)



# Narrow refusal hook at private SDK boundaries that bypass public store callbacks.
# An absent hook preserves upstream behavior for independent SDK fixtures.
WAKE_DECL = """#ifdef __cplusplus
extern "C" {
#endif
extern int ridesync_ble_wake_store_allowed(int, const void *, const void *) __attribute__((weak));
extern int ridesync_ble_wake_peer_allowed(unsigned char, const unsigned char *) __attribute__((weak));
extern int ridesync_ble_wake_security_allowed(unsigned short) __attribute__((weak));
#ifdef __cplusplus
}
#endif
"""

WAKE_NVS_CHANGES = (
    ("ble_store_nvs_peer_records(int obj_type, const struct ble_hs_dev_records *p_dev_rec)\n{\n",
     "ble_store_nvs_peer_records(int obj_type, const struct ble_hs_dev_records *p_dev_rec)\n{\n"
     "    if (ridesync_ble_wake_store_allowed &&\n"
     "        !ridesync_ble_wake_store_allowed(obj_type, NULL, p_dev_rec)) {\n"
     "        return BLE_HS_ESTORE_FAIL;\n    }\n"),
    ("ble_nvs_delete_value(int obj_type, int8_t index)\n{\n",
     "ble_nvs_delete_value(int obj_type, int8_t index)\n{\n"
     "#if MYNEWT_VAL(BLE_HOST_BASED_PRIVACY)\n"
     "    if (obj_type == BLE_STORE_OBJ_TYPE_PEER_DEV_REC &&\n"
     "        ridesync_ble_wake_store_allowed &&\n"
     "        !ridesync_ble_wake_store_allowed(obj_type, NULL, NULL)) {\n"
     "        struct ble_hs_dev_records target = {0};\n"
     "        char target_key[NIMBLE_NVS_STR_NAME_MAX_LEN];\n"
     "        if (index < 0 || index > get_nvs_max_obj_value(obj_type)) {\n"
     "            return BLE_HS_ESTORE_FAIL;\n        }\n"
     "        get_nvs_key_string(obj_type, index, target_key);\n"
     "        if (get_nvs_peer_record(target_key, &target) != ESP_OK ||\n"
     "            !ridesync_ble_wake_store_allowed(obj_type, NULL, &target)) {\n"
     "            return BLE_HS_ESTORE_FAIL;\n        }\n    }\n#endif\n"),
    ("    err = nvs_get_blob(nimble_handle, key_string, p_dev_rec,\n                       &required_size);",
     "    err = nvs_get_blob(nimble_handle, key_string, p_dev_rec,\n                       &required_size);\n"
     "    if (err == ESP_OK && ridesync_ble_wake_store_allowed &&\n"
     "        !ridesync_ble_wake_store_allowed(BLE_STORE_OBJ_TYPE_PEER_DEV_REC, NULL, p_dev_rec)) {\n"
     "        memset(p_dev_rec, 0, sizeof(*p_dev_rec));\n"
     "        err = BLE_HS_ESTORE_FAIL;\n    }"),
)

WAKE_PRIVACY_CHANGES = (
    ("ble_rpa_resolv_add_peer_rec(uint8_t *peer_addr)\n{\n",
     "ble_rpa_resolv_add_peer_rec(uint8_t *peer_addr)\n{\n"
     "    if (ridesync_ble_wake_peer_allowed &&\n"
     "        !ridesync_ble_wake_peer_allowed(BLE_ADDR_RANDOM, peer_addr)) {\n"
     "        return BLE_HS_ESTORE_FAIL;\n    }\n"),
    ("ble_rpa_remove_peer_dev_rec(struct ble_hs_dev_records *p_dev_rec)\n{\n",
     "ble_rpa_remove_peer_dev_rec(struct ble_hs_dev_records *p_dev_rec)\n{\n"
     "    if (ridesync_ble_wake_store_allowed &&\n"
     "        !ridesync_ble_wake_store_allowed(BLE_STORE_OBJ_TYPE_PEER_DEV_REC, NULL, p_dev_rec)) {\n"
     "        return BLE_HS_ESTORE_FAIL;\n    }\n"),
    ("ble_rpa_find_rl_from_peer_records(uint8_t *peer_addr, uint8_t *peer_addr_type)\n{\n",
     "ble_rpa_find_rl_from_peer_records(uint8_t *peer_addr, uint8_t *peer_addr_type)\n{\n"
     "    if (ridesync_ble_wake_peer_allowed &&\n"
     "        !ridesync_ble_wake_peer_allowed(*peer_addr_type, peer_addr)) {\n"
     "        return NULL;\n    }\n"),
    ("ble_hs_resolv_list_add(uint8_t *cmdbuf)\n{\n",
     "ble_hs_resolv_list_add(uint8_t *cmdbuf)\n{\n"
     "    if (ridesync_ble_wake_peer_allowed &&\n"
     "        !ridesync_ble_wake_peer_allowed(cmdbuf[0], cmdbuf + 1)) {\n"
     "        return BLE_HS_ESTORE_FAIL;\n    }\n"),
    ("ble_hs_resolv_list_rmv(uint8_t addr_type, uint8_t *ident_addr)\n{\n",
     "ble_hs_resolv_list_rmv(uint8_t addr_type, uint8_t *ident_addr)\n{\n"
     "    if (ridesync_ble_wake_peer_allowed &&\n"
     "        !ridesync_ble_wake_peer_allowed(addr_type, ident_addr)) {\n"
     "        return BLE_HS_ESTORE_FAIL;\n    }\n"),
)

WAKE_SMP_CHANGES = (
    ("ble_sm_pair_initiate(uint16_t conn_handle)\n{\n",
     "ble_sm_pair_initiate(uint16_t conn_handle)\n{\n"
     "    if (ridesync_ble_wake_security_allowed &&\n"
     "        !ridesync_ble_wake_security_allowed(conn_handle)) {\n"
     "        return BLE_HS_ENOTSUP;\n    }\n"),
    ("    conn_handle = ble_l2cap_get_conn_handle(chan);\n"
     "    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {\n"
     "        return BLE_HS_ENOTCONN;\n    }\n",
     "    conn_handle = ble_l2cap_get_conn_handle(chan);\n"
     "    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {\n"
     "        return BLE_HS_ENOTCONN;\n    }\n"
     "    if (ridesync_ble_wake_security_allowed &&\n"
     "        !ridesync_ble_wake_security_allowed(conn_handle)) {\n"
     "        return BLE_HS_ENOTSUP;\n    }\n"),
)


def strip_wake(source, changes):
    source = source.replace(WAKE_DECL, "")
    for original, fixed in changes:
        source = source.replace(fixed, original)
    return source


def apply_wake(source, changes):
    source = strip_wake(source, changes)
    matched = False
    for original, fixed in changes:
        count = source.count(original)
        if count > 1:
            raise RuntimeError("NimBLE wake refusal match ambiguous")
        if count:
            source = source.replace(original, fixed)
            matched = True
    if not matched:
        raise RuntimeError("NimBLE wake refusal match missing")
    return WAKE_DECL + source


def guard_wake_private(source):
    return apply_wake(source, WAKE_NVS_CHANGES)


def guard_wake_privacy(source):
    return apply_wake(source, WAKE_PRIVACY_CHANGES)


def guard_wake_smp(source):
    return apply_wake(source, WAKE_SMP_CHANGES)


def corrected(source):
    upstream = strip_wake(source, WAKE_NVS_CHANGES).replace(FIXED, ORIGINAL).replace(PRIVATE_FIXED, PRIVATE_ORIGINAL)
    if hashlib.sha256(upstream.encode()).hexdigest() != SOURCE_SHA256:
        raise RuntimeError(f"NimBLE {PIN} restore source mismatch")
    if upstream.count(ORIGINAL) != 1:
        raise RuntimeError("NimBLE restore guard match missing/ambiguous")
    return guard_wake_private(guard_private_stride(guard_zero_counts(upstream)))


PRIVACY_RELATIVE = "src/nimble/nimble/host/src/ble_hs_resolv.c"
PRIVACY_SHA256 = "d4392a827f71ad2dfb04a137c95d3d9886cd37f2c253ed535885a639adcab5f4"
RESOLV_READ = """/* RideSync: bounded read-only inspection of every peer resolving entry. */
int
ridesync_ble_resolv_read(unsigned index, struct ble_hs_resolv_entry *entry)
{
    if (g_ble_hs_resolv_data.rl_cnt < 1 ||
        g_ble_hs_resolv_data.rl_cnt > BLE_RESOLV_LIST_SIZE) {
        return BLE_HS_EUNKNOWN;
    }
    if (index >= g_ble_hs_resolv_data.rl_cnt - 1) {
        return BLE_HS_ENOENT;
    }
    *entry = g_ble_hs_resolv_list[index + 1];
    return 0;
}

"""

PRIVACY_CHANGES = (
    ("int\nble_hs_resolv_list_rmv", RESOLV_READ + "int\nble_hs_resolv_list_rmv"),
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
    upstream = strip_wake(source, WAKE_PRIVACY_CHANGES)
    for original, fixed in PRIVACY_CHANGES:
        upstream = upstream.replace(fixed, original)
    if hashlib.sha256(upstream.encode()).hexdigest() != PRIVACY_SHA256:
        raise RuntimeError(f"NimBLE {PIN} privacy source mismatch")
    return guard_wake_privacy(guard_privacy(upstream))


SMP_RELATIVE = "src/nimble/nimble/host/src/ble_sm.c"
SMP_SHA256 = "f88810ae6fcdf1f499efcb1471b18b36a154e7cd67dd45145b70bd4822d120da"


def corrected_smp(source):
    upstream = strip_wake(source, WAKE_SMP_CHANGES)
    if hashlib.sha256(upstream.encode()).hexdigest() != SMP_SHA256:
        raise RuntimeError(f"NimBLE {PIN} security source mismatch")
    return guard_wake_smp(upstream)


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
    source = Path(directory) / SMP_RELATIVE
    text = source.read_text()
    fixed = corrected_smp(text)
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
    print("RideSync: verified exact-pin NimBLE restore and typed privacy bounds and opt-in wake refusal guards before compilation")
