/* Standalone misc devices; do not register another stock binderfs. */
#undef CONFIG_ANDROID_BINDERFS
/* Linux 6.1 Binder extension, local to this module on stock 5.15.
 * Layout/command from include/uapi/linux/android/binder.h, GPL-2.0 WITH
 * Linux-syscall-note. No stock kernel UAPI or task structure is changed. */
#include <uapi/linux/android/binder.h>
#ifndef BINDER_GET_EXTENDED_ERROR
struct binder_extended_error {
    __u32 id;
    __u32 command;
    __s32 param;
};
#define BINDER_GET_EXTENDED_ERROR _IOWR('b', 17, struct binder_extended_error)
#endif
