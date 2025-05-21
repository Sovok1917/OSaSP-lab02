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
 * - The '&' command launches a child and the parent continues execution.
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
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <locale.h>
#include <limits.h> // For PATH_MAX (though not strictly used, good to be aware of)
#include <stdbool.h>


extern char **environ;


#define MAX_CHILDREN 100
#define PATH_BUFFER_SIZE 4096 // Increased buffer size for paths, consider PATH_MAX
#define CHILD_EXECUTABLE_NAME "child"
#define ENV_VAR_FILTER_FILE_NAME "CHILD_ENV_FILTER_FILE"


typedef struct env_list_s {
    char **vars;
    size_t count;
    size_t capacity;
} env_list_t;


static int g_child_number = 0; // Initialized at load time, effectively runtime for the process

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
 *   6. Exits the loop and terminates if 'q' is entered.
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
    // Explicit runtime initialization for g_child_number (though default 0 is fine)
    // This is more for demonstrating the principle if re-entrancy was a concern.
    g_child_number = 0;

    if (argc != 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    const char *env_filter_file = argv[1];


    if (printf("Parent PID: %d\n", getpid()) < 0) {
        perror("Parent: printf failed");
        // Continue if possible, or decide to exit
    }
    if (printf("Initial environment variables (sorted LC_COLLATE=C):\n") < 0) {
        perror("Parent: printf failed");
    }


    int env_count = 0;
    for (char **env = envp; *env != NULL; ++env) {
        env_count++;
    }


    char **sorted_envp = malloc((size_t)env_count * sizeof(char *));
    if (sorted_envp == NULL) {
        perror("Parent: Failed to allocate memory for environment sorting");
        // abort() is for non-interactive, here we return EXIT_FAILURE
        return EXIT_FAILURE;
    }
    for (int i = 0; i < env_count; ++i) {
        sorted_envp[i] = envp[i]; // Shallow copy, strings are from original envp
    }


    // Set locale for sorting. Store original locale if needed for restoration.
    // char *original_locale = setlocale(LC_COLLATE, NULL); // To query current
    if (setlocale(LC_COLLATE, "C") == NULL) {
        fprintf(stderr, "Parent: Warning - Failed to set LC_COLLATE to C. Sorting might be incorrect.\n");
        // Proceed with default locale sorting if "C" fails.
    }


    qsort(sorted_envp, (size_t)env_count, sizeof(char *), compare_env_vars);


    for (int i = 0; i < env_count; ++i) {
        if (printf("%s\n", sorted_envp[i]) < 0) {
            perror("Parent: Failed to print environment variable");
            // Consider breaking or returning, depending on severity
        }
    }
    if (printf("----------------------------------------\n") < 0) {
        perror("Parent: printf failed");
    }

    free(sorted_envp); // Free the array of pointers, not the strings themselves.
    sorted_envp = NULL;


    if (printf("Enter command (+, *, & to launch child, q to quit):\n> ") < 0) {
        perror("Parent: printf failed");
    }
    if (fflush(stdout) == EOF) {
        perror("Parent: fflush stdout failed");
    }

    int command_char;
    bool terminate_parent = false;

    while (!terminate_parent && (command_char = getchar()) != EOF) {
        // Consume trailing newline and other whitespace characters from the input buffer
        if (command_char == '\n') {
            if (printf("> ") < 0) perror("Parent: printf failed");
            if (fflush(stdout) == EOF) perror("Parent: fflush stdout failed");
            continue;
        }

        int ch_consume;
        while ((ch_consume = getchar()) != '\n' && ch_consume != EOF);


        switch (command_char) {
            case '+':
            case '*':
            case '&':
                if (launch_child((char)command_char, env_filter_file, envp) != 0) {
                    fprintf(stderr, "Parent: Failed to launch child process for command '%c'.\n", command_char);
                }
                // For '&', parent continues without setting terminate_parent to true.
                if (command_char == '&') {
                    printf("Parent: Launched child via '&', parent continues.\n");
                }
                break;
            case 'q':
            case 'Q':
                printf("Parent: Quit command received. Exiting.\n");
                terminate_parent = true;
                break;
            default:
                printf("Parent: Unknown command '%c'. Use +, *, &, or q.\n", command_char);
                break;
        }

        if (!terminate_parent) {
            if (printf("> ") < 0) perror("Parent: printf failed");
            if (fflush(stdout) == EOF) perror("Parent: fflush stdout failed");
        }
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
    fprintf(stderr, "Usage: %s <environment_filter_file>\n", prog_name ? prog_name : "parent");
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
    // Directly casting to const char ** as qsort passes pointers to elements
    const char *str_a = *(const char **)a;
    const char *str_b = *(const char **)b;
    return strcoll(str_a, str_b);
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
        if (strncmp(*env, var_name, name_len) == 0 && (*env)[name_len] == '=') {
            return (*env) + name_len + 1; // Point to the character after '='
        }
    }
    return NULL;
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
 *   list will have `list.vars` set to NULL and `list.count` to 0.
 *   An error message is printed to stderr.
 */
static env_list_t create_filtered_env(const char *filter_filename, char **source_env) {
    env_list_t list = { .vars = NULL, .count = 0, .capacity = 10 }; // Initial capacity
    FILE *file = fopen(filter_filename, "r");
    if (file == NULL) {
        perror("Parent: Failed to open environment filter file");
        return list; // list.vars is NULL
    }

    list.vars = malloc(list.capacity * sizeof(char *));
    if (list.vars == NULL) {
        perror("Parent: Failed to allocate initial memory for filtered environment");
        fclose(file);
        // abort(); // For non-interactive. Here, return an empty list.
        return list; // list.vars is NULL
    }

    char *line_buf = NULL;
    size_t line_buf_size = 0;
    ssize_t line_len;

    // Read variables from filter file
    while ((line_len = getline(&line_buf, &line_buf_size, file)) != -1) {
        if (line_len > 0 && line_buf[line_len - 1] == '\n') {
            line_buf[line_len - 1] = '\0'; // Remove newline
            line_len--;
        }

        if (line_len == 0 || line_buf[0] == '#') { // Skip empty lines and comments
            continue;
        }

        char *var_name = line_buf;
        char *var_value = find_env_var_value(var_name, source_env);

        if (var_value != NULL) {
            size_t entry_len = strlen(var_name) + 1 + strlen(var_value) + 1; // NAME=VALUE\0
            char *env_entry = malloc(entry_len);
            if (env_entry == NULL) {
                perror("Parent: Failed to allocate memory for environment entry");
                // abort(); // Or cleanup and return error
                free(line_buf); // getline buffer
                fclose(file);
                free_env_list(&list); // Free previously allocated entries
                list.vars = NULL; list.count = 0; // Mark as error
                return list;
            }
            // snprintf is safer than sprintf
            if (snprintf(env_entry, entry_len, "%s=%s", var_name, var_value) < 0) {
                perror("Parent: snprintf failed for env_entry");
                free(env_entry);
                // Continue or handle error
            }


            if (list.count >= list.capacity -1) { // -1 for the NULL terminator
                size_t new_capacity = list.capacity == 0 ? 10 : list.capacity * 2;
                char **new_vars = realloc(list.vars, new_capacity * sizeof(char *));
                if (new_vars == NULL) {
                    perror("Parent: Failed to reallocate memory for filtered environment");
                    // abort(); // Or cleanup and return error
                    free(env_entry); // Current entry not added yet
                    free(line_buf);
                    fclose(file);
                    free_env_list(&list);
                    list.vars = NULL; list.count = 0;
                    return list;
                }
                list.vars = new_vars;
                list.capacity = new_capacity;
            }
            list.vars[list.count++] = env_entry;
        }
    }
    free(line_buf); // Free buffer used by getline
    line_buf = NULL;

    if (ferror(file)) {
        perror("Parent: Error reading from filter file");
        // Potentially cleanup and return error
    }
    fclose(file);

    // Add the CHILD_ENV_FILTER_FILE_NAME variable itself
    const char *filter_var_name = ENV_VAR_FILTER_FILE_NAME;
    // filter_filename is const char*, safe to use directly
    const char *filter_var_value = filter_filename;

    size_t filter_entry_len = strlen(filter_var_name) + 1 + strlen(filter_var_value) + 1;
    char *filter_env_entry = malloc(filter_entry_len);
    if (filter_env_entry == NULL) {
        perror("Parent: Failed to allocate memory for filter file path env entry");
        // abort();
        free_env_list(&list);
        list.vars = NULL; list.count = 0;
        return list;
    }
    if (snprintf(filter_env_entry, filter_entry_len, "%s=%s", filter_var_name, filter_var_value) < 0) {
        perror("Parent: snprintf failed for filter_env_entry");
        free(filter_env_entry);
        // Handle error
    }


    if (list.count >= list.capacity - 1) { // Check again for space for this new entry + NULL
        size_t new_capacity = list.capacity + 2; // Ensure space for one more and NULL
        char **new_vars = realloc(list.vars, new_capacity * sizeof(char *));
        if (new_vars == NULL) {
            perror("Parent: Failed to reallocate for filter file path env entry");
            // abort();
            free(filter_env_entry);
            free_env_list(&list);
            list.vars = NULL; list.count = 0;
            return list;
        }
        list.vars = new_vars;
        list.capacity = new_capacity;
    }
    list.vars[list.count++] = filter_env_entry;
    list.vars[list.count] = NULL; // Null-terminate the array

    return list;
}


/*
 * Purpose:
 *   Frees all memory associated with an env_list_t structure. This includes
 *   freeing each individual "NAME=VALUE" string stored within the list's 'vars'
 *   array and then freeing the 'vars' array itself. It also resets the list's
 *   count and capacity members to prevent dangling pointer issues if reused.
 * Receives:
 *   list: A pointer to the env_list_t structure to be freed. Handles NULL list
 *         or list with NULL 'vars' pointer gracefully.
 * Returns:
 *   None (void).
 */
static void free_env_list(env_list_t *list) {
    if (list == NULL || list->vars == NULL) {
        if (list) { // Ensure members are zeroed even if list->vars was NULL
            list->count = 0;
            list->capacity = 0;
        }
        return;
    }

    for (size_t i = 0; i < list->count; ++i) {
        free(list->vars[i]);
        list->vars[i] = NULL; // Good practice: defensive against double-free
    }

    free(list->vars);
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

    char *child_dir = NULL;
    const char *child_path_var_name = "CHILD_PATH";

    switch (method) {
        case '+': child_dir = getenv(child_path_var_name); break;
        case '*': child_dir = find_env_var_value(child_path_var_name, main_envp); break;
        case '&': child_dir = find_env_var_value(child_path_var_name, environ); break;
        default:
            fprintf(stderr, "Parent: Internal error - Invalid launch method '%c'.\n", method);
            return -1; // Should not happen if called correctly
    }

    if (child_dir == NULL) {
        fprintf(stderr, "Parent: Error - CHILD_PATH environment variable not found using method '%c'.\n", method);
        return -1;
    }
    if (strlen(child_dir) == 0) {
        fprintf(stderr, "Parent: Error - CHILD_PATH environment variable is empty for method '%c'.\n", method);
        return -1;
    }

    char child_exec_path[PATH_BUFFER_SIZE];
    // Using snprintf to prevent buffer overflows
    int path_len = snprintf(child_exec_path, sizeof(child_exec_path), "%s/%s", child_dir, CHILD_EXECUTABLE_NAME);
    if (path_len < 0 || (size_t)path_len >= sizeof(child_exec_path)) {
        fprintf(stderr, "Parent: Error constructing child executable path (too long or snprintf error).\n");
        return -1;
    }

    char child_argv0[32]; // "child_XX\0" is small
    // snprintf for argv[0]
    if (snprintf(child_argv0, sizeof(child_argv0), "%s_%.2d", CHILD_EXECUTABLE_NAME, g_child_number) < 0) {
        perror("Parent: snprintf failed for child_argv0");
        return -1; // Or handle differently
    }
    // Increment only after successful setup before fork, or ensure it's correctly managed on failure.
    // Here, we increment optimistically. If fork fails, it's a skipped number.


    // Create filtered environment using parent's full 'environ' as the source
    env_list_t filtered_env_list = create_filtered_env(filter_filename, environ);
    if (filtered_env_list.vars == NULL) {
        fprintf(stderr, "Parent: Failed to create filtered environment for child.\n");
        // No g_child_number increment needed if we fail before fork
        return -1;
    }

    if (printf("Parent: Launching child '%s' using method '%c'...\n", child_argv0, method) < 0) {
        perror("Parent: printf failed");
    }
    if (printf("Parent: Child executable path: %s\n", child_exec_path) < 0) {
        perror("Parent: printf failed");
    }
    if (fflush(stdout) == EOF) {
        perror("Parent: fflush stdout failed");
    }

    pid_t pid = fork();

    if (pid < 0) { // Fork failed
        perror("Parent: fork() failed");
        free_env_list(&filtered_env_list); // Clean up allocated environment
        return -1;
    } else if (pid == 0) { // Child process
        // Child process should not increment g_child_number
        char *child_argv[] = {child_argv0, NULL};

        // execve replaces the current process image
        // On success, this code is no longer running.
        // On failure, execve returns -1.
        execve(child_exec_path, child_argv, filtered_env_list.vars);

        // If execve returns, it must have failed.
        perror("Child (execve failed)"); // Report error from child's perspective
        fprintf(stderr, "Child: Failed attempt to execute '%s' as '%s'\n", child_exec_path, child_argv0);

        free_env_list(&filtered_env_list); // Crucial: free memory before exiting child on error
        _exit(EXIT_FAILURE); // Use _exit in child after fork to avoid stdio buffer issues etc.
    } else { // Parent process
        g_child_number++; // Increment child number in parent after successful fork
        if (printf("Parent: Forked child process '%s' with PID %d.\n", child_argv0, pid) < 0) {
            perror("Parent: printf failed");
        }
        if (fflush(stdout) == EOF) {
            perror("Parent: fflush stdout failed");
        }
        // Parent must free its copy of the filtered_env_list.
        // The child received its own copy through execve (kernel copies it).
        free_env_list(&filtered_env_list);

        // Parent does not wait for the child, as per typical behavior for such launchers.
        // If waiting is desired, waitpid(pid, &status, 0) would be used.
    }
    return 0; // Success
}
