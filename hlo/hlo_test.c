#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#include "pjrt_c_api.h"

typedef const PJRT_Api* (*pjrt_init)();

static int load_plugin(void)
{
    static const char plugin[] = "./pjrt_c_api_cpu_plugin.so";
    pjrt_init init_fn;
    const PJRT_Api* api;
    void *handle = dlopen(plugin, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "Error: %s\n", dlerror());
        return 1;
    }

    init_fn = (pjrt_init)dlsym(handle, "GetPjrtApi");
    if (!init_fn) {
        fprintf(stderr, "Error: %s\n", dlerror());
        dlclose(handle);
        return 1;
    }
    api = init_fn();
    assert(api != NULL);
    fprintf(stderr, "api: %d.%d\n", api->pjrt_api_version.major_version, api->pjrt_api_version.minor_version);
    dlclose(handle);
    return 0;
}

int main(int argc, const char **argv)
{
    int rc = load_plugin();

    (void)argc;
    (void)argv;
    return rc;
}