// SPDX-License-Identifier: Apache-2.0
/*
 * libchipset_vsync.z.so replacement for the OP13 port (revision 31).
 *
 * render_service links three functions of OHOS::Camera::ChipsetVsyncImpl and
 * calls SetVsyncImpl() on its main thread for every frame.  The donor
 * implementation tries to fetch the Huawei "chipset_vsync_service" HDI each
 * time it is not connected; there is no such service (or camera host) on this
 * port, so every frame paid a hdf_devmgr IPC round trip plus two error logs
 * ("get hdi service chipset_vsync_service failed", "Get:get remote object
 * failed!"), about 120 times a second.  These are the only symbols any donor
 * binary imports from the library, and all of them are no-ops here.
 */
#include <stdint.h>

#define EXPORT __attribute__((visibility("default")))

static long instance;

EXPORT void *chipset_vsync_instance(void) __asm__("_ZN4OHOS6Camera16ChipsetVsyncImpl8InstanceEv");
EXPORT int32_t chipset_vsync_init(void *self) __asm__("_ZN4OHOS6Camera16ChipsetVsyncImpl20InitChipsetVsyncImplEv");
EXPORT int32_t chipset_vsync_set(void *self, long timestamp, long period)
    __asm__("_ZN4OHOS6Camera16ChipsetVsyncImpl12SetVsyncImplEll");

void *chipset_vsync_instance(void)
{
    return &instance;
}

int32_t chipset_vsync_init(void *self)
{
    (void)self;
    return 0;
}

int32_t chipset_vsync_set(void *self, long timestamp, long period)
{
    (void)self;
    (void)timestamp;
    (void)period;
    return 0;
}
