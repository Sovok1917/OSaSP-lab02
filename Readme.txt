Project: Parent-Child Process Interaction Demo

Description:
This project consists of two programs: 'parent' and 'child'.
The 'parent' program manages its environment, sorts and prints it, and then
enters a command loop. Based on user input ('+', '*', '&'), it launches
instances of the 'child' program. The method of locating the 'child' executable
(via getenv, main's envp, or environ) depends on the command.
Each 'child' process is given a filtered environment, defined by a list of
variable names in a specified filter file. The 'child' program prints its
identity (name, PID, PPID) and then prints the values of the environment
variables listed in the filter file, as present in its received environment.

The path to the directory containing the 'child' executable must be provided
to the 'parent' program via the `CHILD_PATH` environment variable.

Files:
- src/parent.c: Source code for the parent program.
- src/child.c:  Source code for the child program.
- Makefile:     Build script for compiling the project.
- readme.txt:   This file.

Build Instructions:
The project uses 'make' for building. Ensure you have 'gcc' and 'make' installed.

1.  Default Build (Debug Mode):
    Open a terminal in the project's root directory (where the Makefile is located).
    Run:
    make
    or
    make MODE=debug

    This will compile the 'parent' and 'child' programs with debugging symbols
    and place the executables and the environment filter file (`env_filter.txt`)
    in the `build/debug/` directory.

2.  Release Build:
    To build in release mode (with optimizations and warnings treated as errors):
    make MODE=release

    This will place the executables and filter file in `build/release/`.

3.  Clean Build Artifacts:
    To remove all compiled files and build directories:
    make clean

Running the Program:

1.  Set the `CHILD_PATH` Environment Variable:
    Before running the `parent` program, you must set the `CHILD_PATH` environment
    variable to point to the directory where the `child` executable is located.
    This will be `build/debug` or `build/release` depending on how you built it.

    Example (if you built in debug mode):
    export CHILD_PATH="$(pwd)/build/debug"

    If you use the `make run` or `make run-release` targets, the Makefile handles this automatically.

2.  Run the Parent Program:
    The parent program requires one command-line argument: the path to the
    environment filter file. The Makefile creates a default one named `env_filter.txt`
    in the respective build directory.

    Example (running debug build manually after setting CHILD_PATH):
    ./build/debug/parent ./build/debug/env_filter.txt

    Using Makefile 'run' targets (recommended, as they set CHILD_PATH):
    - For debug mode:
      make run
    - For release mode:
      make run-release

3.  Parent Program Commands:
    Once the parent program is running, it will print its initial environment
    and then prompt for commands:
    - `+` : Launch a child using `getenv("CHILD_PATH")`.
    - `*` : Launch a child using `main`'s `envp` to find `CHILD_PATH`.
    - `&` : Launch a child using `environ` to find `CHILD_PATH`. (Parent continues running)
    - `q` : Quit the parent program.

    Each launched child will print its details and its filtered environment variables
    to standard output.

Example Session (using `make run`):
    $ make run
    # ... (build output) ...
    Running DEBUG version build/debug/parent with filter file build/debug/env_filter.txt...
    Setting CHILD_PATH='/path/to/your/project/lab02/build/debug' for execution.
    Parent PID: 12345
    Initial environment variables (sorted LC_COLLATE=C):
    # ... (sorted environment of parent) ...
    ----------------------------------------
    Enter command (+, *, & to launch child, q to quit):
    > +
    Parent: Launching child 'child_00' using method '+'...
    Parent: Child executable path: /path/to/your/project/lab02/build/debug/child
    Parent: Forked child process 'child_00' with PID 12346.
    Child: Name='child_00', PID=12346, PPID=12345
    Child: Using environment filter file: /path/to/your/project/lab02/build/debug/env_filter.txt
    Child: Received Environment Variables (from filter list):
      SHELL=/bin/bash
      HOME=/home/user
      # ... (other filtered variables) ...
      CHILD_ENV_FILTER_FILE=/path/to/your/project/lab02/build/debug/env_filter.txt
    Child: (child_00, 12346) exiting.
    > &
    Parent: Launching child 'child_01' using method '&'...
    Parent: Child executable path: /path/to/your/project/lab02/build/debug/child
    Parent: Forked child process 'child_01' with PID 12347.
    Parent: Launched child via '&', parent continues.
    Child: Name='child_01', PID=12347, PPID=12345
    Child: Using environment filter file: /path/to/your/project/lab02/build/debug/env_filter.txt
    Child: Received Environment Variables (from filter list):
    # ... (child output) ...
    Child: (child_01, 12347) exiting.
    > q
    Parent: Quit command received. Exiting.
    Parent: Exiting cleanly.
