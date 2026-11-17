/* SPDX-License-Identifier: Apache-2.0
 * Installer-side code-signature compatibility for the OP13 stock kernel.
 *
 * libinstalls.z.so (BMS installd) calls OHOS::Security::CodeSign::CodeSignUtils
 * to enable fs-verity with a code signature on every native library of an app.
 * That needs OpenHarmony kernel support: XPM (/proc/sys/kernel/xpm) and
 * fs-verity builtin signatures.  The OP13 GKI kernel has neither
 * (CONFIG_FS_VERITY_BUILTIN_SIGNATURES unset, no XPM), so revision 19 installed
 * 0 of 77 donor bundles with native libraries, SceneBoard included, and
 * AbilityManager polled ImplicitQueryInfoByPriority forever.
 *
 * This library is placed first in libinstalls.z.so's DT_NEEDED list so these
 * definitions win symbol lookup for the installer only.  Each call is logged
 * and reports success; the HAP signature verification performed earlier by
 * BMS (hapverify) is not touched.  The stock kernel cannot enforce code
 * signatures at exec time either, so no protection this device can provide is
 * removed.
 */
#include <dlfcn.h>
#include <stdint.h>

typedef int (*hilog_print_fn)(int type, int level, unsigned int domain, const char *tag, const char *fmt, ...);

static void note(const char *what)
{
    static hilog_print_fn print;
    static unsigned long calls;
    if (!print) print = (hilog_print_fn)dlsym(RTLD_DEFAULT, "HiLogPrint");
    ++calls;
    // LOG_CORE=3, LOG_INFO=4; domain of BMS installd (0xD001120).
    if (print && (calls <= 20 || calls % 200 == 0))
        print(3, 4, 0xD001120, "OP13CodeSign", "%{public}s: kernel lacks OH code signing; skipped (#%{public}lu)",
              what, calls);
}

/* static int32_t CodeSignUtils::EnforceCodeSignForApp(const EntryMap &, const std::string &) */
int32_t _ZN4OHOS8Security8CodeSign13CodeSignUtils21EnforceCodeSignForAppERKNSt3__h13unordered_mapINS3_12basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEESA_NS3_4hashISA_EENS3_8equal_toISA_EENS8_INS3_4pairIKSA_SA_EEEEEERSG_(void)
{
    note("EnforceCodeSignForApp(entryMap, signatureDir)");
    return 0;
}

/* static int32_t CodeSignUtils::EnforceCodeSignForApp(const std::string &, const EntryMap &, FileType) */
int32_t _ZN4OHOS8Security8CodeSign13CodeSignUtils21EnforceCodeSignForAppERKNSt3__h12basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEERKNS3_13unordered_mapIS9_S9_NS3_4hashIS9_EENS3_8equal_toIS9_EENS7_INS3_4pairISA_S9_EEEEEENS1_8FileTypeE(void)
{
    note("EnforceCodeSignForApp(path, entryMap, type)");
    return 0;
}

/* static int32_t CodeSignUtils::EnforceCodeSignForAppWithOwnerId(...) */
int32_t _ZN4OHOS8Security8CodeSign13CodeSignUtils32EnforceCodeSignForAppWithOwnerIdERKNSt3__h12basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEESB_RKNS3_13unordered_mapIS9_S9_NS3_4hashIS9_EENS3_8equal_toIS9_EENS7_INS3_4pairISA_S9_EEEEEENS1_8FileTypeE(void)
{
    note("EnforceCodeSignForAppWithOwnerId");
    return 0;
}

/* static int32_t CodeSignUtils::EnforceCodeSignForFile(const std::string &, const uint8_t *, uint32_t) */
int32_t _ZN4OHOS8Security8CodeSign13CodeSignUtils22EnforceCodeSignForFileERKNSt3__h12basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEEPKhj(void)
{
    note("EnforceCodeSignForFile");
    return 0;
}
