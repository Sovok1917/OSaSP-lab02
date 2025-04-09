
/*
 * child.c: Child process launched by the parent.
 *
 * This program prints its name (argv[0]), PID, and PPID. It then retrieves
 * the path to an environment variable filter file from its own environment
 * (set by the parent). It reads variable names from that file and prints the
 * values of those variables as found in its received environment (envp).
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     // getpid, getppid
#include <errno.h>      // errno


// Name of the environment variable used to get the filter file path

#define ENV_VAR_FILTER_FILE_NAME "CHILD_ENV_FILTER_FILE"

/* --- Function Prototypes --- */

// Finds the value of an environment variable within a given environment array.
static char *find_env_var_value_in_array(const char *var_name, char **env_array);


/*
 * main: Entry point of the child program.
 *
 * Prints identity information, finds the environment filter file path from
 * its environment, reads variable names from that file, looks up their values
 * in its received environment, prints them, and exits.
 *
 * @param argc Number of command-line arguments (expected: 1).
 * @param argv Array of command-line argument strings (argv[0] is program name).
 * @param envp Array of environment variable strings passed by execve.
 * @return EXIT_SUCCESS on successful completion, EXIT_FAILURE on error.
 */
int main(int argc, char *argv[], char **envp) {
    // 1. Print Identity
    const char *program_name = (argc > 0 && argv[0] != NULL) ? argv[0] : "child (unknown name)";
    pid_t pid = getpid();
    pid_t ppid = getppid();

    if (printf("Child: Name='%s', PID=%d, PPID=%d\n", program_name, pid, ppid) < 0) {
        perror("Child: Failed to print identity");
        return EXIT_FAILURE; // Cannot proceed reliably
    }
    fflush(stdout); // Ensure identity is printed before potential errors

    // 2. Get Environment Filter File Path from *own* environment
    // CRITICAL: Use the envp passed to main, not getenv(), to ensure we read
    // the filtered environment provided by the parent via execve.
    const char *filter_filename = find_env_var_value_in_array(ENV_VAR_FILTER_FILE_NAME, envp);

    if (filter_filename == NULL) {
        fprintf(stderr, "Child (%s, %d): Error - Environment variable '%s' not found in received environment.\n",
                program_name, pid, ENV_VAR_FILTER_FILE_NAME);
        return EXIT_FAILURE;
    }

    if (printf("Child: Using environment filter file: %s\n", filter_filename) < 0) {
        perror("Child: Failed to print filter filename");
        // Continue if possible, but indicates output issues
    }
    fflush(stdout);

    // 3. Open and Read Filter File
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

    // 4. Read Variable Names and Print Values from Received Environment
    while ((line_len = getline(&line_buf, &line_buf_size, file)) != -1) {
        // Remove trailing newline character, if present
        if (line_len > 0 && line_buf[line_len - 1] == '\n') {
            line_buf[line_len - 1] = '\0';
            line_len--; // Adjust length
        }

        // Skip empty lines or lines starting with #
        if (line_len == 0 || line_buf[0] == '#') {
            continue;
        }

        char *var_name = line_buf;
        // Find the variable's value *specifically* in the 'envp' array passed to main
        char *var_value = find_env_var_value_in_array(var_name, envp);

        if (printf("  %s=%s\n", var_name, var_value ? var_value : "(Not found in received env)") < 0) {
            perror("Child: Failed to print environment variable");
            // Continue trying to print others
        }
        fflush(stdout); // Ensure each line is printed
    }

    // Check for getline errors
    if (errno != 0 && !feof(file)) {
        fprintf(stderr, "Child (%s, %d): Error - ", program_name, pid);
        perror("Error reading from filter file");
    }

    free(line_buf); // Free buffer used by getline
    if (fclose(file) != 0) {
        fprintf(stderr, "Child (%s, %d): Error - ", program_name, pid);
        perror("Failed to close filter file");
        // Continue to exit, but log the error
    }

    printf("Child: (%s, %d) exiting.\n", program_name, pid);
    fflush(stdout);

    return EXIT_SUCCESS;
}


/*
 * find_env_var_value_in_array: Searches a specific environment array for a variable.
 *
 * Iterates through the provided environment array (NULL-terminated list of
 * "NAME=VALUE" strings) and returns a pointer to the VALUE part if the
 * variable NAME is found. This is crucial for the child to inspect only the
 * environment it *received* via execve (in its main's envp parameter).
 *
 * @param var_name The name of the environment variable to find (e.g., "PATH").
 * @param env_array The specific environment array (child's 'envp') to search.
 * @return Pointer to the value string if found, NULL otherwise. The returned
 *         pointer points into the existing env_array strings, do not free it.
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
        // Check if the current env string starts with "var_name="
        if (strncmp(*env, var_name, name_len) == 0 && (*env)[name_len] == '=') {
            return (*env) + name_len + 1; // Return pointer to the character after '='
        }
    }
    return NULL; // Variable not found
}
