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
#include <limits.h>
#include <stdbool.h>
#include <signal.h> // Required for signal handling


extern char **environ;


#define MAX_CHILDREN 100
#define PATH_BUFFER_SIZE 4096
#define CHILD_EXECUTABLE_NAME "child"
#define ENV_VAR_FILTER_FILE_NAME "CHILD_ENV_FILTER_FILE"


typedef struct env_list_s {
    char **vars;
    size_t count;
    size_t capacity;
} env_list_t;


static int g_child_number;
static volatile sig_atomic_t signal_flag = 0; // Flag to indicate a signal was received

/* --- Function Prototypes --- */

static int compare_env_vars(const void *a, const void *b);
static char *find_env_var_value(const char *var_name, char **env_array);
static env_list_t create_filtered_env(const char *filter_filename, char **source_env);
static void free_env_list(env_list_t *list);
static int launch_child(char method, const char *filter_filename, char **main_envp);
static void print_usage(const char *prog_name);
static void handle_interrupt_signal(int signum);

/*
 * Purpose:
 *   Signal handler for SIGINT and SIGTERM. Sets a global flag.
 * Receives:
 *   signum: The signal number that was caught.
 * Returns:
 *   None (void).
 */
static void handle_interrupt_signal(int signum) {
    // This assignment is async-signal-safe
    signal_flag = signum;
}


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
 *   6. Exits the loop and terminates if 'q' is entered or a signal is caught.
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
    g_child_number = 0;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_interrupt_signal;
    sigemptyset(&sa.sa_mask);
    // sa.sa_flags = SA_RESTART; // We want syscalls like read() to be interrupted
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("Parent: Failed to register SIGINT handler");
        // Non-fatal, but signal handling for SIGINT won't work
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("Parent: Failed to register SIGTERM handler");
        // Non-fatal, but signal handling for SIGTERM won't work
    }


    if (argc != 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    const char *env_filter_file = argv[1];


    if (printf("Parent PID: %d\n", getpid()) < 0) {
        perror("Parent: printf failed for PID");
    }
    if (printf("Initial environment variables (sorted LC_COLLATE=C):\n") < 0) {
        perror("Parent: printf failed for env header");
    }


    int env_count = 0;
    for (char **env = envp; *env != NULL; ++env) {
        env_count++;
    }


    char **sorted_envp = NULL;
    if (env_count > 0) {
        sorted_envp = malloc((size_t)env_count * sizeof(char *));
        if (sorted_envp == NULL) {
            perror("Parent: Failed to allocate memory for environment sorting");
            return EXIT_FAILURE;
        }
        for (int i = 0; i < env_count; ++i) {
            sorted_envp[i] = envp[i];
        }

        if (setlocale(LC_COLLATE, "C") == NULL) {
            fprintf(stderr, "Parent: Warning - Failed to set LC_COLLATE to C. Sorting might be incorrect.\n");
        }

        qsort(sorted_envp, (size_t)env_count, sizeof(char *), compare_env_vars);

        for (int i = 0; i < env_count; ++i) {
            if (printf("%s\n", sorted_envp[i]) < 0) {
                perror("Parent: Failed to print environment variable");
                // Consider breaking or returning
            }
        }
        free(sorted_envp);
        sorted_envp = NULL;
    } else {
        if (printf("(No environment variables found or envp is empty)\n") < 0) {
            perror("Parent: printf failed for no env message");
        }
    }


    if (printf("----------------------------------------\n") < 0) {
        perror("Parent: printf failed for separator");
    }

    int command_char;
    bool terminate_parent = false;

    while (!terminate_parent) {
        if (signal_flag != 0) {
            // A signal was caught, print message and prepare to exit.
            // Using write for signal-safety if this were in the handler,
            // but here in the main loop, printf is generally okay.
            fprintf(stdout, "\nParent: Signal %d received. Exiting gracefully.\n", signal_flag);
            fflush(stdout); // Ensure message is printed
            terminate_parent = true;
            break;
        }

        if (printf("Enter command (+, *, & to launch child, q to quit):\n> ") < 0) {
            perror("Parent: printf failed for prompt");
            if (signal_flag != 0) continue; // If signal came during printf, re-check
            break;
        }
        if (fflush(stdout) == EOF) {
            perror("Parent: fflush stdout failed for prompt");
            if (signal_flag != 0) continue;
            break;
        }

        command_char = getchar();

        if (signal_flag != 0) { // Check flag immediately after getchar returns
            fprintf(stdout, "\nParent: Signal %d received during input. Exiting gracefully.\n", signal_flag);
            fflush(stdout);
            terminate_parent = true;
            if (command_char == EOF && errno == EINTR) { // If getchar was interrupted
                clearerr(stdin); // Clear error state on stdin
            }
            break;
        }

        if (command_char == EOF) {
            if (feof(stdin)) {
                if(printf("\nParent: EOF detected on stdin. Exiting.\n") < 0) {
                    perror("Parent: printf failed for EOF message");
                }
                terminate_parent = true;
            } else if (ferror(stdin)) {
                if (errno == EINTR) {
                    // This case should ideally be caught by signal_flag check above
                    // if the EINTR was due to SIGINT/SIGTERM we handle.
                    // If it's another signal that interrupted getchar, we just retry.
                    clearerr(stdin);
                    continue;
                }
                perror("Parent: Error reading from stdin");
                terminate_parent = true;
            }
            break;
        }

        if (command_char == '\n') {
            continue;
        }

        int ch_consume;
        // Consume the rest of the line
        while ((ch_consume = getchar()) != '\n' && ch_consume != EOF) {
            if (signal_flag != 0) break; // Check during consumption too
        }

        if (signal_flag != 0) { // Check again after consuming line
            fprintf(stdout, "\nParent: Signal %d received during input processing. Exiting gracefully.\n", signal_flag);
            fflush(stdout);
            terminate_parent = true;
            if (ch_consume == EOF && errno == EINTR) { // If the consuming getchar was interrupted
                clearerr(stdin);
            }
            break;
        }
        if (ch_consume == EOF && !feof(stdin) && errno == EINTR) { // Interrupted while consuming
            clearerr(stdin);
            continue;
        }
        if (ch_consume == EOF && feof(stdin)) { // EOF while consuming
            if(printf("\nParent: EOF detected while consuming input. Exiting.\n") < 0) {
                perror("Parent: printf failed for EOF message");
            }
            terminate_parent = true;
            break;
        }


        switch (command_char) {
            case '+':
            case '*':
            case '&':
                if (launch_child((char)command_char, env_filter_file, envp) != 0) {
                    fprintf(stderr, "Parent: Failed to launch child process for command '%c'.\n", command_char);
                }
                if (command_char == '&') {
                    if(printf("Parent: Launched child via '&', parent continues.\n") < 0) {
                        perror("Parent: printf failed for '&' confirmation");
                    }
                }
                break;
            case 'q':
            case 'Q':
                if(printf("Parent: Quit command received. Exiting.\n") < 0) {
                    perror("Parent: printf failed for quit message");
                }
                terminate_parent = true;
                break;
            default:
                if(printf("Parent: Unknown command '%c'. Use +, *, &, or q.\n", command_char) < 0) {
                    perror("Parent: printf failed for unknown command");
                }
                break;
        }
    } // end while(!terminate_parent)

    if(printf("Parent: Exiting cleanly.\n") < 0) {
        perror("Parent: printf failed for exit message");
    }
    // Normal return from main will trigger atexit handlers, including stdio cleanup.
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
            return (*env) + name_len + 1;
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
    env_list_t list = { .vars = NULL, .count = 0, .capacity = 10 };
    FILE *file = fopen(filter_filename, "r");
    if (file == NULL) {
        perror("Parent: Failed to open environment filter file");
        return list;
    }

    list.vars = malloc(list.capacity * sizeof(char *));
    if (list.vars == NULL) {
        perror("Parent: Failed to allocate initial memory for filtered environment");
        if (fclose(file) != 0) perror("Parent: fclose failed in create_filtered_env error path");
        return list;
    }

    char *line_buf = NULL;
    size_t line_buf_size = 0;
    ssize_t line_len;

    while ((line_len = getline(&line_buf, &line_buf_size, file)) != -1) {
        if (signal_flag != 0) { // Check for signal during file processing
            fprintf(stderr, "Parent: Signal received during environment creation. Aborting creation.\n");
            free(line_buf);
            if (fclose(file) != 0) perror("Parent: fclose failed in create_filtered_env signal path");
            free_env_list(&list);
            list.vars = NULL; list.count = 0;
            return list;
        }

        if (line_len > 0 && line_buf[line_len - 1] == '\n') {
            line_buf[line_len - 1] = '\0';
            line_len--;
        }

        if (line_len == 0 || line_buf[0] == '#') {
            continue;
        }

        char *var_name = line_buf;
        char *var_value = find_env_var_value(var_name, source_env);

        if (var_value != NULL) {
            size_t name_len_val = strlen(var_name);
            size_t value_len_val = strlen(var_value);
            size_t entry_len = name_len_val + 1 + value_len_val + 1;
            char *env_entry = malloc(entry_len);
            if (env_entry == NULL) {
                perror("Parent: Failed to allocate memory for environment entry");
                free(line_buf);
                if (fclose(file) != 0) perror("Parent: fclose failed in create_filtered_env error path");
                free_env_list(&list);
                list.vars = NULL; list.count = 0;
                return list;
            }

            int written = snprintf(env_entry, entry_len, "%s=%s", var_name, var_value);
            if (written < 0 || (size_t)written >= entry_len) {
                fprintf(stderr, "Parent: snprintf error or truncation for env_entry '%s'. Skipping.\n", var_name);
                free(env_entry);
                continue;
            }

            if (list.count >= list.capacity - 1) {
                size_t new_capacity = list.capacity == 0 ? 10 : list.capacity * 2;
                char **new_vars = realloc(list.vars, new_capacity * sizeof(char *));
                if (new_vars == NULL) {
                    perror("Parent: Failed to reallocate memory for filtered environment");
                    free(env_entry);
                    free(line_buf);
                    if (fclose(file) != 0) perror("Parent: fclose failed in create_filtered_env error path");
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
    free(line_buf);
    line_buf = NULL;

    if (ferror(file)) {
        perror("Parent: Error reading from filter file");
    }
    if (fclose(file) != 0) {
        perror("Parent: fclose failed for filter file");
    }

    if (signal_flag != 0) { // Check again before adding the final entry
        fprintf(stderr, "Parent: Signal received before finalizing environment. Aborting.\n");
        free_env_list(&list);
        list.vars = NULL; list.count = 0;
        return list;
    }

    const char *filter_var_name = ENV_VAR_FILTER_FILE_NAME;
    const char *filter_var_value = filter_filename;

    size_t filter_name_len = strlen(filter_var_name);
    size_t filter_value_len = strlen(filter_var_value);
    size_t filter_entry_len = filter_name_len + 1 + filter_value_len + 1;
    char *filter_env_entry = malloc(filter_entry_len);

    if (filter_env_entry == NULL) {
        perror("Parent: Failed to allocate memory for filter file path env entry");
        free_env_list(&list);
        list.vars = NULL; list.count = 0;
        return list;
    }

    int written_filter = snprintf(filter_env_entry, filter_entry_len, "%s=%s", filter_var_name, filter_var_value);
    if (written_filter < 0 || (size_t)written_filter >= filter_entry_len) {
        fprintf(stderr, "Parent: snprintf error or truncation for %s. Critical failure.\n", ENV_VAR_FILTER_FILE_NAME);
        free(filter_env_entry);
        free_env_list(&list);
        list.vars = NULL; list.count = 0;
        return list;
    }

    if (list.count >= list.capacity - 1) {
        size_t new_capacity = list.capacity + 2;
        char **new_vars = realloc(list.vars, new_capacity * sizeof(char *));
        if (new_vars == NULL) {
            perror("Parent: Failed to reallocate for filter file path env entry");
            free(filter_env_entry);
            free_env_list(&list);
            list.vars = NULL; list.count = 0;
            return list;
        }
        list.vars = new_vars;
        list.capacity = new_capacity;
    }
    list.vars[list.count++] = filter_env_entry;
    list.vars[list.count] = NULL;

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
    if (list == NULL) {
        return;
    }
    if (list->vars != NULL) {
        for (size_t i = 0; i < list->count; ++i) {
            free(list->vars[i]);
            list->vars[i] = NULL;
        }
        free(list->vars);
    }
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
    if (signal_flag != 0) { // Check for signal before launching
        fprintf(stderr, "Parent: Signal received, aborting child launch.\n");
        return -1;
    }

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

    char child_exec_path[PATH_BUFFER_SIZE];
    int path_len = snprintf(child_exec_path, sizeof(child_exec_path), "%s/%s", child_dir, CHILD_EXECUTABLE_NAME);
    if (path_len < 0 || (size_t)path_len >= sizeof(child_exec_path)) {
        fprintf(stderr, "Parent: Error constructing child executable path (too long or snprintf error).\n");
        return -1;
    }

    char child_argv0[32];
    int argv0_len = snprintf(child_argv0, sizeof(child_argv0), "%s_%.2d", CHILD_EXECUTABLE_NAME, g_child_number);
    if (argv0_len < 0 || (size_t)argv0_len >= sizeof(child_argv0)) {
        perror("Parent: snprintf failed or truncated for child_argv0");
        return -1;
    }

    env_list_t filtered_env_list = create_filtered_env(filter_filename, environ);
    if (filtered_env_list.vars == NULL) {
        // create_filtered_env might have returned early due to a signal.
        // The signal_flag should already be set if that's the case.
        if (signal_flag == 0) { // If not due to signal, it's another error
            fprintf(stderr, "Parent: Failed to create filtered environment for child.\n");
        }
        return -1;
    }
    if (signal_flag != 0) { // Double check if signal occurred during create_filtered_env
        fprintf(stderr, "Parent: Signal received during child setup, aborting launch.\n");
        free_env_list(&filtered_env_list);
        return -1;
    }


    if (printf("Parent: Launching child '%s' using method '%c'...\n", child_argv0, method) < 0) {
        perror("Parent: printf failed for launch message");
    }
    if (printf("Parent: Child executable path: %s\n", child_exec_path) < 0) {
        perror("Parent: printf failed for exec path message");
    }
    if (fflush(stdout) == EOF) {
        perror("Parent: fflush stdout failed before fork");
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("Parent: fork() failed");
        free_env_list(&filtered_env_list);
        return -1;
    } else if (pid == 0) {
        // Child process: Restore default signal handlers for exec'd program
        struct sigaction sa_default;
        memset(&sa_default, 0, sizeof(sa_default));
        sa_default.sa_handler = SIG_DFL;
        sigaction(SIGINT, &sa_default, NULL);
        sigaction(SIGTERM, &sa_default, NULL);

        char *child_argv[] = {child_argv0, NULL};
        execve(child_exec_path, child_argv, filtered_env_list.vars);

        perror("Child (execve failed)");
        fprintf(stderr, "Child: Failed attempt to execute '%s' as '%s'\n", child_exec_path, child_argv0);
        free_env_list(&filtered_env_list);
        _exit(EXIT_FAILURE);
    } else {
        g_child_number++;
        if (printf("Parent: Forked child process '%s' with PID %d.\n", child_argv0, pid) < 0) {
            perror("Parent: printf failed for fork success message");
        }
        if (fflush(stdout) == EOF) {
            perror("Parent: fflush stdout failed after fork");
        }
        free_env_list(&filtered_env_list);
    }
    return 0;
}
