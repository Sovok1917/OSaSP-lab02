/*
 * child.c
 *
 * Description:
 * This program acts as the child process launched by 'parent.c'.
 * It prints its own identity (program name, PID, PPID). It then retrieves
 * the path to an environment variable filter file from the specific environment
 * it received via execve (passed in 'envp'). It reads variable names listed
 * in that filter file and prints the corresponding values found within its
 * received environment.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     // getpid, getppid
#include <errno.h>      // errno

// Name of the environment variable that stores the path to the filter file.
// The parent process sets this in the child's environment.
#define ENV_VAR_FILTER_FILE_NAME "CHILD_ENV_FILTER_FILE"

/* --- Function Prototypes --- */

static char *find_env_var_value_in_array(const char *var_name, char **env_array);

/*
 * Purpose:
 *   The main entry point for the child process. It performs the following steps:
 *   1. Prints its program name, process ID (PID), and parent process ID (PPID).
 *   2. Retrieves the path of the environment filter file from the environment
 *      array ('envp') passed to it by the parent during execve.
 *   3. Opens and reads the specified filter file line by line.
 *   4. For each line (interpreted as an environment variable name), it looks up
 *      that variable's value within the received 'envp' array.
 *   5. Prints the variable name and its corresponding value (or indicates if not found).
 * Receives:
 *   argc: The number of command-line arguments (expected to be 1, the program name).
 *   argv: An array of command-line argument strings. argv[0] contains the name
 *         used when launching the child (e.g., "child_00").
 *   envp: An array of strings representing the environment variables passed to
 *         this process by execve. This is a NULL-terminated array of "NAME=VALUE" strings.
 * Returns:
 *   EXIT_SUCCESS (0) if the program completes successfully.
 *   EXIT_FAILURE (1) if any critical error occurs (e.g., cannot find the filter
 *   file variable, cannot open the filter file, critical I/O error).
 */
int main(int argc, char *argv[], char **envp) {
    const char *program_name = (argc > 0 && argv[0] != NULL) ? argv[0] : "child (unknown name)";
    pid_t pid = getpid();
    pid_t ppid = getppid();

    // Print identity information.
    if (printf("Child: Name='%s', PID=%d, PPID=%d\n", program_name, pid, ppid) < 0) {
        perror("Child: Failed to print identity");
        return EXIT_FAILURE;
    }
    fflush(stdout);

    // Get the path to the filter file from the received environment (envp).
    // It is crucial to use envp here, not getenv(), to inspect the specific
    // environment provided by the parent via execve.
    const char *filter_filename = find_env_var_value_in_array(ENV_VAR_FILTER_FILE_NAME, envp);

    if (filter_filename == NULL) {
        fprintf(stderr, "Child (%s, %d): Error - Environment variable '%s' not found in received environment.\n",
                program_name, pid, ENV_VAR_FILTER_FILE_NAME);
        return EXIT_FAILURE;
    }

    if (printf("Child: Using environment filter file: %s\n", filter_filename) < 0) {
        perror("Child: Failed to print filter filename");
        // Non-fatal, attempt to continue.
    }
    fflush(stdout);

    // Open the filter file for reading.
    FILE *file = fopen(filter_filename, "r");
    if (file == NULL) {
        fprintf(stderr, "Child (%s, %d): Error - ", program_name, pid);
        perror("Failed to open environment filter file");
        return EXIT_FAILURE;
    }

    printf("Child: Received Environment Variables (from filter list):\n");

    char *line_buf = NULL;
    size_t line_buf_size = 0;
    ssize_t line_len;

    // Read variable names from the filter file, one per line.
    while ((line_len = getline(&line_buf, &line_buf_size, file)) != -1) {
        // Remove trailing newline, if present.
        if (line_len > 0 && line_buf[line_len - 1] == '\n') {
            line_buf[line_len - 1] = '\0';
            line_len--;
        }

        // Ignore empty lines or comment lines.
        if (line_len == 0 || line_buf[0] == '#') {
            continue;
        }

        char *var_name = line_buf;
        // Look up the value in the received environment array (envp).
        char *var_value = find_env_var_value_in_array(var_name, envp);

        // Print the variable and its value (or indicate if not found).
        if (printf("  %s=%s\n", var_name, var_value ? var_value : "(Not found in received env)") < 0) {
            perror("Child: Failed to print environment variable");
            // Attempt to continue printing others.
        }
        fflush(stdout); // Ensure each line is printed promptly.
    }

    // Check if getline stopped due to an error rather than EOF.
    if (errno != 0 && !feof(file)) {
        fprintf(stderr, "Child (%s, %d): Error - ", program_name, pid);
        perror("Error reading from filter file");
        // Error is non-fatal at this point, but worth noting.
    }

    free(line_buf); // Free the buffer allocated by getline.
    if (fclose(file) != 0) {
        fprintf(stderr, "Child (%s, %d): Error - ", program_name, pid);
        perror("Failed to close filter file");
        // Non-fatal error.
    }

    printf("Child: (%s, %d) exiting.\n", program_name, pid);
    fflush(stdout);

    return EXIT_SUCCESS;
}


/*
 * Purpose:
 *   Searches for a specific environment variable within a given environment
 *   array (like the 'envp' array passed to main). This function iterates through
 *   the provided array of "NAME=VALUE" strings.
 * Receives:
 *   var_name:  The name of the environment variable to search for (e.g., "PATH").
 *              Should be a null-terminated string.
 *   env_array: The array of environment strings to search within. This must be
 *              a NULL-terminated array of pointers to characters (char **),
 *              where each string is typically in the "NAME=VALUE" format.
 * Returns:
 *   A pointer to the beginning of the VALUE part of the "NAME=VALUE" string if
 *   the variable 'var_name' is found in the 'env_array'.
 *   Returns NULL if 'var_name' is NULL, 'env_array' is NULL, 'var_name' has
 *   zero length, or the variable is not found in the array.
 *   Note: The returned pointer points directly into the strings within the
 *   'env_array'. Do not modify or free the returned string via this pointer.
 */
static char *find_env_var_value_in_array(const char *var_name, char **env_array) {
    if (var_name == NULL || env_array == NULL) {
        return NULL;
    }
    size_t name_len = strlen(var_name);
    if (name_len == 0) {
        return NULL;
    }

    for (char **env = env_array; *env != NULL; ++env) {
        // Check if the current environment string starts with 'var_name' followed by '='.
        if (strncmp(*env, var_name, name_len) == 0 && (*env)[name_len] == '=') {
            // Return a pointer to the character immediately after the '='.
            return (*env) + name_len + 1;
        }
    }
    // The variable was not found in the array.
    return NULL;
}
