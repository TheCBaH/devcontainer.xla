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
// Returns 0 if error is NULL, 1 if an error occurred and was handled.
// It now prints the error but does not exit, allowing the caller to manage cleanup.
static int handle_error(PJRT_Error* error, const PJRT_Api* api, const char* context) {
  if (error == NULL) {
    return 0; // No error
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
  // Don't exit here, let the caller decide based on the return value.
  return 1; // Error occurred
}

// --- Function to get and print plugin attributes ---
static void print_plugin_attributes(const PJRT_Api* api) {
    PJRT_Plugin_Attributes_Args attr_args = {0};
    attr_args.struct_size = PJRT_Plugin_Attributes_Args_STRUCT_SIZE; // Set struct_size
    attr_args.extension_start = NULL;
    PJRT_Error* attr_error = api->PJRT_Plugin_Attributes(&attr_args);
    if (handle_error(attr_error, api, "PJRT_Plugin_Attributes")) return; // Check error

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

    return 1; // Indicate failure for consistency, though it might have closed successfully
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
    file_data->data = malloc(file_size); // No need for +1 for binary data
    if (file_data->data == NULL) {
        fprintf(stderr, "Error allocating memory for file '%s'\n", filename);
        fclose(file);
        return 1;
    }
    file_data->size = file_size;

    // Read file content
    size_t bytes_read = fread(file_data->data, 1, file_size, file); // Use size_t for fread return
    fclose(file);

    if (bytes_read != (size_t)file_size) { // Check if read matches expected size
        fprintf(stderr, "Error reading file '%s' (read %zu bytes, expected %ld)\n", filename, bytes_read, file_size);
        free(file_data->data);
        file_data->data = NULL; // Avoid double free
        file_data->size = 0;
        return 1;
    }

    return 0; // Success
}
// Frees space that was allocated by read_file_to_buffer
static void free_file_data(struct file_data* file_data) {
    if (file_data->data != NULL) {
        free(file_data->data);
        file_data->data = NULL;
        file_data->size = 0;
    }
}

// Renamed and expanded function
static int run_hlo_test(void)
{
    static const char plugin_path[] = "./pjrt_c_api_cpu_plugin.so";
    static const char hlo_program_path[] = "./add.3x2.xla.pb"; // Use the provided path

    pjrt_init init_fn;
    const PJRT_Api* api = NULL;
    void *handle = NULL;
    PJRT_Client* client = NULL;
    PJRT_LoadedExecutable* loaded_executable = NULL; // Revert to original type based on compile warning
    struct file_data hlo_data = {NULL, 0};
    struct file_data compile_options_data = {NULL, 0}; // Buffer for compile options proto
    int rc = 1; // Default to failure

    handle = dlopen(plugin_path, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "Error loading plugin '%s': %s\n", plugin_path, dlerror());
        goto cleanup;
    }

    init_fn = (pjrt_init)dlsym(handle, "GetPjrtApi");
    if (!init_fn) {
        fprintf(stderr, "Error finding symbol 'GetPjrtApi' in %s\n", plugin_path);
        goto cleanup;
    }
    api = init_fn();
    if (api == NULL) {
        fprintf(stderr, "Error: GetPjrtApi returned NULL\n");
        goto cleanup;
    }
    fprintf(stderr, "Loaded PJRT Plugin: %s\n", plugin_path);
    fprintf(stderr, "Reported PJRT API Version: %d.%d\n", api->pjrt_api_version.major_version,
            api->pjrt_api_version.minor_version);

    // Basic API struct size check
    if (api->struct_size < PJRT_Api_STRUCT_SIZE) {
         fprintf(stderr, "Error: Loaded PJRT_Api struct size (%zu) is smaller than expected (%d).\n",
                 api->struct_size, PJRT_Api_STRUCT_SIZE);
         goto cleanup;
    }


    {
        PJRT_Error* error = NULL;
        PJRT_Plugin_Initialize_Args args = {0};
        args.struct_size = PJRT_Plugin_Initialize_Args_STRUCT_SIZE;
        args.extension_start = NULL;
        error = api->PJRT_Plugin_Initialize(&args);
        if (handle_error(error, api, "PJRT_Plugin_Initialize")) {
            goto cleanup;
        }
       printf("PJRT Plugin Initialized successfully.\n");
    }

    // Print attributes (optional, but kept from original)
    print_plugin_attributes(api);

    // Create Client
    {
        PJRT_Client_Create_Args create_args = {0};
        create_args.struct_size = PJRT_Client_Create_Args_STRUCT_SIZE;
        create_args.extension_start = NULL;
        // create_options: Can be NULL or specify options like kv_store
        create_args.create_options = NULL;
        create_args.num_options = 0;
        create_args.kv_get_callback = NULL;
        create_args.kv_put_callback = NULL;
        create_args.kv_get_user_arg = NULL;
        create_args.kv_put_user_arg = NULL;

        PJRT_Error* error = api->PJRT_Client_Create(&create_args);
         if (handle_error(error, api, "PJRT_Client_Create")) {
            goto cleanup;
        }
        client = create_args.client; // Get the created client
        printf("PJRT Client created successfully.\n");
    }
    // Read HLO program file
    if (read_file_to_buffer(hlo_program_path, &hlo_data) != 0) {
        fprintf(stderr, "Failed to read HLO program file: %s\n", hlo_program_path);
        goto cleanup;
    }
    printf("Read HLO program '%s' (%zu bytes).\n", hlo_program_path, hlo_data.size);

    // Read Compile Options Proto file
    static const char compile_options_path[] = "./compile_options.0.pb";
    if (read_file_to_buffer(compile_options_path, &compile_options_data) != 0) {
        fprintf(stderr, "Failed to read compile options file: %s\n", compile_options_path);
        // Treat as fatal as it likely contains required info
        goto cleanup;
    }
    printf("Read compile options proto '%s' (%zu bytes).\n", compile_options_path, compile_options_data.size);


    // Compile HLO program
    {
        PJRT_Program program = {0};
        program.struct_size = PJRT_Program_STRUCT_SIZE;
        program.extension_start = NULL;
        program.format = "hlo"; // Specify format if known, or let PJRT infer
        program.format_size = strlen(program.format);
        program.code = hlo_data.data;
        program.code_size = hlo_data.size;

        PJRT_Client_Compile_Args compile_args = {0};
        compile_args.struct_size = PJRT_Client_Compile_Args_STRUCT_SIZE;
        compile_args.extension_start = NULL;
        compile_args.client = client;
        compile_args.program = &program;
        compile_args.compile_options = compile_options_data.data; // Pass buffer data
        compile_args.compile_options_size = compile_options_data.size; // Pass buffer size

        PJRT_Error* error = api->PJRT_Client_Compile(&compile_args);
        if (handle_error(error, api, "PJRT_Client_Compile")) {
            goto cleanup; // Error during compilation
        }
        loaded_executable = compile_args.executable; // Assign to correct variable type (PJRT_LoadedExecutable*)
        printf("PJRT_Client_Compile successful.\n");

        // --- Get and print executable properties ---
        if (loaded_executable != NULL) { // Check the correct variable
            printf("Attempting to get executable properties...\n");

            // Example: Get serialized executable size
            // Using PJRT_Executable_Serialize function/args and the correct size member.

            // Get PJRT_Executable* from PJRT_LoadedExecutable*
            PJRT_LoadedExecutable_GetExecutable_Args get_exec_args = {0};
            // Use the defined struct size macro
            get_exec_args.struct_size = PJRT_LoadedExecutable_GetExecutable_Args_STRUCT_SIZE;
            get_exec_args.extension_start = NULL;
            get_exec_args.loaded_executable = loaded_executable;
            PJRT_Executable* executable_from_loaded = NULL;
            get_exec_args.executable = executable_from_loaded; // Output parameter

            PJRT_Error* get_exec_error = api->PJRT_LoadedExecutable_GetExecutable(&get_exec_args);
            if (handle_error(get_exec_error, api, "PJRT_LoadedExecutable_GetExecutable")) {
                fprintf(stderr, "Could not get PJRT_Executable from PJRT_LoadedExecutable.\n");
                // Continue cleanup even if this fails
            } else {
                executable_from_loaded = get_exec_args.executable; // Get the obtained executable

                // Declare and initialize serialize_args here, closer to its use
                PJRT_Executable_Serialize_Args serialize_args = {0};
                // Use the defined struct size macro
                serialize_args.struct_size = PJRT_Executable_Serialize_Args_STRUCT_SIZE;
                serialize_args.extension_start = NULL;

                // Pass the obtained PJRT_Executable* to the serialize function
                serialize_args.executable = executable_from_loaded;

                PJRT_Error* serialize_error = api->PJRT_Executable_Serialize(&serialize_args);
                if (handle_error(serialize_error, api, "PJRT_Executable_Serialize")) {
                    fprintf(stderr, "Could not serialize the executable to get its size.\n");
                    // Continue cleanup even if serialization fails
                } else {
                    // Use the correct size member 'serialized_bytes_size' found in pjrt_c_api.h
                    printf("Serialized executable size: %zu bytes\n", serialize_args.serialized_bytes_size);

                    // IMPORTANT: Call the deleter provided by the Serialize function to free
                    // the memory backing the serialized data, as specified in pjrt_c_api.h.
                    if (serialize_args.serialized_executable_deleter != NULL &&
                        serialize_args.serialized_executable != NULL) {
                        printf("Calling serialized executable deleter.\n");
                        serialize_args.serialized_executable_deleter(serialize_args.serialized_executable);
                    } else {
                         fprintf(stderr, "Warning: Serialized executable deleter or data is NULL.\n");
                    }
                }
                // Note: The PJRT_Executable* obtained from PJRT_LoadedExecutable_GetExecutable
                // is likely owned by the PJRT_LoadedExecutable and does not need separate destruction.
                // If issues arise, consult the PJRT C API documentation for ownership rules.
            }
        } else {
            fprintf(stderr, "Executable is NULL after compile, cannot get properties.\n");
        }
        // --- End of getting executable properties ---

    } // Closing brace for the block after PJRT_Client_Compile

    rc = 0; // Mark as success

cleanup:
    // Free file buffers regardless of success/failure
    printf("Freeing HLO program buffer.\n");
    free_file_data(&hlo_data);
    printf("Freeing compile options buffer.\n");
    free_file_data(&compile_options_data);

    // Destroy loaded executable if it exists
    if (loaded_executable != NULL && api != NULL) { // Use correct variable name
        printf("Destroying loaded executable.\n");
        PJRT_LoadedExecutable_Destroy_Args destroy_exec_args = {0}; // Match variable type
        // Use the defined struct size macro
        destroy_exec_args.struct_size = PJRT_LoadedExecutable_Destroy_Args_STRUCT_SIZE;
        destroy_exec_args.extension_start = NULL;
        destroy_exec_args.executable = loaded_executable; // Use correct variable
        PJRT_Error* destroy_exec_err = api->PJRT_LoadedExecutable_Destroy(&destroy_exec_args); // Match variable type
        handle_error(destroy_exec_err, api, "PJRT_LoadedExecutable_Destroy"); // Report error but continue cleanup
    }

    // Destroy client if it exists
    if (client != NULL && api != NULL) {
        printf("Destroying client.\n");
        PJRT_Client_Destroy_Args destroy_client_args = {0};
        destroy_client_args.struct_size = PJRT_Client_Destroy_Args_STRUCT_SIZE;
        destroy_client_args.extension_start = NULL;
        destroy_client_args.client = client;
        PJRT_Error* destroy_client_err = api->PJRT_Client_Destroy(&destroy_client_args);
        handle_error(destroy_client_err, api, "PJRT_Client_Destroy"); // Report error but continue cleanup
    }

    // Close plugin handle
    if (handle != NULL) {
        printf("Closing plugin handle.\n");
        close_plugin(handle, plugin_path, NULL); // Use the correct variable name
    }

    return rc; // Return 0 for success, 1 for failure
} // Closing brace for run_hlo_test

int main(int argc, const char **argv)
{
    // Call the refactored function
    int rc = run_hlo_test();

    // Keep original main content if any, or just return rc
    (void)argc; // Suppress unused parameter warning
    (void)argv; // Suppress unused parameter warning
    if (rc == 0) {
        printf("hlo_test completed successfully.\n");
    } else {
        fprintf(stderr, "hlo_test failed.\n");
    }
    return rc;
} // Closing brace for main