// SPDX-License-Identifier: Apache-2.0
// Check the actual donor HCS parser against the generated host/ID sequence.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "device_resource_if.h"

extern void SetHcsBlobPath(const char *path);
extern const struct DeviceResourceNode *HcsGetRootNode(void);
extern const struct DeviceResourceNode *HcsGetNodeByMatchAttr(const struct DeviceResourceNode *, const char *);
extern int HcsGetString(const struct DeviceResourceNode *, const char *, const char **, const char *);

int main(int argc, char **argv)
{
    assert(argc > 2);
    SetHcsBlobPath(argv[1]);
    const struct DeviceResourceNode *root = HcsGetRootNode();
    assert(root);
    const struct DeviceResourceNode *manager = HcsGetNodeByMatchAttr(root, "hdf_manager");
    assert(manager);
    const struct DeviceResourceNode *host = manager->child;
    int index = 0;
    for (; host; host = host->sibling, ++index) {
        assert(index + 2 < argc);
        const char *name = NULL;
        assert(HcsGetString(host, "hostName", &name, NULL) == 0);
        assert(name && strcmp(name, argv[index + 2]) == 0);
        printf("PASS native HCB host ID %d = %s\n", index, name);
    }
    assert(index == argc - 2);
    return 0;
}
