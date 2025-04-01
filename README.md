<<<<<<< HEAD
# Dirwalk - Directory Traversal Utility

**Dirwalk** is a command-line utility written in C that performs directory traversal, listing files, directories, and symbolic links based on specified options. It supports filtering the output to include only files, directories, or links, and provides an option to sort the output.

---

## Features

- **Directory Traversal**: Recursively traverses directories starting from a specified path (default is the current directory).
- **Filter Output**:
  - List only symbolic links (`-l`).
  - List only directories (`-d`).
  - List only files (`-f`).
- **Sort Output**: Sort the output alphabetically using locale-specific collation (`-s`).
- **Combined Options**: Combine options (e.g., `-ld` to list both links and directories).
- **Error Handling**: Provides meaningful error messages for invalid options or unexpected arguments.

## Building

Build the app with following:
- 1 Clone the repository:
  - ```git clone https://github.com/Sovok1917/dirwalk/```
- 2 Build the project:
  - ```make MODE=release```
- 3 Running the app (either one works):
  - ```./build/release/prog [options] [directory]```
  - or
  - ```./build/release/prog [directory] [options]```

=======
# Linux Parent-Child Process Environment Management Example

This project demonstrates fundamental concepts of process creation and environment variable management in C on Linux/POSIX systems using `fork()` and `execve()`. It consists of two programs: a `parent` process that launches a `child` process in different modes to illustrate how environment variables can be passed or inherited.

## Description

The core purpose is to show the difference between:

1.  A child process inheriting the parent's environment and using `getenv()` to look up specific variables listed in a configuration file.
2.  A parent process constructing a minimal, custom environment array (`envp`) containing only specified variables and passing it directly to the child via `execve()`.

The parent process takes simple commands (`+`, `*`, `&`, `q`) to trigger these different behaviors.

## Features

*   Demonstrates process creation using `fork()`.
*   Shows how to replace a process image using `execve()`.
*   Illustrates passing command-line arguments (`argv`) to a child process.
*   Contrasts two common methods for controlling a child's environment variables:
    *   **`+` / `&` mode:** Child inherits environment, reads variable names from a file (`env`), and uses `getenv()` to find values.
    *   **`*` mode:** Parent builds a custom `char *envp[]` array based on the `env` file and passes it explicitly via `execve()`. Child iterates this array.
*   Parent process prints its own sorted environment for comparison.
*   Handles basic user interaction via standard input.
*   Includes launching the child process in the foreground or background (`&`).
*   Uses the `CHILD_PATH` environment variable to locate the child executable and configuration file, allowing for separate build directories (e.g., debug/release).
*   Basic error handling for system calls and file operations.

## How it Works

1.  **`parent.c`:**
    *   Reads the `CHILD_PATH` environment variable to find the directory containing the `child` executable and the `env` file.
    *   Prints its own environment variables, sorted alphabetically (using `LC_COLLATE=C`).
    *   Enters a loop, prompting the user for a command:
        *   `+`: (Foreground `getenv` mode) Reads the `env` file to determine *which* variable names to look for. Launches the `child` executable, passing the *path* to the `env` file as `argv[1]`. The child inherits the parent's full environment.
        *   `*`: (Foreground `envp` mode) Reads the `env` file. For each variable name listed, it retrieves the value from its *own* environment using `getenv()`. It constructs a new environment array (`envp`) containing only these `NAME=VALUE` pairs. It launches the `child` executable with *no* extra arguments (`argc=1`) but passes the custom `envp` array as the third argument to `execve()`.
        *   `&`: (Background `getenv` mode) Functionally the same as `+`, but the parent does not wait for the child to complete.
        *   `q`: Exits the parent program.
    *   For foreground commands (`+`, `*`), the parent waits briefly (`usleep`) after launching the child to allow the child's output to appear before the next parent prompt (this is a simple mechanism, not robust signal handling).
    *   For background commands (`&`), the parent does not wait. Note: This basic example doesn't handle `SIGCHLD` or `waitpid()` for background processes, so finished background children might become zombies until the parent exits.
    *   Uses `child_XX` naming convention for launched child processes (`argv[0]`).

2.  **`child.c`:**
    *   Prints its process name (`argv[0]`), PID, and PPID.
    *   Checks `argc` to determine its mode:
        *   If `argc == 2` (`+` or `&` mode): It assumes `argv[1]` is the path to the `env` file. It opens and reads this file. For each variable name found, it calls `getenv()` to look up the value in its *own* (inherited) environment and prints the result.
        *   If `argc == 1` (`*` mode): It iterates through the `envp` array passed to its `main` function (which was supplied by the parent via `execve()`) and prints each `NAME=VALUE` string directly.
    *   Prints a completion message and exits.

3.  **`env` file:**
    *   A simple text file located in the directory specified by `CHILD_PATH`.
    *   Each line should contain the *name* of an environment variable that the parent should process (either to pass its value in `*` mode or to tell the child to look it up in `+`/`&` mode).
    *   Lines starting with `#` and empty lines are ignored.

4.  **`CHILD_PATH` Environment Variable:**
    *   **Crucial:** This environment variable *must* be set before running the `parent` executable.
    *   It must point to the directory where the compiled `child` executable and the `env` file reside (e.g., `build/debug` or `build/release`).
    *   The provided `Makefile` (if used) typically handles setting this when using targets like `make run`.

## Building and Running

*(Assuming a standard Makefile setup, adjust if necessary)*

1.  **Compile:**
    ```bash
    make # Or 'make all' - Creates build/debug/parent, build/debug/child, build/debug/env
    # or optionally
    make release # Creates build/release/...
    ```
    *Note: The Makefile should create the executables and likely a default `env` file (e.g., containing `USER`, `HOME`, `PATH`) in the appropriate `build/` subdirectory.*

2.  **Run (using Makefile recommended):**
    ```bash
    # Run the debug version
    make run

    # Run the release version (if configured)
    make run-release
    ```
    *The `make run` targets typically set the `CHILD_PATH` environment variable correctly before executing the parent.*

3.  **Run (Manually):**
    *   First, set the `CHILD_PATH` variable:
        ```bash
        # Example for bash/zsh if built with 'make'
        export CHILD_PATH=build/debug
        ```
    *   Then, run the parent executable:
        ```bash
        ./build/debug/parent
        ```

4.  **Interact:**
    *   Once the parent starts, it will print its own environment and prompt you with `> `.
    *   Enter `+`, `*`, `&`, or `q` followed by Enter.
    *   Observe the output from both the parent and the launched child processes.

## Code Structure

*   `parent.c`: Source code for the main parent process.
*   `child.c`: Source code for the child process.
*   `Makefile`: (Assumed) Build instructions for compiling the project and potentially running it.
*   `env`: (Created by Makefile or manually) Configuration file listing environment variable names for the child. Located in the build directory (`$CHILD_PATH`).

## Key Concepts Demonstrated

*   Process Management: `fork()`, `execve()`, `getpid()`, `getppid()`, `wait()` (implicitly, via shell or briefly via `usleep`), `_exit()` vs `exit()`
*   Environment Variables: `getenv()`, `environ` (global variable), `envp` (argument to `main` and `execve`), passing environment to children.
*   Inter-Process Communication (IPC): Via environment variables and command-line arguments.
*   File I/O: `fopen()`, `fgets()`, `fclose()` to read the `env` configuration file.
*   String Manipulation: `strcmp()`, `strcpy()`, `snprintf()`, `strcspn()`, `isspace()`.
*   Dynamic Memory Allocation: `malloc()`, `realloc()`, `free()` for building the child's environment array.
*   Sorting: `qsort()` for displaying the parent's environment.
*   Basic Error Handling: Using `perror()` and checking return values.
*   Build Systems: Implied use of `make`.
*   POSIX/Linux System Calls and C Standard Library features.
>>>>>>> 893015ecd0e4460fe26abb186e36c77213304779
