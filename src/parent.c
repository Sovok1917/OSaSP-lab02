#define _DEFAULT_SOURCE // Request default glibc features (includes usleep)
#define _POSIX_C_SOURCE 200809L // For strdup, getline, nanosleep etc.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <ctype.h>
#include <locale.h>
#include <time.h> // Needed for nanosleep if you switch to it
#include <stdbool.h> // For bool type

// Access to the environment pointer
extern char **environ;

// Max lengths for safety
#define MAX_VAR_NAME_LEN 256
#define MAX_VAR_VALUE_LEN 1024
#define MAX_ENV_VARS 256
#define MAX_PATH_LEN 4096
#define CHILD_NAME_PREFIX "child_"
#define CHILD_NAME_LEN (sizeof(CHILD_NAME_PREFIX) + 2) // "child_XX\0"
#define ENV_FILENAME "env" // Standard name for the env file

// Child process counter
static int child_counter = 0;

// Comparison function for qsort (using strcmp for LC_COLLATE=C)
int compare_strings(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

// Function to print parent's environment sorted
void print_sorted_environ() {
    int count = 0;
    char **env_copy = NULL;
    char **current = environ;

    // Count environment variables
    while (*current++) {
        count++;
    }
    if (count == 0) {
        printf("--- Parent's Environment (empty) ---\n");
        return;
    }


    // Allocate memory for a copy of the pointers
    env_copy = malloc(count * sizeof(char *));
    if (!env_copy) {
        perror("malloc for env_copy failed");
        return; // Or exit
    }

    // Copy pointers from environ
    for (int i = 0; i < count; i++) {
        env_copy[i] = environ[i];
    }

    // Set locale for sorting
    if (setlocale(LC_COLLATE, "C") == NULL) {
        fprintf(stderr, "Warning: Failed to set locale LC_COLLATE=C\n");
        // Continue with default locale sorting if setting fails
    }


    // Sort the copied pointers
    qsort(env_copy, count, sizeof(char *), compare_strings);

    // Print sorted environment
    printf("--- Parent's Sorted Environment (LC_COLLATE=C) ---\n");
    for (int i = 0; i < count; i++) {
        printf("%s\n", env_copy[i]);
    }
    printf("--------------------------------------------------\n");

    // Free the allocated array (not the strings themselves, they belong to environ)
    free(env_copy);
}

// Function to build the child's environment
// Takes the *full path* to the env file
char** build_child_env(const char *full_env_file_path) {
    FILE *fp = fopen(full_env_file_path, "r");
    if (!fp) {
        perror("Parent: fopen env file failed");
        fprintf(stderr, "Parent: Tried to open: %s\n", full_env_file_path);
        return NULL;
    }

    char **child_envp = NULL;
    size_t env_count = 0;
    size_t env_capacity = 10; // Initial capacity

    child_envp = malloc(env_capacity * sizeof(char *));
    if (!child_envp) {
        perror("malloc for child_envp failed");
        fclose(fp);
        return NULL;
    }

    char line[MAX_VAR_NAME_LEN];
    while (fgets(line, sizeof(line), fp)) {
        // Remove trailing newline if present
        line[strcspn(line, "\n\r")] = '\0';

        // Trim leading/trailing whitespace (simple trim)
        char *start = line;
        while (isspace((unsigned char)*start)) start++;
        char *end = start + strlen(start) - 1;
        while (end > start && isspace((unsigned char)*end)) end--;
        *(end + 1) = '\0';

        // Skip empty lines or comments
        if (*start == '\0' || *start == '#') {
            continue;
        }

        char *var_name = start;
        char *var_value = getenv(var_name);

        if (var_value) {
            // Resize dynamic array if needed
            if (env_count >= env_capacity - 1) { // Need space for NULL terminator
                env_capacity *= 2;
                char **temp = realloc(child_envp, env_capacity * sizeof(char *));
                if (!temp) {
                    perror("realloc for child_envp failed");
                    // Clean up previously allocated strings
                    for (size_t i = 0; i < env_count; i++) {
                        free(child_envp[i]);
                    }
                    free(child_envp);
                    fclose(fp);
                    return NULL;
                }
                child_envp = temp;
            }

            // Allocate space and format "NAME=VALUE"
            size_t entry_len = strlen(var_name) + 1 + strlen(var_value) + 1; // NAME=VALUE\0
            child_envp[env_count] = malloc(entry_len);
            if (!child_envp[env_count]) {
                perror("malloc for env entry failed");
                fclose(fp);
                // Free previously allocated parts of child_envp (complex cleanup omitted for brevity)
                free(child_envp); // Simplified cleanup
                return NULL;
            }
            snprintf(child_envp[env_count], entry_len, "%s=%s", var_name, var_value);
            env_count++;
        } else {
            fprintf(stderr, "Parent: Warning: Variable '%s' from %s not found in parent's environment.\n", var_name, ENV_FILENAME);
        }
    }

    fclose(fp);

    // NULL-terminate the array
    if (env_count >= env_capacity) { // Ensure space for NULL terminator after loop
        char **temp = realloc(child_envp, (env_count + 1) * sizeof(char *));
        if(!temp) {
            perror("realloc for NULL terminator failed");
            // Free previously allocated parts (complex cleanup omitted)
            free(child_envp); // Simplified cleanup
            return NULL;
        }
        child_envp = temp;
    }
    child_envp[env_count] = NULL;

    return child_envp;
}

// Function to free the memory allocated for the child's environment array
void free_child_env(char **envp) {
    if (!envp) return;
    for (int i = 0; envp[i] != NULL; i++) {
        free(envp[i]);
    }
    free(envp);
}

// Function to launch a child process
// Takes the full path to the child executable, the full path to the env file,
// the mode ('+' or '*'), and whether to run in background.
void launch_child(const char *child_exec_path, const char *full_env_file_path, char mode, bool is_background) {
    if (child_counter > 99) {
        fprintf(stderr, "Parent: Maximum child processes (99) reached.\n");
        return;
    }

    // 1. Build child's environment based on the env file
    //    (This is always needed to know *which* vars to pass, even for '*' mode)
    char **child_envp = build_child_env(full_env_file_path);
    if (!child_envp) {
        fprintf(stderr, "Parent: Failed to build child environment from %s.\n", full_env_file_path);
        return;
    }

    // 2. Prepare child's argv
    char child_name[CHILD_NAME_LEN];
    snprintf(child_name, sizeof(child_name), "%s%02d", CHILD_NAME_PREFIX, child_counter);

    char *child_argv[3]; // Max 3: child_name, env_file_path (optional), NULL
    child_argv[0] = child_name;
    // Only pass env file path if mode is '+' (used by standalone '&' too)
    if (mode == '+') {
        child_argv[1] = (char *)full_env_file_path; // Cast needed for execve prototype
        child_argv[2] = NULL;
    } else { // mode == '*'
        child_argv[1] = NULL;
    }

    // 3. Fork
    pid_t pid = fork();

    if (pid == -1) {
        perror("Parent: fork failed");
        free_child_env(child_envp); // Clean up envp if fork fails
    } else if (pid == 0) {
        // --- Child Process ---
        // Note: Child's printf might be interleaved with parent's prompt,
        // especially when running in the background.

        // Execute the child program
        execve(child_exec_path, child_argv, child_envp);

        // execve only returns on error
        perror("Parent: execve failed");
        fprintf(stderr," Parent failed trying to execute: %s\n", child_exec_path); // Debug info
        // IMPORTANT: Use _exit in child after fork failure in execve
        // to avoid flushing parent's stdio buffers.
        free_child_env(child_envp); // Free envp in child only if exec fails
        _exit(EXIT_FAILURE); // Use _exit!

    } else {
        // --- Parent Process ---
        printf("Parent: Launched child '%s' (PID: %d)%s.\n",
               child_name, pid, is_background ? " in background" : ""); // Indicate background launch
        child_counter++; // Increment counter only in parent after successful fork

        // Free the child's environment strings (parent's copy) - crucial!
        free_child_env(child_envp);

        // *** Conditional Wait/Pause ***
        if (!is_background) {
            // Give foreground child a moment to print its output before parent prompts again
            // This helps prevent garbled output but isn't foolproof.
            usleep(100000); // 100ms - adjust as needed (requires _DEFAULT_SOURCE)

            // Alternative using nanosleep (more POSIX compliant):
            // struct timespec sleep_time;
            // sleep_time.tv_sec = 0;
            // sleep_time.tv_nsec = 100000000L; // 100 million nanoseconds = 100 ms
            // nanosleep(&sleep_time, NULL);
        }
        // NOTE: For background (&), we don't wait or pause here at all.
        // Proper background process handling would involve SIGCHLD and waitpid(WNOHANG)
        // to reap zombie processes. This basic implementation omits that; zombies
        // might accumulate until the parent exits or are inherited by init.
    }
}

int main(void) { // No command line arguments needed anymore

    // Get CHILD_PATH from environment - this points to the build dir (debug or release)
    const char *child_path_dir = getenv("CHILD_PATH");
    if (!child_path_dir) {
        fprintf(stderr, "Error: CHILD_PATH environment variable not set.\n");
        fprintf(stderr, "Please run using 'make run' or 'make run-release',\n");
        fprintf(stderr, "or manually set CHILD_PATH (e.g., export CHILD_PATH=build/debug).\n");
        return EXIT_FAILURE;
    }

    // Construct full path to child executable
    char child_exec_path[MAX_PATH_LEN];
    int n_exec = snprintf(child_exec_path, sizeof(child_exec_path), "%s/child", child_path_dir);
    if (n_exec < 0 || (size_t)n_exec >= sizeof(child_exec_path)) {
        fprintf(stderr, "Error: CHILD_PATH resulted in too long executable path.\n");
        return EXIT_FAILURE;
    }

    // Construct full path to the environment file (located in the same directory)
    char env_file_path[MAX_PATH_LEN];
    int n_env = snprintf(env_file_path, sizeof(env_file_path), "%s/%s", child_path_dir, ENV_FILENAME);
    if (n_env < 0 || (size_t)n_env >= sizeof(env_file_path)) {
        fprintf(stderr, "Error: CHILD_PATH resulted in too long env file path.\n");
        return EXIT_FAILURE;
    }

    // Check if the child executable actually exists and is executable
    if (access(child_exec_path, X_OK) != 0) {
        perror("Error checking child executable");
        fprintf(stderr, "Parent looked for: %s\n", child_exec_path);
        fprintf(stderr, "Ensure CHILD_PATH is correct and 'make' was run successfully.\n");
        return EXIT_FAILURE;
    }
    // Check if the env file exists and is readable
    if (access(env_file_path, R_OK) != 0) {
        perror("Error checking env file");
        fprintf(stderr, "Parent looked for: %s\n", env_file_path);
        fprintf(stderr, "Ensure CHILD_PATH is correct and 'make' was run successfully (it creates the env file).\n");
        return EXIT_FAILURE;
    }


    // Print parent's sorted environment
    print_sorted_environ();

    // Input loop - Updated prompt
    printf("\nEnter command (+ = launch fg getenv, * = launch fg envp, & = launch bg getenv, q = quit):\n");
    fflush(stdout); // Ensure prompt is shown before getchar potentially blocks

    int ch;
    while (printf("> "), fflush(stdout), (ch = getchar()) != EOF) {
        // Consume trailing newline character(s) or extra input on the line
        if (ch != '\n') {
            int next_ch;
            while ((next_ch = getchar()) != '\n' && next_ch != EOF);
        } else {
            continue; // If only newline was pressed, just re-prompt
        }


        switch (ch) {
            case '+':
                printf("Command: +\n");
                // Pass the *full path* to the env file, run in foreground
                launch_child(child_exec_path, env_file_path, '+', false);
                break;
            case '*':
                printf("Command: *\n");
                // Env file path is needed to build envp, run in foreground
                launch_child(child_exec_path, env_file_path, '*', false);
                break;
            case '&':
                printf("Command: & (Launching child in background using '+' mode)\n");
                // Launch using '+' mode conventions, but in the background
                launch_child(child_exec_path, env_file_path, '+', true);
                break;
            case 'q':
                printf("Command: q\nExiting.\n");
                // As before, this doesn't explicitly wait for background children.
                // They become orphans if still running and are adopted by init.
                return EXIT_SUCCESS;
            default:
                printf("Unknown command: '%c'\n", ch);
                break;
        }
        // Don't need the extra prompt here, it's at the start of the while condition
    }

    printf("\nEOF reached or error reading input. Exiting.\n");
    return EXIT_SUCCESS;
}
