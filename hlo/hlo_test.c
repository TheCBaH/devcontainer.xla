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

// Helper function to create a buffer from host data
static PJRT_Buffer* create_buffer_from_host(const PJRT_Api* api, PJRT_Client* client, PJRT_Device* device,
                                            void* host_data, PJRT_Buffer_Type type,
                                            const int64_t* dims, size_t num_dims,
                                            const char* context_prefix) {
    PJRT_Client_BufferFromHostBuffer_Args create_buf_args = {0};
    create_buf_args.struct_size = PJRT_Client_BufferFromHostBuffer_Args_STRUCT_SIZE;
    create_buf_args.extension_start = NULL;
    create_buf_args.client = client;
    create_buf_args.data = host_data;
    create_buf_args.type = type;
    create_buf_args.dims = dims;
    create_buf_args.num_dims = num_dims;
    create_buf_args.byte_strides = NULL; // Let PJRT calculate strides (row-major)
    create_buf_args.num_byte_strides = 0;
    create_buf_args.device_layout = NULL; // Use default layout
    // create_buf_args.device_layout_size = 0; // Field does not exist
    create_buf_args.host_buffer_semantics = PJRT_HostBufferSemantics_kImmutableOnlyDuringCall;
    create_buf_args.device = device;
    create_buf_args.memory = NULL; // Use default memory for the device

    PJRT_Error* create_buf_error = api->PJRT_Client_BufferFromHostBuffer(&create_buf_args);
    char error_context[100];
    snprintf(error_context, sizeof(error_context), "%s: PJRT_Client_BufferFromHostBuffer", context_prefix);
    if (handle_error(create_buf_error, api, error_context)) {
        return NULL; // Error creating buffer
    }
    printf("%s: Buffer created successfully.\n", context_prefix);
    return create_buf_args.buffer;
}

// Helper function to print a 2D float buffer
static void print_float_buffer(float* data, int rows, int cols) {
    printf("Buffer Contents (%dx%d):\n", rows, cols);
    for (int i = 0; i < rows; ++i) {
        printf("  [");
        for (int j = 0; j < cols; ++j) {
            printf("%f%s", data[i * cols + j], (j == cols - 1) ? "" : ", ");
        }
        printf("]\n");
    }
}


// Forward declaration for the new execute function
static int execute_hlo_program(const PJRT_Api* api, PJRT_Client* client,
                               PJRT_LoadedExecutable* executable,
                               PJRT_Buffer** input_buffers, size_t num_inputs,
                               PJRT_Buffer*** output_buffers, size_t* num_outputs);

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
    PJRT_Buffer** input_buffers = NULL; // Placeholder for input buffers
    PJRT_Buffer** output_buffers = NULL; // Placeholder for output buffers
    size_t num_outputs = 0; // Placeholder for number of outputs
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


    // --- Get Addressable Devices ---
    PJRT_Device* const* addressable_devices = NULL; // Match API type
    size_t num_addressable_devices = 0;
    PJRT_Device* target_device = NULL; // Device to run on
    {
        PJRT_Client_AddressableDevices_Args devices_args = {0};
        devices_args.struct_size = PJRT_Client_AddressableDevices_Args_STRUCT_SIZE;
        devices_args.extension_start = NULL;
        devices_args.client = client;

        PJRT_Error* devices_error = api->PJRT_Client_AddressableDevices(&devices_args);
        if (handle_error(devices_error, api, "PJRT_Client_AddressableDevices")) {
            goto cleanup;
        }
        addressable_devices = devices_args.addressable_devices; // Get the list
        num_addressable_devices = devices_args.num_addressable_devices;
        printf("Found %zu addressable devices.\n", num_addressable_devices);

        if (num_addressable_devices == 0) {
            fprintf(stderr, "Error: No addressable devices found.\n");
            goto cleanup;
        }
        target_device = addressable_devices[0]; // Use the first device
        printf("Using device 0 for execution.\n");
        // Note: addressable_devices points to internal client memory, no need to free here.
    }


    // --- Create Input Buffers ---
    const size_t num_inputs = 2; // The 'add' program takes two inputs
    input_buffers = (PJRT_Buffer**)malloc(num_inputs * sizeof(PJRT_Buffer*));
    if (input_buffers == NULL) {
        fprintf(stderr, "Failed to allocate memory for input buffer array.\n");
        goto cleanup;
    }
    for(size_t i=0; i<num_inputs; ++i) input_buffers[i] = NULL; // Initialize

    float input_data_1[3][2] = {{1.0f, 2.0f}, {3.0f, 4.0f}, {5.0f, 6.0f}};
    float input_data_2[3][2] = {{10.0f, 20.0f}, {30.0f, 40.0f}, {50.0f, 60.0f}};
    int64_t dims[2] = {3, 2}; // Dimensions for 3x2 matrix
    size_t num_dims = 2;

    input_buffers[0] = create_buffer_from_host(api, client, target_device, input_data_1,
                                               PJRT_Buffer_Type_F32, dims, num_dims, "Input 1");
    if (input_buffers[0] == NULL) goto cleanup;

    input_buffers[1] = create_buffer_from_host(api, client, target_device, input_data_2,
                                               PJRT_Buffer_Type_F32, dims, num_dims, "Input 2");
    if (input_buffers[1] == NULL) goto cleanup;


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
            goto cleanup; // Cannot proceed without executable
        }
        // --- End of getting executable properties ---

        // --- Execute the program ---
        if (loaded_executable != NULL) {
             printf("Executing the compiled program...\n");

             if (execute_hlo_program(api, client, loaded_executable,
                                     input_buffers, num_inputs,
                                     &output_buffers, &num_outputs) != 0) {
                 fprintf(stderr, "Failed to execute HLO program.\n");
                 goto cleanup;
             }
             printf("Execution successful. Received %zu output buffer(s).\n", num_outputs);

             // --- Process Output Buffers ---
             if (num_outputs > 0 && output_buffers != NULL && output_buffers[0] != NULL) {
                 printf("Processing output buffer 0...\n");
                 PJRT_Buffer* out_buf = output_buffers[0];

                 // Get output buffer properties (assuming it's also F32 3x2)
                 PJRT_Buffer_Dimensions_Args dim_args = {0};
                 dim_args.struct_size = PJRT_Buffer_Dimensions_Args_STRUCT_SIZE;
                 dim_args.buffer = out_buf;
                 PJRT_Error* dim_err = api->PJRT_Buffer_Dimensions(&dim_args);
                 if (handle_error(dim_err, api, "PJRT_Buffer_Dimensions (output)")) {
                     // Don't goto cleanup, just skip processing this buffer
                 } else {
                     printf("Output buffer dimensions: %zu\n", dim_args.num_dims);
                     if (dim_args.num_dims == 2) { // Expecting 2D
                         size_t rows = dim_args.dims[0];
                         size_t cols = dim_args.dims[1];
                         size_t total_elements = rows * cols;
                         size_t expected_byte_size = total_elements * sizeof(float); // Assuming F32

                         // Allocate host buffer for output
                         float* host_output_data = (float*)malloc(expected_byte_size);
                         if (host_output_data == NULL) {
                             fprintf(stderr, "Failed to allocate host memory for output buffer.\n");
                         } else {
                             // Copy data from device to host (assuming synchronous for now)
                             PJRT_Buffer_ToHostBuffer_Args to_host_args = {0};
                             to_host_args.struct_size = PJRT_Buffer_ToHostBuffer_Args_STRUCT_SIZE;
                             to_host_args.src = out_buf;
                             to_host_args.dst = host_output_data;
                             to_host_args.dst_size = expected_byte_size;
                             // Removed event handling as members/functions seem missing in this API version
    
                             PJRT_Error* to_host_error = api->PJRT_Buffer_ToHostBuffer(&to_host_args);
    
                             // Assume synchronous completion or handle error
                             if (handle_error(to_host_error, api, "PJRT_Buffer_ToHostBuffer")) {
                                 fprintf(stderr, "Failed to copy output buffer to host.\n");
                             } else {
                                 printf("Output buffer copied to host successfully.\n");
                                 print_float_buffer(host_output_data, rows, cols);
                             }
                             free(host_output_data); // Free host buffer
                         }
                     } else {
                         fprintf(stderr, "Unexpected output buffer dimensions: %zu\n", dim_args.num_dims);
                     }
                 }
             } else if (num_outputs > 0) {
                  fprintf(stderr, "Output buffer list exists, but buffer 0 is NULL.\n");
             } else {
                  printf("No output buffers to process.\n");
             }
             // --- End of processing output buffers ---

        } else {
             fprintf(stderr, "Executable is NULL, cannot execute.\n");
             goto cleanup;
        }
        // --- End of execution ---


    } // Closing brace for the block after PJRT_Client_Compile

    rc = 0; // Mark as success

cleanup:
    // Free file buffers regardless of success/failure
    printf("Freeing HLO program buffer.\n");
    free_file_data(&hlo_data);
    printf("Freeing compile options buffer.\n");
    free_file_data(&compile_options_data);

    // Destroy output buffers if they were allocated
    if (output_buffers != NULL && api != NULL) {
        printf("Destroying output buffers.\n");
        for (size_t i = 0; i < num_outputs; ++i) {
            if (output_buffers[i] != NULL) {
                PJRT_Buffer_Destroy_Args destroy_buf_args = {0};
                destroy_buf_args.struct_size = PJRT_Buffer_Destroy_Args_STRUCT_SIZE;
                destroy_buf_args.extension_start = NULL;
                destroy_buf_args.buffer = output_buffers[i];
                PJRT_Error* destroy_buf_err = api->PJRT_Buffer_Destroy(&destroy_buf_args);
                // Handle error, but continue cleanup
                handle_error(destroy_buf_err, api, "PJRT_Buffer_Destroy (output)");
            }
        }
        free(output_buffers); // Free the array holding the buffer pointers
    }
     // Destroy input buffers
     if (input_buffers != NULL && api != NULL) {
         printf("Destroying input buffers.\n");
         for (size_t i = 0; i < num_inputs; ++i) { // Use the actual number of inputs created
             if (input_buffers[i] != NULL) {
                 PJRT_Buffer_Destroy_Args destroy_buf_args = {0};
                 destroy_buf_args.struct_size = PJRT_Buffer_Destroy_Args_STRUCT_SIZE;
                 destroy_buf_args.extension_start = NULL;
                 destroy_buf_args.buffer = input_buffers[i];
                 PJRT_Error* destroy_buf_err = api->PJRT_Buffer_Destroy(&destroy_buf_args);
                 // Handle error, but continue cleanup
                 handle_error(destroy_buf_err, api, "PJRT_Buffer_Destroy (input)");
             }
         }
         free(input_buffers); // Free the array holding the buffer pointers
     }

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


// --- New function to execute the HLO program ---
// Simplified version focusing on single device, single output list for now.
// Assumes caller manages input/output buffer creation/destruction.
static int execute_hlo_program(const PJRT_Api* api, PJRT_Client* client,
                               PJRT_LoadedExecutable* executable,
                               PJRT_Buffer** input_buffers, size_t num_inputs,
                               PJRT_Buffer*** output_buffers_ptr, size_t* num_outputs_ptr) {
    (void)client; // Mark client as unused for now
    printf("Preparing arguments for PJRT_LoadedExecutable_Execute...\n");

    // --- 1. Prepare Execute Options ---
    PJRT_ExecuteOptions options = {0};
    options.struct_size = PJRT_ExecuteOptions_STRUCT_SIZE;
    options.extension_start = NULL;
    options.launch_id = 0; // Example launch ID
    // Set other options as needed, e.g., options.strict_shape_checking = true;

    // --- 2. Prepare Argument Lists ---
    // For this basic test, assume execution on a single device (device 0).
    // Therefore, we need one list of input buffers.
    PJRT_Buffer** argument_list = input_buffers; // Direct use for single list
    size_t num_argument_lists = 1;

    // --- 3. Prepare Output Buffers ---
    // We need to know how many outputs the executable produces per device.
    // Get the underlying PJRT_Executable first.
    PJRT_LoadedExecutable_GetExecutable_Args get_exec_args = {0};
    get_exec_args.struct_size = PJRT_LoadedExecutable_GetExecutable_Args_STRUCT_SIZE;
    get_exec_args.loaded_executable = executable;
    PJRT_Error* get_exec_error = api->PJRT_LoadedExecutable_GetExecutable(&get_exec_args);
    if (handle_error(get_exec_error, api, "execute_hlo_program: PJRT_LoadedExecutable_GetExecutable")) {
        return 1;
    }
    PJRT_Executable* base_executable = get_exec_args.executable;
    if (base_executable == NULL) {
         fprintf(stderr, "execute_hlo_program: Failed to get base PJRT_Executable.\n");
         return 1;
    }

    // Now query the PJRT_Executable for output arity.
    PJRT_Executable_NumOutputs_Args num_outputs_args = {0};
    num_outputs_args.struct_size = PJRT_Executable_NumOutputs_Args_STRUCT_SIZE;
    num_outputs_args.extension_start = NULL;
    num_outputs_args.executable = base_executable; // Use the base executable
    PJRT_Error* num_outputs_error = api->PJRT_Executable_NumOutputs(&num_outputs_args);
     if (handle_error(num_outputs_error, api, "PJRT_Executable_NumOutputs")) {
        return 1; // Failed to get number of outputs
    }
    size_t num_outputs_per_device = num_outputs_args.num_outputs;
    printf("Executable has %zu output(s) per device.\n", num_outputs_per_device);

    if (num_outputs_per_device == 0) {
        printf("Executable has no outputs.\n");
        *output_buffers_ptr = NULL;
        *num_outputs_ptr = 0;
        // Execution might still be valid (e.g., for side effects), proceed.
    }

    // Allocate space for the output buffer pointers for the single device.
    // The PJRT API will fill this array.
    PJRT_Buffer** output_list = NULL;
     if (num_outputs_per_device > 0) {
        output_list = (PJRT_Buffer**)malloc(num_outputs_per_device * sizeof(PJRT_Buffer*));
        if (output_list == NULL) {
            fprintf(stderr, "Failed to allocate memory for output buffer list.\n");
            return 1;
        }
        // Initialize to NULL (important for cleanup)
        for (size_t i = 0; i < num_outputs_per_device; ++i) {
            output_list[i] = NULL;
        }
    }


    // We have one list of outputs (for the single device).
    PJRT_Buffer** output_lists_array[] = { output_list }; // Array containing the single output list
    // size_t num_output_lists = 1; // Unused variable


    // --- 4. Prepare Execute Arguments ---
    PJRT_LoadedExecutable_Execute_Args execute_args = {0};
    execute_args.struct_size = PJRT_LoadedExecutable_Execute_Args_STRUCT_SIZE;
    execute_args.extension_start = NULL; // Add extensions if needed (e.g., profiler)
    execute_args.executable = executable;
    execute_args.options = &options;
    execute_args.num_devices = num_argument_lists; // Executing on 1 device 'group'
    execute_args.num_args = num_inputs;
    // Cast to expected type: PJRT_Buffer* const* const*
    execute_args.argument_lists = (PJRT_Buffer* const* const*)&argument_list;
    execute_args.output_lists = output_lists_array; // Pointer to the array holding the output list(s)
    execute_args.execute_device = NULL; // Let PJRT manage device placement for multi-device execution
                                        // For single-device, could specify the device.
    execute_args.device_complete_events = NULL; // Not requesting completion events for now

    // --- 5. Execute ---
    printf("Calling PJRT_LoadedExecutable_Execute...\n");
    PJRT_Error* execute_error = api->PJRT_LoadedExecutable_Execute(&execute_args);

    // --- 6. Handle Errors and Outputs ---
    if (handle_error(execute_error, api, "PJRT_LoadedExecutable_Execute")) {
        // Cleanup allocated output list memory even if execute failed
        if (output_list != NULL) {
             // Important: Destroy any buffers PJRT *might* have created before the error
            for (size_t i = 0; i < num_outputs_per_device; ++i) {
                 if (output_list[i] != NULL) {
                    PJRT_Buffer_Destroy_Args destroy_buf_args = {0};
                    destroy_buf_args.struct_size = PJRT_Buffer_Destroy_Args_STRUCT_SIZE;
                    destroy_buf_args.buffer = output_list[i];
                    PJRT_Error* destroy_err = api->PJRT_Buffer_Destroy(&destroy_buf_args);
                    handle_error(destroy_err, api, "PJRT_Buffer_Destroy (error cleanup)");
                 }
            }
            free(output_list);
        }
        return 1; // Execution failed
    }

    printf("PJRT_LoadedExecutable_Execute call successful.\n");

    // Pass the ownership of the output list back to the caller
    *output_buffers_ptr = output_list;
    *num_outputs_ptr = num_outputs_per_device;

    return 0; // Success
}