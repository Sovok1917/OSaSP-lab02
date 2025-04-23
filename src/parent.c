/*
 * parent.c
 *
 * Description:
 * This program serves as the parent process responsible for launching instances
 * of a child program ('child.c'). It demonstrates process creation (fork),
 * execution (execve), and environment manipulation.
 *
 * Features:
 * - Sorts and prints its own initial environment variables using the "C" locale.
 * - Waits for keyboard commands (+, *, &) to launch a child process.
 * - Uses different methods (+: getenv, *: main's envp, &: environ) to locate the
 *   path to the child executable, specified by the CHILD_PATH environment variable.
 * - Creates a filtered environment for each child based on variable names listed
 *   in a file specified as a command-line argument.
 * - Passes the filter file path itself to the child via an environment variable.
 * - The '&' command causes the parent to terminate after launching the child.
 * - Manages child process numbering (e.g., child_00, child_01).
 *
 * Pre-requisites:
 * - Requires the 'child' executable to be compiled and accessible.
 * - Requires the CHILD_PATH environment variable to be set, pointing to the
 *   directory containing the 'child' executable.
 * - Requires one command-line argument: the path to the environment filter file.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     // fork, execve, getpid, environ
#include <sys/types.h>  // pid_t
#include <sys/wait.h>   // waitpid
#include <errno.h>      // errno
#include <locale.h>     // setlocale, strcoll
#include <limits.h>     // PATH_MAX (potentially useful, though not strictly needed here)
#include <stdbool.h>

// External variable holding the process environment (standard).
extern char **environ;

// Configuration constants
#define MAX_CHILDREN 100        // Limit on child process suffixes (00-99)
#define BUFFER_SIZE 1024        // General buffer size for paths/strings
#define CHILD_EXECUTABLE_NAME "child" // Expected name of the child executable file
#define ENV_VAR_FILTER_FILE_NAME "CHILD_ENV_FILTER_FILE" // Env var for filter file path

// Type definition for a dynamically managed list of environment strings.
typedef struct {
    char **vars;    // Array of "NAME=VALUE" strings
    size_t count;   // Number of strings currently in the array
    size_t capacity;// Allocated capacity of the 'vars' array
} env_list_t;

// Global counter for assigning unique numbers to child processes.
static int g_child_number = 0;

/* --- Function Prototypes --- */

static int compare_env_vars(const void *a, const void *b);
static char *find_env_var_value(const char *var_name, char **env_array);
static env_list_t create_filtered_env(const char *filter_filename, char **source_env);
static void free_env_list(env_list_t *list);
static int launch_child(char method, const char *filter_filename, char **main_envp);
static void print_usage(const char *prog_name);

/*
 * Purpose:
 *   Main entry point for the parent program. It orchestrates the setup and
 *   command loop for launching child processes.
 *   1. Validates command-line arguments (requires one: filter file path).
 *   2. Prints its own PID.
 *   3. Sorts its initial environment variables using the "C" locale and prints them.
 *   4. Enters a loop, prompting the user for commands (+, *, &, q).
 *   5. Based on the command, calls launch_child() to create and execute a
 *      child process with appropriate settings.
 *   6. Exits the loop and terminates if 'q' is entered or after launching with '&'.
 * Receives:
 *   argc: The number of command-line arguments.
 *   argv: An array of command-line argument strings. argv[0] is the program name,
 *         argv[1] should be the path to the environment filter file.
 *   envp: An array of strings representing the environment variables passed to
 *         this process by the operating system when it started.
 * Returns:
 *   EXIT_SUCCESS (0) if the program completes normally.
 *   EXIT_FAILURE (1) if an error occurs (e.g., incorrect arguments, memory
 *   allocation failure, failure during setup).
 */
int main(int argc, char *argv[], char *envp[]) {
    // Validate command line arguments.
    if (argc != 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    const char *env_filter_file = argv[1];

    // Print parent identity and initial environment.
    printf("Parent PID: %d\n", getpid());
    printf("Initial environment variables (sorted LC_COLLATE=C):\n");

    // Count environment variables in envp.
    int env_count = 0;
    for (char **env = envp; *env != NULL; ++env) {
        env_count++;
    }

    // Create a temporary array of pointers to sort envp without modifying it directly.
    char **sorted_envp = malloc(env_count * sizeof(char *));
    if (sorted_envp == NULL) {
        perror("Parent: Failed to allocate memory for environment sorting");
        return EXIT_FAILURE;
    }
    for (int i = 0; i < env_count; ++i) {
        sorted_envp[i] = envp[i];
    }

    // Set locale to "C" for sorting, as required.
    if (setlocale(LC_COLLATE, "C") == NULL) {
        fprintf(stderr, "Parent: Warning - Failed to set LC_COLLATE to C. Sorting might be incorrect.\n");
        // Continue, but sorting behavior might differ.
    }

    // Sort the pointers based on the strings they point to.
    qsort(sorted_envp, env_count, sizeof(char *), compare_env_vars);

    // Print the sorted environment.
    for (int i = 0; i < env_count; ++i) {
        if (printf("%s\n", sorted_envp[i]) < 0) {
            perror("Parent: Failed to print environment variable");
            free(sorted_envp);
            return EXIT_FAILURE;
        }
    }
    printf("----------------------------------------\n");

    free(sorted_envp); // Free the temporary pointer array.

    // Main command processing loop.
    printf("Enter command (+, *, & to launch child, q to quit):\n> ");
    fflush(stdout);

    int command_char;
    bool terminate_parent = false; // Flag to exit loop.

    while ((command_char = getchar()) != EOF) {
        // Ignore leading/trailing whitespace around the command character.
        if (command_char == '\n' || command_char == ' ' || command_char == '\t') {
            continue;
        }

        // Consume the rest of the input line (up to newline or EOF).
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF);

        switch (command_char) {
            case '+': // Launch child using getenv for CHILD_PATH
            case '*': // Launch child using main's envp for CHILD_PATH
            case '&': // Launch child using 'environ' for CHILD_PATH and then terminate parent
                if (launch_child((char)command_char, env_filter_file, envp) != 0) {
                    fprintf(stderr, "Parent: Failed to launch child process for command '%c'.\n", command_char);
                    // Continue loop unless it was '&'
                }
                if (command_char == '&') {
                    printf("Parent: Initiating termination after launching child via '&'.\n");
                    terminate_parent = true; // Signal loop exit.
                }
                break;
            case 'q': // Quit command
            case 'Q':
                printf("Parent: Quit command received. Exiting.\n");
                terminate_parent = true;
                break;
            default: // Unknown command
                printf("Parent: Unknown command '%c'. Use +, *, &, or q.\n", command_char);
                break;
        }

        if (terminate_parent) {
            break; // Exit the command loop.
        }

        printf("> "); // Prompt for the next command.
        fflush(stdout);
    }

    printf("Parent: Exiting cleanly.\n");
    return EXIT_SUCCESS;
}

/*
 * Purpose:
 *   Prints usage instructions for the parent program to the standard error stream.
 * Receives:
 *   prog_name: The name of the executable (typically argv[0]).
 * Returns:
 *   None (void).
 */
static void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s <environment_filter_file>\n", prog_name);
    fprintf(stderr, "  <environment_filter_file>: Path to a file listing environment variables\n");
    fprintf(stderr, "                             (one per line) to pass to child processes.\n");
    fprintf(stderr, "  Requires CHILD_PATH environment variable to be set to the directory\n");
    fprintf(stderr, "  containing the '%s' executable.\n", CHILD_EXECUTABLE_NAME);
}


/*
 * Purpose:
 *   Comparison function suitable for use with qsort() to sort an array of
 *   C strings (char *). It uses strcoll() to perform the comparison based
 *   on the current LC_COLLATE locale setting (expected to be "C" in this program).
 * Receives:
 *   a: A const void pointer to the first element (which is a char *).
 *   b: A const void pointer to the second element (which is a char *).
 * Returns:
 *   An integer < 0 if the string pointed to by 'a' is less than the string 'b'.
 *   An integer = 0 if the string pointed to by 'a' is equal to the string 'b'.
 *   An integer > 0 if the string pointed to by 'a' is greater than the string 'b'.
 *   (Based on strcoll() semantics).
 */
static int compare_env_vars(const void *a, const void *b) {
    // Cast the void pointers to the actual type being sorted (pointers to char pointers).
    const char **str_a = (const char **)a;
    const char **str_b = (const char **)b;

    // Dereference the pointers to get the strings and compare using strcoll.
    return strcoll(*str_a, *str_b);
}

/*
 * Purpose:
 *   Searches for a specific environment variable within a given environment
 *   array (such as 'environ' or the 'envp' passed to main).
 * Receives:
 *   var_name:  The name of the environment variable to find (null-terminated string).
 *   env_array: The NULL-terminated array of environment strings ("NAME=VALUE") to search.
 * Returns:
 *   A pointer to the value part of the matched "NAME=VALUE" string if found.
 *   NULL if the variable is not found, or if input parameters are invalid.
 *   Note: The returned pointer points into the existing 'env_array'. Do not free it.
 */
static char *find_env_var_value(const char *var_name, char **env_array) {
    if (var_name == NULL || env_array == NULL) {
        return NULL;
    }
    size_t name_len = strlen(var_name);
    if (name_len == 0) {
        return NULL;
    }

    for (char **env = env_array; *env != NULL; ++env) {
        // Check if the current entry starts with "var_name=".
        if (strncmp(*env, var_name, name_len) == 0 && (*env)[name_len] == '=') {
            return (*env) + name_len + 1; // Return pointer to the value part.
        }
    }
    return NULL; // Not found.
}

/*
 * Purpose:
 *   Creates a new, dynamically allocated environment array (suitable for execve)
 *   containing only the environment variables specified in a filter file. It reads
 *   variable names from the file, looks up their values in a source environment
 *   (e.g., the parent's 'environ'), and constructs "NAME=VALUE" strings for the
 *   new array. It also automatically includes an entry for ENV_VAR_FILTER_FILE_NAME
 *   pointing to the provided filter file path, so the child can locate it.
 * Receives:
 *   filter_filename: Path to the text file listing desired environment variable names,
 *                    one per line. Lines starting with '#' are ignored.
 *   source_env:      The environment array (e.g., 'environ') from which to retrieve
 *                    the values for the variables listed in the filter file.
 * Returns:
 *   An env_list_t structure containing the newly allocated, NULL-terminated
 *   environment array (`list.vars`). The caller is responsible for freeing
 *   this list using free_env_list().
 *   On error (e.g., cannot open file, memory allocation fails), the returned
 *   list will have `list.vars` set to NULL. An error message is printed to stderr.
 */
static env_list_t create_filtered_env(const char *filter_filename, char **source_env) {
    env_list_t list = { .vars = NULL, .count = 0, .capacity = 10 }; // Initial state
    FILE *file = fopen(filter_filename, "r");
    if (file == NULL) {
        perror("Parent: Failed to open environment filter file");
        return list; // Return list with vars=NULL
    }

    // Allocate initial memory for the pointer array.
    list.vars = malloc(list.capacity * sizeof(char *));
    if (list.vars == NULL) {
        perror("Parent: Failed to allocate initial memory for filtered environment");
        fclose(file);
        return list; // Return list with vars=NULL
    }

    char *line_buf = NULL;
    size_t line_buf_size = 0;
    ssize_t line_len;

    // Read variable names from the filter file.
    while ((line_len = getline(&line_buf, &line_buf_size, file)) != -1) {
        // Clean up newline character.
        if (line_len > 0 && line_buf[line_len - 1] == '\n') {
            line_buf[line_len - 1] = '\0';
            line_len--;
        }

        // Skip empty lines and comments.
        if (line_len == 0 || line_buf[0] == '#') {
            continue;
        }

        char *var_name = line_buf;
        // Find the value in the *source* environment provided.
        char *var_value = find_env_var_value(var_name, source_env);

        if (var_value != NULL) {
            // Allocate memory for the "NAME=VALUE" string.
            size_t entry_len = strlen(var_name) + 1 + strlen(var_value) + 1; // name + '=' + value + '\0'
            char *env_entry = malloc(entry_len);
            if (env_entry == NULL) {
                perror("Parent: Failed to allocate memory for environment entry");
                free(line_buf);
                fclose(file);
                free_env_list(&list); // Clean up partially built list.
                list.vars = NULL;     // Signal error.
                return list;
            }
            snprintf(env_entry, entry_len, "%s=%s", var_name, var_value);

            // Resize pointer array if necessary (ensure space for entry + NULL terminator).
            if (list.count >= list.capacity - 1) {
                size_t new_capacity = list.capacity * 2;
                char **new_vars = realloc(list.vars, new_capacity * sizeof(char *));
                if (new_vars == NULL) {
                    perror("Parent: Failed to reallocate memory for filtered environment");
                    free(env_entry); // Free the entry we couldn't add.
                    free(line_buf);
                    fclose(file);
                    free_env_list(&list);
                    list.vars = NULL;
                    return list;
                }
                list.vars = new_vars;
                list.capacity = new_capacity;
            }
            // Add the newly created entry to the list.
            list.vars[list.count++] = env_entry;
        }
        // Optional: Could add a warning here if var_value is NULL (variable not found).
    } // End while getline

    free(line_buf);
    fclose(file);

    // Add the filter file path variable itself to the child's environment.
    const char *filter_var_name = ENV_VAR_FILTER_FILE_NAME;
    const char *filter_var_value = filter_filename;

    size_t filter_entry_len = strlen(filter_var_name) + 1 + strlen(filter_var_value) + 1;
    char *filter_env_entry = malloc(filter_entry_len);
    if (filter_env_entry == NULL) {
        perror("Parent: Failed to allocate memory for filter file path env entry");
        free_env_list(&list);
        list.vars = NULL;
        return list;
    }
    snprintf(filter_env_entry, filter_entry_len, "%s=%s", filter_var_name, filter_var_value);

    // Ensure space again for this entry + NULL terminator.
    if (list.count >= list.capacity - 1) {
        size_t new_capacity = list.capacity + 2; // Just add enough space.
        char **new_vars = realloc(list.vars, new_capacity * sizeof(char *));
        if (new_vars == NULL) {
            perror("Parent: Failed to reallocate memory for filter file path env entry");
            free(filter_env_entry);
            free_env_list(&list);
            list.vars = NULL;
            return list;
        }
        list.vars = new_vars;
        list.capacity = new_capacity;
    }
    // Add the filter file path entry.
    list.vars[list.count++] = filter_env_entry;

    // NULL-terminate the environment array.
    list.vars[list.count] = NULL;

    return list; // Return the completed list.
}


/*
 * Purpose:
 *   Frees all memory associated with an env_list_t structure. This includes
 *   freeing each individual "NAME=VALUE" string stored within the list's 'vars'
 *   array and then freeing the 'vars' array itself. It also resets the list's
 *   count and capacity members.
 * Receives:
 *   list: A pointer to the env_list_t structure to be freed. Handles NULL list
 *         or list with NULL 'vars' pointer gracefully.
 * Returns:
 *   None (void).
 */
static void free_env_list(env_list_t *list) {
    if (list == NULL || list->vars == NULL) {
        return; // Nothing to free.
    }
    // Free each allocated string in the array.
    for (size_t i = 0; i < list->count; ++i) {
        free(list->vars[i]);
        list->vars[i] = NULL; // Good practice, though array is freed next.
    }
    // Free the array of pointers.
    free(list->vars);

    // Reset the list structure members.
    list->vars = NULL;
    list->count = 0;
    list->capacity = 0;
}


/*
 * Purpose:
 *   Handles the process of launching a child process. This involves:
 *   1. Determining the directory containing the child executable (CHILD_PATH)
 *      based on the specified method ('+', '*', '&').
 *   2. Constructing the full path to the child executable.
 *   3. Creating a unique name for the child instance (e.g., "child_00").
 *   4. Creating the filtered environment array for the child using create_filtered_env().
 *   5. Forking the current process.
 *   6. In the child process: Executing the child program ('child') using execve(),
 *      passing the constructed name, arguments, and the filtered environment. Handles
 *      execve errors by printing an error and calling _exit().
 *   7. In the parent process: Prints the PID of the forked child, frees the memory
 *      allocated for the filtered environment (as the child has its own copy), and
 *      returns. The parent does not wait for the child to complete.
 * Receives:
 *   method:          A character indicating how to find CHILD_PATH:
 *                    '+' uses getenv().
 *                    '*' uses the envp array passed to parent's main().
 *                    '&' uses the global 'environ' variable.
 *   filter_filename: The path to the environment filter file.
 *   main_envp:       The 'envp' array received by the parent's main function (used only
 *                    if method is '*').
 * Returns:
 *   0 if the fork and setup in the parent were successful (execve success/failure
 *     is handled within the child).
 *   -1 if an error occurs in the parent before or during the fork (e.g., CHILD_PATH
 *      not found, memory allocation failure, fork failure). Error messages are
 *      printed to stderr.
 */
static int launch_child(char method, const char *filter_filename, char **main_envp) {
    if (g_child_number >= MAX_CHILDREN) {
        fprintf(stderr, "Parent: Maximum number of children (%d) reached.\n", MAX_CHILDREN);
        return -1;
    }

    // Determine the directory containing the child executable (CHILD_PATH).
    char *child_dir = NULL;
    const char *child_path_var_name = "CHILD_PATH";

    switch (method) {
        case '+': child_dir = getenv(child_path_var_name); break;
        case '*': child_dir = find_env_var_value(child_path_var_name, main_envp); break;
        case '&': child_dir = find_env_var_value(child_path_var_name, environ); break;
        default:
            fprintf(stderr, "Parent: Internal error - Invalid launch method '%c'.\n", method);
            return -1;
    }

    if (child_dir == NULL) {
        fprintf(stderr, "Parent: Error - CHILD_PATH environment variable not found using method '%c'.\n", method);
        return -1;
    }
    if (strlen(child_dir) == 0) {
        fprintf(stderr, "Parent: Error - CHILD_PATH environment variable is empty for method '%c'.\n", method);
        return -1;
    }

    // Construct the full path to the child executable.
    char child_exec_path[BUFFER_SIZE];
    int path_len = snprintf(child_exec_path, sizeof(child_exec_path), "%s/%s", child_dir, CHILD_EXECUTABLE_NAME);
    if (path_len < 0 || (size_t)path_len >= sizeof(child_exec_path)) {
        fprintf(stderr, "Parent: Error constructing child executable path (too long or snprintf error).\n");
        return -1;
    }

    // Construct the name (argv[0]) for the child process instance.
    char child_argv0[32]; // e.g., "child_00"
    snprintf(child_argv0, sizeof(child_argv0), "%s_%.2d", CHILD_EXECUTABLE_NAME, g_child_number);
    g_child_number++; // Increment for the next child.

    // Create the filtered environment list for the child.
    // Use 'environ' as the source for values, as it's generally the most up-to-date
    // and accessible view of the environment (similar to what getenv uses).
    env_list_t filtered_env_list = create_filtered_env(filter_filename, environ);
    if (filtered_env_list.vars == NULL) {
        fprintf(stderr, "Parent: Failed to create filtered environment for child.\n");
        // create_filtered_env should have printed specific perror reason.
        return -1;
    }

    printf("Parent: Launching child '%s' using method '%c'...\n", child_argv0, method);
    printf("Parent: Child executable path: %s\n", child_exec_path);
    fflush(stdout);

    // Fork the process.
    pid_t pid = fork();

    if (pid < 0) {
        // Fork failed.
        perror("Parent: fork() failed");
        free_env_list(&filtered_env_list); // Clean up allocated environment.
        return -1;

    } else if (pid == 0) {
        // --- Child Process ---
        char *child_argv[] = {child_argv0, NULL}; // Prepare argv for the child.

        // Execute the child program. execve replaces the current process image.
        execve(child_exec_path, child_argv, filtered_env_list.vars);

        // If execve returns, an error occurred.
        perror("Child (in parent context before exec): execve failed");
        fprintf(stderr, "Child (in parent context before exec): Failed attempt to execute '%s'\n", child_exec_path);
        // Important: Use _exit() in the child after a fork/exec failure.
        // This prevents the child from returning to the parent's code path
        // and avoids duplicating actions like flushing parent's stdio buffers
        // or running parent's atexit handlers.
        free_env_list(&filtered_env_list); // Attempt cleanup before exiting.
        _exit(EXIT_FAILURE); // Use _exit, not exit.

    } else {
        // --- Parent Process ---
        printf("Parent: Forked child process with PID %d.\n", pid);
        fflush(stdout);

        // Parent cleans up its copy of the filtered environment list.
        // The child received its own copies of the strings via execve.
        free_env_list(&filtered_env_list);

        // Parent does not wait for the child, continues its loop (or terminates if method was '&').
        // No wait() or waitpid() call here.

        // Note: In a long-running parent, it might be necessary to periodically
        // call waitpid(-1, NULL, WNOHANG) to reap zombie child processes.
        // For this assignment's structure, it's likely not critical.
    }

    return 0; // Parent returns success after fork.
}
