# README

This code is AI-generated and should be used with care. Please review and test thoroughly before using it in production.

## hlo_test.c

This program demonstrates how to use the PJRT C API to load and execute HLO (High Level Optimizer) computations using a CPU plugin (`pjrt_c_api_cpu_plugin.so`).

### Structure

The program is structured as follows:

1.  **`main` function:**
    *   Loads the PJRT CPU plugin (`.so` file) using `dlopen`.
    *   Retrieves the PJRT API function table using `dlsym`.
    *   Initializes the plugin and creates a PJRT client.
    *   Retrieves the first available addressable device.
    *   Defines test cases (`TestCase` structs) for different HLO computations (e.g., "Add 3x2", "Identity 2x2").
    *   Loops through the defined test cases and calls `run_computation_test` for each.
    *   Cleans up the client and unloads the plugin.

2.  **`run_computation_test` function:**
    *   Takes the PJRT API, client, target device, and a `TestCase` struct as input.
    *   Reads the HLO program file (`.pb`) and compile options file specified in the test case.
    *   Creates input `PJRT_Buffer`s on the target device from the host data defined in the test case using `create_buffer_from_host`.
    *   Prints the input buffer data.
    *   Compiles the HLO program using `PJRT_Client_Compile`.
    *   Executes the compiled program using `execute_hlo_program`.
    *   Processes the output buffers: retrieves dimensions, copies data back to the host using `PJRT_Buffer_ToHostBuffer`, and prints the results using `print_float_buffer`.
    *   Cleans up resources specific to the test case (executable, input/output buffers, file data).

3.  **`execute_hlo_program` function:**
    *   Takes the PJRT API, loaded executable, input buffers, and pointers for output buffers/counts.
    *   Prepares the necessary arguments (`PJRT_ExecuteOptions`, `PJRT_LoadedExecutable_Execute_Args`).
    *   Determines the number of expected outputs using `PJRT_Executable_NumOutputs`.
    *   Allocates memory for the output buffer pointers.
    *   Calls the core `PJRT_LoadedExecutable_Execute` function from the PJRT C API.
    *   Handles potential errors and returns the output buffer array and count to the caller.

4.  **Helper Functions:**
    *   `handle_error`: Checks for and prints details of PJRT errors.
    *   `print_plugin_attributes`: Queries and prints attributes of the loaded PJRT plugin.
    *   `close_plugin`: Safely closes the loaded plugin handle.
    *   `read_file_to_buffer`: Reads a binary file into a memory buffer.
    *   `free_file_data`: Frees memory allocated by `read_file_to_buffer`.
    *   `create_buffer_from_host`: Creates a `PJRT_Buffer` on the device from host data.
    *   `print_float_buffer`: Prints the contents of a float buffer (currently supports 2D and basic printing for other ranks).

### Functionality

The program demonstrates a basic workflow for using the PJRT C API:

*   Plugin loading and initialization.
*   Client and device management.
*   Reading HLO programs and compile options from files.
*   Transferring host data to device buffers.
*   Compiling HLO programs.
*   Executing compiled programs.
*   Transferring device results back to host buffers.
*   Resource cleanup.

It is designed to be easily extensible by adding new `TestCase` definitions in the `main` function for different HLO programs and input data.