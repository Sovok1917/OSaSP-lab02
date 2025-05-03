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
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <locale.h>
#include <limits.h>
#include <stdbool.h>


extern char **environ;


#define MAX_CHILDREN 100
#define BUFFER_SIZE 1024
#define CHILD_EXECUTABLE_NAME "child"
#define ENV_VAR_FILTER_FILE_NAME "CHILD_ENV_FILTER_FILE"


typedef struct {
    char **vars;
    size_t count;
    size_t capacity;
} env_list_t;


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

    if (argc != 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    const char *env_filter_file = argv[1];


    printf("Parent PID: %d\n", getpid());
    printf("Initial environment variables (sorted LC_COLLATE=C):\n");


    int env_count = 0;
    for (char **env = envp; *env != NULL; ++env) {
        env_count++;
    }


    char **sorted_envp = malloc(env_count * sizeof(char *));
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


    qsort(sorted_envp, env_count, sizeof(char *), compare_env_vars);


    for (int i = 0; i < env_count; ++i) {
        if (printf("%s\n", sorted_envp[i]) < 0) {
            perror("Parent: Failed to print environment variable");
            free(sorted_envp);
            return EXIT_FAILURE;
        }
    }
    printf("----------------------------------------\n");

    free(sorted_envp);


    printf("Enter command (+, *, & to launch child, q to quit):\n> ");
    fflush(stdout);

    int command_char;
    bool terminate_parent = false;

    while ((command_char = getchar()) != EOF) {

        if (command_char == '\n' || command_char == ' ' || command_char == '\t') {
            continue;
        }


        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF);

        switch (command_char) {
            case '+':
            case '*':
            case '&':
                if (launch_child((char)command_char, env_filter_file, envp) != 0) {
                    fprintf(stderr, "Parent: Failed to launch child process for command '%c'.\n", command_char);

                }
                if (command_char == '&') {
                    printf("Parent: Initiating termination after launching child via '&'.\n");
                    terminate_parent = true;
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

        if (terminate_parent) {
            break;
        }

        printf("> ");
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

    const char **str_a = (const char **)a;
    const char **str_b = (const char **)b;


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
 *   list will have `list.vars` set to NULL. An error message is printed to stderr.
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
        fclose(file);
        return list;
    }

    char *line_buf = NULL;
    size_t line_buf_size = 0;
    ssize_t line_len;


    while ((line_len = getline(&line_buf, &line_buf_size, file)) != -1) {

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

            size_t entry_len = strlen(var_name) + 1 + strlen(var_value) + 1;
            char *env_entry = malloc(entry_len);
            if (env_entry == NULL) {
                perror("Parent: Failed to allocate memory for environment entry");
                free(line_buf);
                fclose(file);
                free_env_list(&list);
                list.vars = NULL;
                return list;
            }
            snprintf(env_entry, entry_len, "%s=%s", var_name, var_value);


            if (list.count >= list.capacity - 1) {
                size_t new_capacity = list.capacity * 2;
                char **new_vars = realloc(list.vars, new_capacity * sizeof(char *));
                if (new_vars == NULL) {
                    perror("Parent: Failed to reallocate memory for filtered environment");
                    free(env_entry);
                    free(line_buf);
                    fclose(file);
                    free_env_list(&list);
                    list.vars = NULL;
                    return list;
                }
                list.vars = new_vars;
                list.capacity = new_capacity;
            }

            list.vars[list.count++] = env_entry;
        }

    }

    free(line_buf);
    fclose(file);


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


    if (list.count >= list.capacity - 1) {
        size_t new_capacity = list.capacity + 2;
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

    list.vars[list.count++] = filter_env_entry;


    list.vars[list.count] = NULL;

    return list;
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
        return;
    }

    for (size_t i = 0; i < list->count; ++i) {
        free(list->vars[i]);
        list->vars[i] = NULL;
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


    char child_exec_path[BUFFER_SIZE];
    int path_len = snprintf(child_exec_path, sizeof(child_exec_path), "%s/%s", child_dir, CHILD_EXECUTABLE_NAME);
    if (path_len < 0 || (size_t)path_len >= sizeof(child_exec_path)) {
        fprintf(stderr, "Parent: Error constructing child executable path (too long or snprintf error).\n");
        return -1;
    }


    char child_argv0[32];
    snprintf(child_argv0, sizeof(child_argv0), "%s_%.2d", CHILD_EXECUTABLE_NAME, g_child_number);
    g_child_number++;




    env_list_t filtered_env_list = create_filtered_env(filter_filename, environ);
    if (filtered_env_list.vars == NULL) {
        fprintf(stderr, "Parent: Failed to create filtered environment for child.\n");

        return -1;
    }

    printf("Parent: Launching child '%s' using method '%c'...\n", child_argv0, method);
    printf("Parent: Child executable path: %s\n", child_exec_path);
    fflush(stdout);


    pid_t pid = fork();

    if (pid < 0) {

        perror("Parent: fork() failed");
        free_env_list(&filtered_env_list);
        return -1;

    } else if (pid == 0) {

        char *child_argv[] = {child_argv0, NULL};


        execve(child_exec_path, child_argv, filtered_env_list.vars);


        perror("Child (in parent context before exec): execve failed");
        fprintf(stderr, "Child (in parent context before exec): Failed attempt to execute '%s'\n", child_exec_path);




        free_env_list(&filtered_env_list);
        _exit(EXIT_FAILURE);

    } else {

        printf("Parent: Forked child process with PID %d.\n", pid);
        fflush(stdout);



        free_env_list(&filtered_env_list);







    }

    return 0;
}
