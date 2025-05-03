#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // Added for general string handling

#include "pjrt_c_api.h"

typedef const PJRT_Api* (*pjrt_init)();

struct file_data {
    void* data;
    size_t size;
};


// Helper function to handle PJRT errors
static void handle_error(PJRT_Error* error, const PJRT_Api* api, const char* context) {
  if (error == NULL) {
    return;
  }
  fprintf(stderr, "PJRT Error in %s: ", context);
  PJRT_Error_Message_Args msg_args;
  msg_args.struct_size = PJRT_Error_Message_Args_STRUCT_SIZE;
  msg_args.extension_start = NULL;
  msg_args.error = error;
  api->PJRT_Error_Message(&msg_args);
  // Use fprintf for stderr and handle potential null message
  if (msg_args.message != NULL) {
      fprintf(stderr, "%.*s\n", (int)msg_args.message_size, msg_args.message);
  } else {
      fprintf(stderr, "[No error message provided]\n");
  }

  // Get and print the error code
  PJRT_Error_GetCode_Args code_args = {0};
  code_args.struct_size = PJRT_Error_GetCode_Args_STRUCT_SIZE;
  code_args.extension_start = NULL; 
  code_args.error = error;
  PJRT_Error* code_err = api->PJRT_Error_GetCode(&code_args);
  if (code_err == NULL) {
      fprintf(stderr, "PJRT Error Code: %d\n", code_args.code);  
  } else {
      // Handle error while getting error code (though unlikely)
      PJRT_Error_Destroy_Args destroy_code_err_args;
      destroy_code_err_args.struct_size = PJRT_Error_Destroy_Args_STRUCT_SIZE;
      destroy_code_err_args.extension_start = NULL;
      destroy_code_err_args.error = code_err;
      api->PJRT_Error_Destroy(&destroy_code_err_args);
      fprintf(stderr, "Could not retrieve error code.\n");
      
  }

  PJRT_Error_Destroy_Args destroy_args = {0};
  destroy_args.struct_size = PJRT_Error_Destroy_Args_STRUCT_SIZE; // Ensure struct_size is set
  destroy_args.extension_start = NULL;
  destroy_args.error = error;
  api->PJRT_Error_Destroy(&destroy_args);
  exit(EXIT_FAILURE); 
}

// --- Function to get and print plugin attributes ---
static void print_plugin_attributes(const PJRT_Api* api) {
    PJRT_Plugin_Attributes_Args attr_args = {0};
    attr_args.struct_size = PJRT_Plugin_Attributes_Args_STRUCT_SIZE; // Set struct_size
    attr_args.extension_start = NULL;
    PJRT_Error* attr_error = api->PJRT_Plugin_Attributes(&attr_args);
    handle_error(attr_error, api, "PJRT_Plugin_Attributes"); // Use helper

    printf("PJRT Plugin Attributes (Count: %zu):\n", attr_args.num_attributes);

    for (size_t i = 0; i < attr_args.num_attributes; ++i) {
        const PJRT_NamedValue* attr = &attr_args.attributes[i];
        // Print attribute index and name safely
        printf("  Attribute %zu: Name='%.*s', Type=%d, Size=%zu, Value=",
               i, (int)attr->name_size, attr->name ? attr->name : "[NULL NAME]", attr->type, attr->value_size);

        switch (attr->type) {
            case PJRT_NamedValue_kString:
                // Ensure string_value is not NULL before printing
                printf("'%.*s'\n", (int)attr->value_size, attr->string_value ? attr->string_value : "[NULL STRING]");
                break; 
            case PJRT_NamedValue_kInt64:
                // Cast to long long for portability with printf
                printf("%lld\n", (long long)attr->int64_value);
                break;
            case PJRT_NamedValue_kInt64List:
                printf("["); // Corrected string formatting
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
                printf("[Unknown Type %d]\n", attr->type);
        }
    }
    printf("Finished printing attributes.\n");
}
// --- End of print_plugin_attributes function ---


static int close_plugin(void* handle, const char* plugin, const char* message) {
    if (message != NULL) {
        fprintf(stderr, "%s\n", message);
    }
    if (handle) {
        if (dlclose(handle) != 0) {
            fprintf(stderr, "Error closing plugin '%s': %s\n", plugin, dlerror());
        }
    }

    return 1;
}

// Function to read a file into a buffer
static int read_file_to_buffer(const char* filename, struct file_data* file_data) {
    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        fprintf(stderr, "Error opening file '%s'\n", filename);
        return 1;
    }

    // Determine file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    if (file_size < 0) {
        fprintf(stderr, "Error getting file size for '%s'\n", filename);
        fclose(file);
        return 1;
    }
    rewind(file);

    // Allocate buffer
    file_data->data = malloc(file_size + 1); // +1 for null terminator
    if (file_data->data == NULL) {
        fprintf(stderr, "Error allocating memory for file '%s'\n", filename);
        fclose(file);
        return 1;
    }
    file_data->size = file_size;

    // Read file content
    ssize_t bytes_read = fread(file_data->data, 1, file_size, file);
    fclose(file);

    if (bytes_read < 0 || bytes_read != (ssize_t)file_size) {
        fprintf(stderr, "Error reading file '%s'\n", filename);
        free(file_data->data);
        return 1;
    }

    return 0;
}
// Frees space that was allocated by read_file_to_buffer
static void free_file_data(struct file_data* file_data) {
    if (file_data->data != NULL) {
        free(file_data->data);
        file_data->data = NULL;
        file_data->size = 0;
    }
}

static int load_plugin(void)
{
    static const char plugin[] = "./pjrt_c_api_cpu_plugin.so";
    pjrt_init init_fn;
    const PJRT_Api* api;
    void *handle = dlopen(plugin, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "Error loading plugin '%s': %s\n", plugin, dlerror());
        return close_plugin(handle, plugin, NULL);
    }

    init_fn = (pjrt_init)dlsym(handle, "GetPjrtApi");
    if (!init_fn) {
        return close_plugin(handle, plugin, "Error finding symbol 'GetPjrtApi'");
    }
    api = init_fn();
    if (api == NULL) {
        return close_plugin(handle, plugin, "Error: GetPjrtApi returned NULL");
    }
    fprintf(stderr, "Loaded PJRT Plugin: %s\n", plugin);
    fprintf(stderr, "Reported PJRT API Version: %d.%d\n", api->pjrt_api_version.major_version,
            api->pjrt_api_version.minor_version);
    // Basic API struct size check
    if (api->struct_size < PJRT_Api_STRUCT_SIZE) {
         fprintf(stderr, "Error: Loaded PJRT_Api struct size (%zu) is smaller than expected (%d).\n",
                 api->struct_size, PJRT_Api_STRUCT_SIZE);
         // Optionally check major version compatibility here too
         dlclose(handle);
         return 1;
    }


    {
        PJRT_Error* error = NULL;
        PJRT_Plugin_Initialize_Args args = {0};
        args.struct_size = PJRT_Plugin_Initialize_Args_STRUCT_SIZE;
        args.extension_start = NULL;
        error = api->PJRT_Plugin_Initialize(&args);
        // Use the helper function for error handling       
       handle_error(error, api, "PJRT_Plugin_Initialize");
       printf("PJRT Plugin Initialized successfully.\n"); // Use printf for standard output
    }

    // Call the dedicated function to print attributes
    print_plugin_attributes(api);

    // Future client creation, device handling etc. would go here

    close_plugin(handle, plugin, NULL); // Close the handle after use
    return 0;
}

int main(int argc, const char **argv)
{
    int rc = load_plugin();

    // Keep original main content if any, or just return rc
    (void)argc; // Suppress unused parameter warning
    (void)argv; // Suppress unused parameter warning
    if (rc == 0) {
        printf("hlo_test completed successfully.\n");
    } else {
        fprintf(stderr, "hlo_test failed.\n");
    }
    return rc;
}