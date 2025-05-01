#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // Added for strncmp if needed, and general string handling

#include "pjrt_c_api.h"

typedef const PJRT_Api* (*pjrt_init)();

// Helper function to handle PJRT errors
static void handle_error(PJRT_Error* error, const PJRT_Api* api, const char* context) {
  if (error == NULL) {
    return;
  }
  fprintf(stderr, "PJRT Error in %s: ", context);
  PJRT_Error_Message_Args msg_args = {sizeof(PJRT_Error_Message_Args)};
  msg_args.struct_size = PJRT_Error_Message_Args_STRUCT_SIZE; // Ensure struct_size is set
  msg_args.extension_start = NULL;
  msg_args.error = error;
  api->PJRT_Error_Message(&msg_args);
  // Use fprintf for stderr and handle potential null message
  if (msg_args.message != NULL) {
      fprintf(stderr, "%.*s
", (int)msg_args.message_size, msg_args.message);
  } else {
      fprintf(stderr, "[No error message provided]
");
  }

  // Get and print the error code
  PJRT_Error_GetCode_Args code_args = {sizeof(PJRT_Error_GetCode_Args)};
  code_args.struct_size = PJRT_Error_GetCode_Args_STRUCT_SIZE; // Ensure struct_size is set
  code_args.extension_start = NULL;
  code_args.error = error;
  PJRT_Error* code_err = api->PJRT_Error_GetCode(&code_args);
  if (code_err == NULL) {
      fprintf(stderr, "PJRT Error Code: %d
", code_args.code);
  } else {
      // Handle error while getting error code (though unlikely)
       PJRT_Error_Destroy_Args destroy_code_err_args = {sizeof(PJRT_Error_Destroy_Args)};
       destroy_code_err_args.struct_size = PJRT_Error_Destroy_Args_STRUCT_SIZE;
       destroy_code_err_args.extension_start = NULL;
       destroy_code_err_args.error = code_err;
       api->PJRT_Error_Destroy(&destroy_code_err_args);
       fprintf(stderr, "Could not retrieve error code.
");
  }


  PJRT_Error_Destroy_Args destroy_args = {sizeof(PJRT_Error_Destroy_Args)};
  destroy_args.struct_size = PJRT_Error_Destroy_Args_STRUCT_SIZE; // Ensure struct_size is set
  destroy_args.extension_start = NULL;
  destroy_args.error = error;
  api->PJRT_Error_Destroy(&destroy_args);
  exit(EXIT_FAILURE); // Exit after handling the error
}


static int load_plugin(void)
{
    static const char plugin[] = "./pjrt_c_api_cpu_plugin.so";
    pjrt_init init_fn;
    const PJRT_Api* api;
    void *handle = dlopen(plugin, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "Error loading plugin '%s': %s
", plugin, dlerror());
        return 1;
    }

    init_fn = (pjrt_init)dlsym(handle, "GetPjrtApi");
    if (!init_fn) {
        fprintf(stderr, "Error finding symbol 'GetPjrtApi' in '%s': %s
", plugin, dlerror());
        dlclose(handle);
        return 1;
    }
    api = init_fn();
    if (api == NULL) {
         fprintf(stderr, "Error: GetPjrtApi returned NULL
");
         dlclose(handle);
         return 1;
    }
    // Basic API struct size check
    if (api->struct_size < PJRT_Api_STRUCT_SIZE) {
         fprintf(stderr, "Error: Loaded PJRT_Api struct size (%zu) is smaller than expected (%d).
",
                 api->struct_size, PJRT_Api_STRUCT_SIZE);
         // Optionally check major version compatibility here too
         dlclose(handle);
         return 1;
    }

    fprintf(stderr, "Loaded PJRT Plugin: %s
", plugin);
    fprintf(stderr, "Reported PJRT API Version: %d.%d
", api->pjrt_api_version.major_version, api->pjrt_api_version.minor_version);

    {
        PJRT_Error* error;
        PJRT_Plugin_Initialize_Args args;
        args.struct_size = PJRT_Plugin_Initialize_Args_STRUCT_SIZE;
        args.extension_start = NULL;
        error = api->PJRT_Plugin_Initialize(&args);
        // Use the helper function for error handling
        handle_error(error, api, "PJRT_Plugin_Initialize");
        printf("PJRT Plugin Initialized successfully.
"); // Use printf for standard output
    }

    // --- Added code to get and print attributes ---
    {
        PJRT_Plugin_Attributes_Args attr_args = {sizeof(PJRT_Plugin_Attributes_Args)};
        attr_args.struct_size = PJRT_Plugin_Attributes_Args_STRUCT_SIZE; // Set struct_size
        attr_args.extension_start = NULL;
        PJRT_Error* attr_error = api->PJRT_Plugin_Attributes(&attr_args);
        handle_error(attr_error, api, "PJRT_Plugin_Attributes"); // Use helper

        printf("PJRT Plugin Attributes (Count: %zu):
", attr_args.num_attributes);

        for (size_t i = 0; i < attr_args.num_attributes; ++i) {
            const PJRT_NamedValue* attr = &attr_args.attributes[i];
            // Print attribute index and name safely
            printf("  Attribute %zu: Name='%.*s', Type=%d, Size=%zu, Value=",
                   i, (int)attr->name_size, attr->name ? attr->name : "[NULL NAME]", attr->type, attr->value_size);

            switch (attr->type) {
                case PJRT_NamedValue_kString:
                    // Ensure string_value is not NULL before printing
                    printf("'%.*s'
", (int)attr->value_size, attr->string_value ? attr->string_value : "[NULL STRING]");
                    break;
                case PJRT_NamedValue_kInt64:
                    // Cast to long long for portability with printf
                    printf("%lld
", (long long)attr->int64_value);
                    break;
                case PJRT_NamedValue_kInt64List:
                    printf("[");
                    // Check if int64_array_value is NULL before dereferencing
                    if (attr->int64_array_value != NULL) {
                        for (size_t j = 0; j < attr->value_size; ++j) {
                        printf("%lld%s", (long long)attr->int64_array_value[j],
                                (j == attr->value_size - 1) ? "" : ", ");
                        }
                    } else {
                        printf("[NULL ARRAY]");
                    }
                    printf("]\n");
                    break;
                case PJRT_NamedValue_kFloat:
                    printf("%f\n", attr->float_value);
                    break;
                case PJRT_NamedValue_kBool:
                    printf("%s\n", attr->bool_value ? "true" : "false");
                    break;
                default:
                    printf("[Unknown Type %d]
", attr->type);
            }
        }
         printf("Finished printing attributes.
");
    }
    // --- End of added code ---

    dlclose(handle); // Close the handle after use
    return 0;
}

int main(int argc, const char **argv)
{
    int rc = load_plugin();

    // Keep original main content if any, or just return rc
    (void)argc; // Suppress unused parameter warning
    (void)argv; // Suppress unused parameter warning
    if (rc == 0) {
        printf("hlo_test completed successfully.
");
    } else {
        fprintf(stderr, "hlo_test failed.
");
    }
    return rc;
}
