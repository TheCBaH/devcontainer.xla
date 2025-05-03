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

// --- Test Case Definition ---
typedef struct {
    const char* name;
    const char* hlo_path;
    const char* compile_options_path;
    size_t num_inputs;
    void** input_data; // Array of pointers to host data arrays
    int64_t** input_dims; // Array of pointers to dimension arrays
    size_t* input_num_dims; // Array of number of dimensions per input
    PJRT_Buffer_Type* input_types; // Array of buffer types per input
    // TODO: Add fields for expected output verification if needed
} TestCase;


// --- Forward Declarations ---
static int handle_error(PJRT_Error* error, const PJRT_Api* api, const char* context);
static void print_plugin_attributes(const PJRT_Api* api);
static int close_plugin(void* handle, const char* plugin, const char* message);
static int read_file_to_buffer(const char* filename, struct file_data* file_data);
static void free_file_data(struct file_data* file_data);
static PJRT_Buffer* create_buffer_from_host(const PJRT_Api* api, PJRT_Client* client, PJRT_Device* device,
                                            void* host_data, PJRT_Buffer_Type type,
                                            const int64_t* dims, size_t num_dims,
                                            const char* context_prefix);
static void print_float_buffer(float* data, const int64_t* dims, size_t num_dims); // Updated signature
static int execute_hlo_program(const PJRT_Api* api, PJRT_LoadedExecutable* executable,
                               PJRT_Buffer** input_buffers, size_t num_inputs,
                               PJRT_Buffer*** output_buffers_ptr, size_t* num_outputs_ptr);
static int run_computation_test(const PJRT_Api* api, PJRT_Client* client, PJRT_Device* device,
                                const TestCase* test_case);


// --- Helper function to handle PJRT errors ---
// (handle_error function remains the same)
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
// (print_plugin_attributes function remains the same)
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


// --- Function to close plugin ---
// (close_plugin function remains the same)
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


// --- Function to read a file into a buffer ---
// (read_file_to_buffer function remains the same)
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


// --- Function to free file data ---
// (free_file_data function remains the same)
static void free_file_data(struct file_data* file_data) {
    if (file_data->data != NULL) {
        free(file_data->data);
        file_data->data = NULL;
        file_data->size = 0;
    }
}


// --- Helper function to create a buffer from host data ---
// (create_buffer_from_host function remains the same)
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


// --- Helper function to print a float buffer ---
// Updated to handle generic dimensions
static void print_float_buffer(float* data, const int64_t* dims, size_t num_dims) {
    if (num_dims == 2) {
        int rows = dims[0];
        int cols = dims[1];
        printf("Buffer Contents (%dx%d):\n", rows, cols);
        for (int i = 0; i < rows; ++i) {
            printf("  [");
            for (int j = 0; j < cols; ++j) {
                printf("%f%s", data[i * cols + j], (j == cols - 1) ? "" : ", ");
            }
            printf("]\n");
        }
    } else {
        // Basic print for other dimensions
        printf("Buffer Contents (Num Dims: %zu, First Dim: %ld, ...):\n  [", num_dims, dims[0]); // Use %ld for int64_t (long int)
        size_t total_elements = 1;
        for(size_t i=0; i<num_dims; ++i) total_elements *= dims[i];
        size_t print_limit = total_elements < 10 ? total_elements : 10; // Print first few elements
        for(size_t i=0; i<print_limit; ++i) {
             printf("%f%s", data[i], (i == print_limit - 1) ? "" : ", ");
        }
        if (print_limit < total_elements) printf("...");
        printf("]\n");
    }
}


// --- Function to execute the HLO program ---
// Removed client parameter as it's not used here
static int execute_hlo_program(const PJRT_Api* api, PJRT_LoadedExecutable* executable,
                               PJRT_Buffer** input_buffers, size_t num_inputs,
                               PJRT_Buffer*** output_buffers_ptr, size_t* num_outputs_ptr) {
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


// --- Function to run a specific computation test case ---
static int run_computation_test(const PJRT_Api* api, PJRT_Client* client, PJRT_Device* device,
                                const TestCase* test_case) {
    printf("\n--- Running Test Case: %s ---\n", test_case->name);
    int rc = 1; // Default to failure
    struct file_data hlo_data = {NULL, 0};
    struct file_data compile_options_data = {NULL, 0};
    PJRT_LoadedExecutable* loaded_executable = NULL;
    PJRT_Buffer** input_buffers = NULL;
    PJRT_Buffer** output_buffers = NULL;
    size_t num_outputs = 0;

    // --- Read Files ---
    if (read_file_to_buffer(test_case->hlo_path, &hlo_data) != 0) {
        fprintf(stderr, "Failed to read HLO program file: %s\n", test_case->hlo_path);
        goto cleanup_test;
    }
    printf("Read HLO program '%s' (%zu bytes).\n", test_case->hlo_path, hlo_data.size);

    if (read_file_to_buffer(test_case->compile_options_path, &compile_options_data) != 0) {
        fprintf(stderr, "Failed to read compile options file: %s\n", test_case->compile_options_path);
        goto cleanup_test;
    }
    printf("Read compile options proto '%s' (%zu bytes).\n", test_case->compile_options_path, compile_options_data.size);

    // --- Create Input Buffers ---
    input_buffers = (PJRT_Buffer**)malloc(test_case->num_inputs * sizeof(PJRT_Buffer*));
    if (input_buffers == NULL) {
        fprintf(stderr, "Failed to allocate memory for input buffer array.\n");
        goto cleanup_test;
    }
    for(size_t i=0; i<test_case->num_inputs; ++i) input_buffers[i] = NULL; // Initialize

    for (size_t i = 0; i < test_case->num_inputs; ++i) {
        char context[50];
        snprintf(context, sizeof(context), "Input %zu", i);
        input_buffers[i] = create_buffer_from_host(api, client, device,
                                                   test_case->input_data[i],
                                                   test_case->input_types[i],
                                                   test_case->input_dims[i],
                                                   test_case->input_num_dims[i],
                                                   context);
        if (input_buffers[i] == NULL) goto cleanup_test;

        // Print input buffer
        printf("--- %s Data ---\n", context);
        // Assuming F32 for now, might need type switching later
        if (test_case->input_types[i] == PJRT_Buffer_Type_F32) {
             print_float_buffer((float*)test_case->input_data[i], test_case->input_dims[i], test_case->input_num_dims[i]);
        } else {
             printf("  (Printing not implemented for this type)\n");
        }
        printf("-------------------\n");
    }


    // --- Compile HLO program ---
    {
        PJRT_Program program = {0};
        program.struct_size = PJRT_Program_STRUCT_SIZE;
        program.extension_start = NULL;
        program.format = "hlo"; // Assuming HLO format
        program.format_size = strlen(program.format);
        program.code = hlo_data.data;
        program.code_size = hlo_data.size;

        PJRT_Client_Compile_Args compile_args = {0};
        compile_args.struct_size = PJRT_Client_Compile_Args_STRUCT_SIZE;
        compile_args.extension_start = NULL;
        compile_args.client = client;
        compile_args.program = &program;
        compile_args.compile_options = compile_options_data.data;
        compile_args.compile_options_size = compile_options_data.size;

        PJRT_Error* error = api->PJRT_Client_Compile(&compile_args);
        if (handle_error(error, api, "PJRT_Client_Compile")) {
            goto cleanup_test;
        }
        loaded_executable = compile_args.executable;
        printf("PJRT_Client_Compile successful.\n");
    }

    // --- Execute the program ---
    if (loaded_executable != NULL) {
         printf("Executing the compiled program...\n");

         if (execute_hlo_program(api, loaded_executable,
                                 input_buffers, test_case->num_inputs,
                                 &output_buffers, &num_outputs) != 0) {
             fprintf(stderr, "Failed to execute HLO program.\n");
             goto cleanup_test;
         }
         printf("Execution successful. Received %zu output buffer(s).\n", num_outputs);

         // --- Process Output Buffers ---
         if (num_outputs > 0 && output_buffers != NULL && output_buffers[0] != NULL) {
             printf("Processing output buffer 0...\n");
             PJRT_Buffer* out_buf = output_buffers[0];

             PJRT_Buffer_Dimensions_Args dim_args = {0};
             dim_args.struct_size = PJRT_Buffer_Dimensions_Args_STRUCT_SIZE;
             dim_args.buffer = out_buf;
             PJRT_Error* dim_err = api->PJRT_Buffer_Dimensions(&dim_args);
             if (handle_error(dim_err, api, "PJRT_Buffer_Dimensions (output)")) {
                 // Error getting dimensions, cannot proceed with copy/print
             } else {
                 printf("Output buffer dimensions: %zu\n", dim_args.num_dims);
                 // Calculate size based on dimensions and type (assuming F32 for now)
                 size_t total_elements = 1;
                 for(size_t i=0; i<dim_args.num_dims; ++i) total_elements *= dim_args.dims[i];
                 // TODO: Get buffer type instead of assuming F32
                 size_t expected_byte_size = total_elements * sizeof(float);

                 float* host_output_data = (float*)malloc(expected_byte_size);
                 if (host_output_data == NULL) {
                     fprintf(stderr, "Failed to allocate host memory for output buffer.\n");
                 } else {
                     PJRT_Buffer_ToHostBuffer_Args to_host_args = {0};
                     to_host_args.struct_size = PJRT_Buffer_ToHostBuffer_Args_STRUCT_SIZE;
                     to_host_args.src = out_buf;
                     to_host_args.dst = host_output_data;
                     to_host_args.dst_size = expected_byte_size;

                     PJRT_Error* to_host_error = api->PJRT_Buffer_ToHostBuffer(&to_host_args);

                     if (handle_error(to_host_error, api, "PJRT_Buffer_ToHostBuffer")) {
                         fprintf(stderr, "Failed to copy output buffer to host.\n");
                     } else {
                         printf("Output buffer copied to host successfully.\n");
                         // Assuming F32 for printing
                         print_float_buffer(host_output_data, dim_args.dims, dim_args.num_dims);
                     }
                     free(host_output_data);
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
         goto cleanup_test;
    }
    // --- End of execution ---

    rc = 0; // Mark test as success

cleanup_test:
    printf("Cleaning up resources for test case: %s\n", test_case->name);
    // Destroy output buffers
    if (output_buffers != NULL && api != NULL) {
        printf("Destroying output buffers.\n");
        for (size_t i = 0; i < num_outputs; ++i) {
            if (output_buffers[i] != NULL) {
                PJRT_Buffer_Destroy_Args destroy_buf_args = {0};
                destroy_buf_args.struct_size = PJRT_Buffer_Destroy_Args_STRUCT_SIZE;
                destroy_buf_args.buffer = output_buffers[i];
                PJRT_Error* destroy_buf_err = api->PJRT_Buffer_Destroy(&destroy_buf_args);
                handle_error(destroy_buf_err, api, "PJRT_Buffer_Destroy (output)");
            }
        }
        free(output_buffers);
    }
    // Destroy input buffers
     if (input_buffers != NULL && api != NULL) {
         printf("Destroying input buffers.\n");
         for (size_t i = 0; i < test_case->num_inputs; ++i) {
             if (input_buffers[i] != NULL) {
                 PJRT_Buffer_Destroy_Args destroy_buf_args = {0};
                 destroy_buf_args.struct_size = PJRT_Buffer_Destroy_Args_STRUCT_SIZE;
                 destroy_buf_args.buffer = input_buffers[i];
                 PJRT_Error* destroy_buf_err = api->PJRT_Buffer_Destroy(&destroy_buf_args);
                 handle_error(destroy_buf_err, api, "PJRT_Buffer_Destroy (input)");
             }
         }
         free(input_buffers);
     }
    // Destroy loaded executable
    if (loaded_executable != NULL && api != NULL) {
        printf("Destroying loaded executable.\n");
        PJRT_LoadedExecutable_Destroy_Args destroy_exec_args = {0};
        destroy_exec_args.struct_size = PJRT_LoadedExecutable_Destroy_Args_STRUCT_SIZE;
        destroy_exec_args.executable = loaded_executable;
        PJRT_Error* destroy_exec_err = api->PJRT_LoadedExecutable_Destroy(&destroy_exec_args);
        handle_error(destroy_exec_err, api, "PJRT_LoadedExecutable_Destroy");
    }
    // Free file buffers
    free_file_data(&hlo_data);
    free_file_data(&compile_options_data);

    printf("--- Finished Test Case: %s (Result: %s) ---\n", test_case->name, rc == 0 ? "SUCCESS" : "FAILURE");
    return rc;
}


// --- Main Function ---
int main(int argc, const char **argv)
{
    (void)argc; // Suppress unused parameter warning
    (void)argv; // Suppress unused parameter warning

    static const char plugin_path[] = "./pjrt_c_api_cpu_plugin.so";
    pjrt_init init_fn;
    const PJRT_Api* api = NULL;
    void *handle = NULL;
    PJRT_Client* client = NULL;
    PJRT_Device* target_device = NULL;
    int overall_rc = 0; // Track overall success/failure

    // --- Plugin Loading and Client Creation ---
    handle = dlopen(plugin_path, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "Error loading plugin '%s': %s\n", plugin_path, dlerror());
        return 1;
    }

    init_fn = (pjrt_init)dlsym(handle, "GetPjrtApi");
    if (!init_fn) {
        fprintf(stderr, "Error finding symbol 'GetPjrtApi' in %s\n", plugin_path);
        close_plugin(handle, plugin_path, NULL);
        return 1;
    }
    api = init_fn();
    if (api == NULL) {
        fprintf(stderr, "Error: GetPjrtApi returned NULL\n");
        close_plugin(handle, plugin_path, NULL);
        return 1;
    }
    fprintf(stderr, "Loaded PJRT Plugin: %s\n", plugin_path);
    fprintf(stderr, "Reported PJRT API Version: %d.%d\n", api->pjrt_api_version.major_version,
            api->pjrt_api_version.minor_version);

    if (api->struct_size < PJRT_Api_STRUCT_SIZE) {
         fprintf(stderr, "Error: Loaded PJRT_Api struct size (%zu) is smaller than expected (%d).\n",
                 api->struct_size, PJRT_Api_STRUCT_SIZE);
         close_plugin(handle, plugin_path, NULL);
         return 1;
    }

    {
        PJRT_Plugin_Initialize_Args args = {0};
        args.struct_size = PJRT_Plugin_Initialize_Args_STRUCT_SIZE;
        PJRT_Error* error = api->PJRT_Plugin_Initialize(&args);
        if (handle_error(error, api, "PJRT_Plugin_Initialize")) {
            close_plugin(handle, plugin_path, NULL);
            return 1;
        }
       printf("PJRT Plugin Initialized successfully.\n");
    }

    print_plugin_attributes(api);

    {
        PJRT_Client_Create_Args create_args = {0};
        create_args.struct_size = PJRT_Client_Create_Args_STRUCT_SIZE;
        PJRT_Error* error = api->PJRT_Client_Create(&create_args);
         if (handle_error(error, api, "PJRT_Client_Create")) {
            close_plugin(handle, plugin_path, NULL);
            return 1;
        }
        client = create_args.client;
        printf("PJRT Client created successfully.\n");
    }

    // --- Get Target Device ---
    {
        PJRT_Client_AddressableDevices_Args devices_args = {0};
        devices_args.struct_size = PJRT_Client_AddressableDevices_Args_STRUCT_SIZE;
        devices_args.client = client;
        PJRT_Error* devices_error = api->PJRT_Client_AddressableDevices(&devices_args);
        if (handle_error(devices_error, api, "PJRT_Client_AddressableDevices")) {
             if (client != NULL) {
                PJRT_Client_Destroy_Args destroy_args = {PJRT_Client_Destroy_Args_STRUCT_SIZE, NULL, client};
                handle_error(api->PJRT_Client_Destroy(&destroy_args), api, "PJRT_Client_Destroy (error path)");
             }
             close_plugin(handle, plugin_path, NULL);
             return 1;
        }
        if (devices_args.num_addressable_devices == 0) {
            fprintf(stderr, "Error: No addressable devices found.\n");
             if (client != NULL) {
                PJRT_Client_Destroy_Args destroy_args = {PJRT_Client_Destroy_Args_STRUCT_SIZE, NULL, client};
                handle_error(api->PJRT_Client_Destroy(&destroy_args), api, "PJRT_Client_Destroy (error path)");
             }
             close_plugin(handle, plugin_path, NULL);
            return 1;
        }
        target_device = devices_args.addressable_devices[0]; // Use the first device
        printf("Using device 0 for execution.\n");
    }


    // --- Define Test Cases ---
    // Test Case 1: Add 3x2
    float add_input_data_1[3][2] = {{1.0f, 2.0f}, {3.0f, 4.0f}, {5.0f, 6.0f}};
    float add_input_data_2[3][2] = {{10.0f, 20.0f}, {30.0f, 40.0f}, {50.0f, 60.0f}};
    void* add_inputs[] = {add_input_data_1, add_input_data_2};
    int64_t add_dims[2] = {3, 2};
    int64_t* add_input_dims[] = {add_dims, add_dims};
    size_t add_num_dims[] = {2, 2};
    PJRT_Buffer_Type add_types[] = {PJRT_Buffer_Type_F32, PJRT_Buffer_Type_F32};
    TestCase add_test = {
        .name = "Add 3x2",
        .hlo_path = "./add.3x2.xla.pb",
        .compile_options_path = "./compile_options.0.pb", // Assuming same options for now
        .num_inputs = 2,
        .input_data = add_inputs,
        .input_dims = add_input_dims,
        .input_num_dims = add_num_dims,
        .input_types = add_types
    };

    // Test Case 2: Identity 2x2
    float identity_input_data[2][2] = {{10.0f, -20.0f}, {35.5f, 0.0f}};
    void* identity_inputs[] = {identity_input_data};
    int64_t identity_dims[2] = {2, 2};
    int64_t* identity_input_dims[] = {identity_dims};
    size_t identity_num_dims[] = {2};
    PJRT_Buffer_Type identity_types[] = {PJRT_Buffer_Type_F32};
     TestCase identity_test = {
        .name = "Identity 2x2",
        .hlo_path = "./Identity.2x2.xla.pb",
        .compile_options_path = "./compile_options.0.pb", // Assuming same options for now
        .num_inputs = 1,
        .input_data = identity_inputs,
        .input_dims = identity_input_dims,
        .input_num_dims = identity_num_dims,
        .input_types = identity_types
    };

    TestCase* all_tests[] = {&add_test, &identity_test};
    size_t num_tests = sizeof(all_tests) / sizeof(all_tests[0]);

    // --- Run Tests ---
    for (size_t i = 0; i < num_tests; ++i) {
        int test_rc = run_computation_test(api, client, target_device, all_tests[i]);
        if (test_rc != 0) {
            overall_rc = 1; // Mark overall failure if any test fails
        }
    }

    // --- Cleanup ---
    if (client != NULL && api != NULL) {
        printf("Destroying client.\n");
        PJRT_Client_Destroy_Args destroy_client_args = {0};
        destroy_client_args.struct_size = PJRT_Client_Destroy_Args_STRUCT_SIZE;
        destroy_client_args.client = client;
        PJRT_Error* destroy_client_err = api->PJRT_Client_Destroy(&destroy_client_args);
        handle_error(destroy_client_err, api, "PJRT_Client_Destroy");
    }

    if (handle != NULL) {
        printf("Closing plugin handle.\n");
        close_plugin(handle, plugin_path, NULL);
    }

    if (overall_rc == 0) {
        printf("\nAll hlo_tests completed successfully.\n");
    } else {
        fprintf(stderr, "\nOne or more hlo_tests failed.\n");
    }
    return overall_rc;
}
