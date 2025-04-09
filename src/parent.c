/*
 * parent.c: Parent process for demonstrating process creation and environment handling.
 *
 * This program sorts and prints its initial environment variables. It then enters
 * a loop waiting for specific keyboard commands (+, *, &) to launch child processes.
 * Each launch method uses a different technique to find the child executable's path
 * and passes a filtered environment to the child based on a configuration file.
 * The '&' command also causes the parent process to terminate after launching the child.
 *
 * Requires the CHILD_PATH environment variable to be set to the directory
 * containing the child executable before running.
 * Requires a command-line argument specifying the path to the environment
 * variable filter file.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     // fork, execve, getpid, getppid, environ
#include <sys/types.h>  // pid_t
#include <sys/wait.h>   // waitpid
#include <errno.h>      // errno
#include <locale.h>     // setlocale, strcoll
#include <limits.h>     // PATH_MAX (potentially)

// External variable defined by the environment (IEEE Std 1003.1-2017)
extern char **environ;

// Maximum number of child processes (00-99)
#define MAX_CHILDREN 100
// Buffer size for constructing paths and arguments
#define BUFFER_SIZE 1024
// Name of the child executable file
#define CHILD_EXECUTABLE_NAME "child"
// Name of the environment variable used to pass the filter file path
#define ENV_VAR_FILTER_FILE_NAME "CHILD_ENV_FILTER_FILE"

// Type definition for environment array list (dynamic)
typedef struct {
    char **vars;
    size_t count;
    size_t capacity;
} env_list_t;

// Global counter for child process numbers
static int g_child_number = 0;

/* --- Function Prototypes --- */

// Comparison function for qsort using LC_COLLATE=C (via strcoll)
static int compare_env_vars(const void *a, const void *b);

// Finds the value of an environment variable within a given environment array.
static char *find_env_var_value(const char *var_name, char **env_array);

// Creates a filtered environment array based on names read from a file.
static env_list_t create_filtered_env(const char *filter_filename, char **source_env);

// Frees the memory allocated for a filtered environment list.
static void free_env_list(env_list_t *list);

// Launches a child process using the specified method.
static int launch_child(char method, const char *filter_filename, char **main_envp);

// Prints usage information to stderr.
static void print_usage(const char *prog_name);

/*
 * main: Entry point of the parent program.
 *
 * Parses arguments, sorts and prints environment variables, and enters
 * the command processing loop to launch child processes.
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line argument strings.
 * @param envp Array of environment variable strings passed by the OS.
 * @return EXIT_SUCCESS on successful completion, EXIT_FAILURE on error.
 */
int main(int argc, char *argv[], char *envp[]) {
    // 1. Check Command Line Arguments
    if (argc != 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    const char *env_filter_file = argv[1];

    // 2. Sort and Print Environment Variables
    printf("Parent PID: %d\n", getpid());
    printf("Initial environment variables (sorted LC_COLLATE=C):\n");

    // Count environment variables
    int env_count = 0;
    for (char **env = envp; *env != NULL; ++env) {
        env_count++;
    }

    // Allocate memory to store pointers for sorting
    char **sorted_envp = malloc(env_count * sizeof(char *));
    if (sorted_envp == NULL) {
        perror("Failed to allocate memory for environment sorting");
        return EXIT_FAILURE;
    }

    // Copy pointers from envp
    for (int i = 0; i < env_count; ++i) {
        sorted_envp[i] = envp[i];
    }

    // Set locale for C collation standard required by the task
    if (setlocale(LC_COLLATE, "C") == NULL) {
        fprintf(stderr, "Warning: Failed to set LC_COLLATE to C. Sorting might be incorrect.\n");
        // Continue execution, but sorting might use system default
    }

    // Sort the environment variables
    qsort(sorted_envp, env_count, sizeof(char *), compare_env_vars);

    // Print the sorted environment variables
    for (int i = 0; i < env_count; ++i) {
        if (printf("%s\n", sorted_envp[i]) < 0) {
            perror("Failed to print environment variable");
            free(sorted_envp); // Clean up allocated memory
            return EXIT_FAILURE;
        }
    }
    printf("----------------------------------------\n");

    free(sorted_envp); // Free the temporary array of pointers

    // 3. Command Processing Loop
    printf("Enter command (+, *, & to launch child, q to quit):\n> ");
    fflush(stdout); // Ensure prompt is displayed

    int command_char;
    int terminate_parent = 0; // Flag to signal parent termination after '&' launch

    while ((command_char = getchar()) != EOF) {
        // Ignore whitespace like newline that might follow the command char
        if (command_char == '\n' || command_char == ' ' || command_char == '\t') {
            continue;
        }

        // Consume rest of the line until newline or EOF
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF);

        switch (command_char) {
            case '+':
            case '*':
            case '&':
                if (launch_child((char)command_char, env_filter_file, envp) != 0) {
                    fprintf(stderr, "Parent: Failed to launch child process.\n");
                }
                if (command_char == '&') {
                    printf("Parent: Initiating termination after launching child via '&'.\n");
                    terminate_parent = 1; // Set flag to terminate after this iteration
                }
                break;
            case 'q':
            case 'Q':
                printf("Parent: Quit command received. Exiting.\n");
                terminate_parent = 1;
                break;
            default:
                printf("Parent: Unknown command '%c'. Use +, *, &, or q.\n", command_char);
                break;
        }

        if (terminate_parent) {
            break; // Exit the while loop
        }

        printf("> "); // Prompt for next command
        fflush(stdout);
    }

    printf("Parent: Exiting cleanly.\n");
    return EXIT_SUCCESS;
}

/*
 * print_usage: Prints usage instructions to stderr.
 *
 * @param prog_name The name of the executable (argv[0]).
 */
static void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s <environment_filter_file>\n", prog_name);
    fprintf(stderr, "  <environment_filter_file>: Path to a file listing environment variables\n");
    fprintf(stderr, "                             (one per line) to pass to child processes.\n");
    fprintf(stderr, "  Requires CHILD_PATH environment variable to be set to the directory\n");
    fprintf(stderr, "  containing the 'child' executable.\n");
}


/*
 * compare_env_vars: Comparison function for qsort.
 *
 * Compares two C strings using strcoll, respecting the current LC_COLLATE locale.
 * Used for sorting environment variables according to LC_COLLATE=C setting.
 *
 * @param a Pointer to the first string pointer.
 * @param b Pointer to the second string pointer.
 * @return An integer less than, equal to, or greater than zero if the first
 *         string is found, respectively, to be less than, to match, or be
 *         greater than the second.
 */
static int compare_env_vars(const void *a, const void *b) {
    // Cast void pointers back to the correct type (pointer to char pointer)
    const char **str_a = (const char **)a;
    const char **str_b = (const char **)b;

    // Use strcoll for locale-aware string comparison
    return strcoll(*str_a, *str_b);
}

/*
 * find_env_var_value: Searches an environment array for a specific variable.
 *
 * Iterates through the provided environment array (NULL-terminated list of
 * "NAME=VALUE" strings) and returns a pointer to the VALUE part if the
 * variable NAME is found.
 *
 * @param var_name The name of the environment variable to find (e.g., "PATH").
 * @param env_array The environment array (like envp or environ) to search.
 * @return Pointer to the value string if found, NULL otherwise. The returned
 *         pointer points into the existing env_array strings, do not free it.
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
        // Check if the current env string starts with "var_name="
        if (strncmp(*env, var_name, name_len) == 0 && (*env)[name_len] == '=') {
            return (*env) + name_len + 1; // Return pointer to the character after '='
        }
    }
    return NULL; // Variable not found
}

/*
 * create_filtered_env: Reads variable names from a file and creates a new env array.
 *
 * Opens the filter file, reads variable names line by line. For each name,
 * it retrieves the corresponding value from the source environment and adds
 * "NAME=VALUE" to a new, dynamically allocated environment array. It also
 * automatically adds an entry for ENV_VAR_FILTER_FILE_NAME pointing to the
 * filter file path itself, so the child can find it.
 *
 * @param filter_filename Path to the file containing variable names (one per line).
 * @param source_env The environment array (e.g., parent's 'environ') to get values from.
 * @return An env_list_t containing the new environment array (NULL-terminated).
 *         The caller is responsible for freeing the list using free_env_list.
 *         Returns a list with vars=NULL on error.
 */
static env_list_t create_filtered_env(const char *filter_filename, char **source_env) {
    env_list_t list = { .vars = NULL, .count = 0, .capacity = 10 }; // Initial capacity
    FILE *file = fopen(filter_filename, "r");
    if (file == NULL) {
        perror("Failed to open environment filter file");
        return list; // Return empty list
    }

    list.vars = malloc(list.capacity * sizeof(char *));
    if (list.vars == NULL) {
        perror("Failed to allocate initial memory for filtered environment");
        fclose(file);
        return list; // Return empty list
    }

    char *line_buf = NULL;
    size_t line_buf_size = 0;
    ssize_t line_len;

    // Read variable names from the filter file
    while ((line_len = getline(&line_buf, &line_buf_size, file)) != -1) {
        // Remove trailing newline character, if present
        if (line_len > 0 && line_buf[line_len - 1] == '\n') {
            line_buf[line_len - 1] = '\0';
            line_len--; // Adjust length
        }

        // Skip empty lines or lines starting with # (optional comment handling)
        if (line_len == 0 || line_buf[0] == '#') {
            continue;
        }

        char *var_name = line_buf;
        char *var_value = find_env_var_value(var_name, source_env); // Use parent's original env

        if (var_value != NULL) {
            // Construct "NAME=VALUE" string
            size_t entry_len = strlen(var_name) + 1 + strlen(var_value) + 1; // name + '=' + value + '\0'
            char *env_entry = malloc(entry_len);
            if (env_entry == NULL) {
                perror("Failed to allocate memory for environment entry");
                // Clean up previously allocated entries before returning error
                free(line_buf);
                fclose(file);
                free_env_list(&list); // Free partially built list
                list.vars = NULL;     // Signal error
                return list;
            }
            snprintf(env_entry, entry_len, "%s=%s", var_name, var_value);

            // Add to list, resize if necessary
            if (list.count >= list.capacity - 1) { // -1 for the eventual NULL terminator
                list.capacity *= 2;
                char **new_vars = realloc(list.vars, list.capacity * sizeof(char *));
                if (new_vars == NULL) {
                    perror("Failed to reallocate memory for filtered environment");
                    free(env_entry); // Free the entry we couldn't add
                    free(line_buf);
                    fclose(file);
                    free_env_list(&list); // Free partially built list
                    list.vars = NULL;     // Signal error
                    return list;
                }
                list.vars = new_vars;
            }
            list.vars[list.count++] = env_entry; // Add the new entry
        } else {
            // Optional: Warn if a requested variable is not found in the source env
            // fprintf(stderr, "Parent: Warning - Variable '%s' from filter file not found in source environment.\n", var_name);
        }
    } // End while getline

    free(line_buf); // Free buffer used by getline
    fclose(file);

    // === Add the filter file path itself to the child's environment ===
    const char *filter_var_name = ENV_VAR_FILTER_FILE_NAME;
    const char *filter_var_value = filter_filename; // Value is the path passed to parent

    size_t filter_entry_len = strlen(filter_var_name) + 1 + strlen(filter_var_value) + 1;
    char *filter_env_entry = malloc(filter_entry_len);
    if (filter_env_entry == NULL) {
        perror("Failed to allocate memory for filter file path env entry");
        free_env_list(&list); // Free partially built list
        list.vars = NULL;     // Signal error
        return list;
    }
    snprintf(filter_env_entry, filter_entry_len, "%s=%s", filter_var_name, filter_var_value);

    // Ensure space for this entry + NULL terminator
    if (list.count >= list.capacity - 1) {
        list.capacity += 2; // Add space for this and NULL
        char **new_vars = realloc(list.vars, list.capacity * sizeof(char *));
        if (new_vars == NULL) {
            perror("Failed to reallocate memory for filter file path env entry");
            free(filter_env_entry);
            free_env_list(&list);
            list.vars = NULL;
            return list;
        }
        list.vars = new_vars;
    }
    list.vars[list.count++] = filter_env_entry;


    // Add NULL terminator to the end of the environment array
    list.vars[list.count] = NULL;

    return list; // Return the completed list
}


/*
 * free_env_list: Frees memory allocated for an environment list.
 *
 * Frees each string within the list's 'vars' array and then frees the
 * 'vars' array itself. Resets count and capacity.
 *
 * @param list Pointer to the env_list_t to free.
 */
static void free_env_list(env_list_t *list) {
    if (list == NULL || list->vars == NULL) {
        return;
    }
    // Free each individual "NAME=VALUE" string
    for (size_t i = 0; i < list->count; ++i) {
        free(list->vars[i]);
        list->vars[i] = NULL; // Avoid double free
    }
    // Free the array of pointers
    free(list->vars);
    list->vars = NULL;
    list->count = 0;
    list->capacity = 0;
}


/*
 * launch_child: Forks and execs a child process.
 *
 * Determines the child executable path based on the method ('+', '*', '&').
 * Creates a filtered environment. Forks the process. The child process
 * executes the child program using execve. The parent process optionally
 * waits for the child (or continues asynchronously).
 *
 * @param method Character indicating how to find CHILD_PATH ('+', '*', '&').
 * @param filter_filename Path to the environment filter file.
 * @param main_envp The environment array passed to parent's main().
 * @return 0 on success, -1 on failure.
 */
static int launch_child(char method, const char *filter_filename, char **main_envp) {
    if (g_child_number >= MAX_CHILDREN) {
        fprintf(stderr, "Parent: Maximum number of children (%d) reached.\n", MAX_CHILDREN);
        return -1;
    }

    // 1. Determine Child Executable Directory (CHILD_PATH)
    char *child_dir = NULL;
    const char *child_path_var_name = "CHILD_PATH";

    switch (method) {
        case '+':
            // Use getenv() which searches the current process environment
            child_dir = getenv(child_path_var_name);
            if (child_dir == NULL) {
                fprintf(stderr, "Parent: Error - CHILD_PATH environment variable not found using getenv() for method '+'.\n");
                return -1;
            }
            break;
        case '*':
            // Scan the envp array passed to main()
            child_dir = find_env_var_value(child_path_var_name, main_envp);
            if (child_dir == NULL) {
                fprintf(stderr, "Parent: Error - CHILD_PATH environment variable not found in main's envp array for method '*'.\n");
                return -1;
            }
            break;
        case '&':
            // Scan the external 'environ' variable
            child_dir = find_env_var_value(child_path_var_name, environ);
            if (child_dir == NULL) {
                fprintf(stderr, "Parent: Error - CHILD_PATH environment variable not found in external 'environ' array for method '&'.\n");
                return -1;
            }
            break;
        default: // Should not happen based on caller logic
            fprintf(stderr, "Parent: Internal error - Invalid launch method '%c'.\n", method);
            return -1;
    }

    if (strlen(child_dir) == 0) {
        fprintf(stderr, "Parent: Error - CHILD_PATH environment variable is empty for method '%c'.\n", method);
        return -1;
    }

    // 2. Construct Full Path to Child Executable
    char child_exec_path[BUFFER_SIZE];
    int path_len = snprintf(child_exec_path, sizeof(child_exec_path), "%s/%s", child_dir, CHILD_EXECUTABLE_NAME);
    if (path_len < 0 || (size_t)path_len >= sizeof(child_exec_path)) {
        fprintf(stderr, "Parent: Error constructing child executable path (path too long or snprintf error).\n");
        return -1;
    }

    // 3. Construct Child's argv[0]
    char child_argv0[32]; // "child_XX" + null terminator
    snprintf(child_argv0, sizeof(child_argv0), "%s_%.2d", CHILD_EXECUTABLE_NAME, g_child_number);

    // Increment child number for the next launch AFTER constructing name
    g_child_number++;

    // 4. Create Filtered Environment for Child
    // Use 'environ' as the source, as it reflects the most current state accessible via getenv,
    // which aligns with how the child will likely access its *own* environment later.
    env_list_t filtered_env_list = create_filtered_env(filter_filename, environ);
    if (filtered_env_list.vars == NULL) {
        fprintf(stderr, "Parent: Failed to create filtered environment for child.\n");
        return -1; // create_filtered_env already printed perror
    }

    printf("Parent: Launching child '%s' using method '%c'...\n", child_argv0, method);
    printf("Parent: Child executable path: %s\n", child_exec_path);

    // 5. Fork Process
    pid_t pid = fork();

    if (pid < 0) {
        // Fork failed
        perror("Parent: fork() failed");
        free_env_list(&filtered_env_list); // Clean up allocated environment
        return -1;
    } else if (pid == 0) {
        // --- Child Process ---
        char *child_argv[] = {child_argv0, NULL}; // argv for the child program

        // Execute the child program
        execve(child_exec_path, child_argv, filtered_env_list.vars);

        // If execve returns, an error occurred
        perror("Child: execve failed");
        fprintf(stderr, "Child: Failed to execute '%s'\n", child_exec_path);
        // IMPORTANT: Use _exit in the child after fork failure to avoid messing with parent's state
        // (e.g., flushing stdio buffers, calling atexit handlers registered by parent)
        free_env_list(&filtered_env_list); // Still attempt cleanup before _exit
        _exit(EXIT_FAILURE);

    } else {
        // --- Parent Process ---
        printf("Parent: Forked child process with PID %d.\n", pid);

        // Clean up the filtered environment allocated in the parent
        // The child process received copies of these strings via execve.
        free_env_list(&filtered_env_list);

        // Parent does not wait for the child, as per requirements (implied by no mention of wait)
        // If using '&', the parent will terminate shortly after this.
        // For '+' and '*', the parent continues its command loop.

        // Optional: Reap zombie processes occasionally to prevent buildup if parent runs long
        // int status;
        // waitpid(-1, &status, WNOHANG); // Check for any terminated children without blocking
    }

    return 0; // Success
}
