/**
 * @file child.c
 * @brief Child process launched by the parent.
 *
 * Reports its identity (name, PID, PPID) and prints environment variables
 * based on the mode it was launched in:
 * - Mode '+' (argc=2): Reads variable names from the file path passed in argv[1]
 *   and looks them up using getenv() in its inherited environment.
 * - Mode '*' (argc=1): Iterates through and prints the environment variables
 *   passed directly via the 'envp' parameter to main() (provided by parent's execve).
 * Uses getline() to read arbitrarily long variable names from the file in '+' mode.
 */
#define _POSIX_C_SOURCE 200809L // For getline
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h> // For perror with getline

// External access to the environment pointer (fallback if envp is NULL, though unlikely with execve)
extern char **environ;

/**
 * @brief Main function for the child process.
 *
 * Determines the operating mode based on argc, prints process information,
 * and then prints environment variables either by reading a file and using
 * getenv() ('+' mode) or by iterating the envp array ('*' mode).
 *
 * @param argc Number of command-line arguments. Expected 1 ('*' mode) or 2 ('+' mode).
 * @param argv Array of command-line argument strings. argv[0] is the process name.
 *             argv[1] (if present) is the path to the environment file for '+' mode.
 * @param envp The environment array passed by execve. Used directly in '*' mode.
 * @return int Returns EXIT_SUCCESS on success, EXIT_FAILURE on errors (e.g., file issues).
 */
int main(int argc, char *argv[], char *envp[]) {
    printf("Child Process Report:\n");
    printf("  Name (argv[0]): %s\n", (argv && argv[0]) ? argv[0] : "[unknown]");
    printf("  PID: %d\n", getpid());
    printf("  PPID: %d\n", getppid());
    printf("Child Environment Variables:\n");

    // Mode '+' or '&': Get variables listed in env file using getenv()
    if (argc == 2) {
        const char *env_file_path = argv[1];
        FILE *fp = NULL;
        char *line = NULL;      // Buffer for getline
        size_t line_cap = 0;    // Capacity for getline
        ssize_t line_len;       // Length from getline

        printf("  Mode: '+' (using getenv based on %s)\n", env_file_path);
        fp = fopen(env_file_path, "r");
        if (!fp) {
            perror("Child failed to open env file");
            fprintf(stderr,"Child tried to open: %s\n", env_file_path);
            return EXIT_FAILURE;
        }

        // Read variable names from file using getline
        while ((line_len = getline(&line, &line_cap, fp)) != -1) {
            // Remove trailing newline if present
            if (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r')) {
                line[line_len - 1] = '\0';
                if (line_len > 1 && line[line_len - 2] == '\r') { // Handle CRLF
                    line[line_len - 2] = '\0';
                }
            }

            // Trim leading/trailing whitespace
            char *start = line;
            while (isspace((unsigned char)*start)) start++;

            if (*start == '\0') continue; // Skip empty lines after trimming

            char *end = start + strlen(start) - 1;
            while (end > start && isspace((unsigned char)*end)) end--;
            *(end + 1) = '\0';

            // Skip empty lines or comments
            if (*start == '\0' || *start == '#') {
                continue;
            }

            // 'start' now points to the trimmed variable name
            char *var_name = start;
            char *var_value = getenv(var_name); // Look up in child's inherited env
            if (var_value) {
                printf("    %s=%s\n", var_name, var_value);
            } else {
                printf("    %s (not found via getenv)\n", var_name);
            }
        } // end while getline

        // Check for getline errors
        if (ferror(fp)) {
            perror("Child: Error reading env file");
            // Clean up before exiting
            free(line);
            fclose(fp);
            return EXIT_FAILURE;
        }

        free(line); // Free buffer allocated by getline
        fclose(fp);

        // Mode '*': Get variables by iterating through envp parameter passed by execve
    } else if (argc == 1) {
        printf("  Mode: '*' (iterating envp[] parameter)\n");

        // Use envp passed to main first. Fallback to extern environ if envp is NULL (shouldn't happen with correct execve).
        char **current_env = envp;
        if (current_env == NULL) {
            printf("   Warning: envp parameter to main was NULL. Trying extern char **environ.\n");
            current_env = environ;
        }

        if (current_env && current_env[0] != NULL) {
            for (int i = 0; current_env[i] != NULL; i++) {
                printf("    %s\n", current_env[i]);
            }
        } else {
            printf("   Environment is empty or inaccessible.\n");
        }
        // Incorrect usage
    } else {
        // Requirement 2: Display hint if parameters are wrong
        fprintf(stderr, "Child Usage Error:\n");
        fprintf(stderr, "  Launched as: %s ", (argv && argv[0]) ? argv[0] : "[unknown]");
        for(int i=1; i<argc; ++i) fprintf(stderr, "%s ", argv[i]);
        fprintf(stderr, "\n");
        fprintf(stderr, "  This program expects 0 arguments (for '*' mode) or 1 argument (the env_file path for '+' mode).\n");
        return EXIT_FAILURE;
    }

    printf("Child process finished.\n");
    fflush(stdout); // Ensure output is flushed before exiting
    return EXIT_SUCCESS;
}
