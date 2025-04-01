/**
 * @file parent.c
 * @brief Parent process for demonstrating fork, execve, and environment passing.
 *
 * This program launches child processes (`child`) based on user commands,
 * illustrating different ways to manage the child's environment variables:
 * 1. Inheriting the parent's environment and using getenv ('+' or '&' mode).
 * 2. Passing a custom, minimal environment via execve's envp ('*' mode).
 * It requires the CHILD_PATH environment variable to be set, pointing to the
 * directory containing the 'child' executable and the 'env' configuration file.
 */
#define _POSIX_C_SOURCE 200809L // For strdup, getline, nanosleep, POSIX compliance
#define _DEFAULT_SOURCE         // For miscellaneous glibc features if needed implicitly
// Although nanosleep should be covered by _POSIX_C_SOURCE >= 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <ctype.h>
#include <locale.h>
#include <time.h>    // For nanosleep
#include <stdbool.h> // For bool type

// External access to the process environment list
extern char **environ;

// ---- Constants ----
#define MAX_VAR_VALUE_LEN 1024 // Max length for formatted NAME=VALUE string (practical limit)
#define MAX_PATH_LEN 4096      // Max length for paths (practical limit)
#define CHILD_NAME_PREFIX "child_"
#define CHILD_NAME_LEN (sizeof(CHILD_NAME_PREFIX) + 2) // "child_XX\0"
#define ENV_FILENAME "env" // Standard name for the env file

// ---- Globals ----
// Counter for naming child processes uniquely. Explicitly initialized per requirement 11.
static int child_counter = 0;

// ---- Function Prototypes ----
int compare_strings(const void *a, const void *b);
void print_sorted_environ(void);
char** build_child_env(const char *full_env_file_path);
void free_child_env(char **envp);
void launch_child(const char *child_exec_path, const char *full_env_file_path, char mode, bool is_background);

/**
 * @brief Main function for the parent process.
 *
 * Initializes paths, prints the parent environment, and enters a command loop
 * to launch child processes based on user input.
 * Requires the CHILD_PATH environment variable to be set correctly.
 *
 * @param void Takes no command-line arguments.
 * @return int Returns EXIT_SUCCESS on normal exit, EXIT_FAILURE on critical errors.
 */
int main(void) {
    const char *child_path_dir = NULL;
    char child_exec_path[MAX_PATH_LEN];
    char env_file_path[MAX_PATH_LEN];
    int n_exec, n_env;
    int ch, next_ch;

    // --- Initialization and Path Setup ---

    // Get CHILD_PATH from environment - this points to the build dir
    child_path_dir = getenv("CHILD_PATH");
    if (!child_path_dir) {
        fprintf(stderr, "Error: CHILD_PATH environment variable not set.\n");
        fprintf(stderr, "Please run using 'make run' or 'make run-release',\n");
        fprintf(stderr, "or manually set CHILD_PATH (e.g., export CHILD_PATH=build/debug).\n");
        return EXIT_FAILURE;
    }

    // Construct full path to child executable
    n_exec = snprintf(child_exec_path, sizeof(child_exec_path), "%s/child", child_path_dir);
    if (n_exec < 0 || (size_t)n_exec >= sizeof(child_exec_path)) {
        fprintf(stderr, "Error: CHILD_PATH resulted in too long executable path.\n");
        return EXIT_FAILURE;
    }

    // Construct full path to the environment file
    n_env = snprintf(env_file_path, sizeof(env_file_path), "%s/%s", child_path_dir, ENV_FILENAME);
    if (n_env < 0 || (size_t)n_env >= sizeof(env_file_path)) {
        fprintf(stderr, "Error: CHILD_PATH resulted in too long env file path.\n");
        return EXIT_FAILURE;
    }

    // Check if the child executable exists and is executable
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
        fprintf(stderr, "Ensure CHILD_PATH is correct and the '%s' file exists.\n", ENV_FILENAME);
        return EXIT_FAILURE;
    }

    // --- Main Logic ---

    // Print parent's sorted environment once at the start
    print_sorted_environ();

    // Input loop
    printf("\nEnter command (+ = launch fg getenv, * = launch fg envp, & = launch bg getenv, q = quit):\n");
    fflush(stdout); // Ensure prompt is shown before blocking read

    // Use printf in condition for continuous prompting
    while (printf("> "), fflush(stdout), (ch = getchar()) != EOF) {
        // Consume trailing newline or extra input on the line
        if (ch != '\n') {
            while ((next_ch = getchar()) != '\n' && next_ch != EOF);
        } else {
            continue; // If only newline was pressed, just re-prompt
        }

        switch (ch) {
            case '+':
                printf("Command: +\n");
                // Pass full env file path, run in foreground (getenv mode)
                launch_child(child_exec_path, env_file_path, '+', false);
                break;
            case '*':
                printf("Command: *\n");
                // Env file path needed to build envp, run in foreground (envp mode)
                launch_child(child_exec_path, env_file_path, '*', false);
                break;
            case '&':
                printf("Command: & (Launching child in background using '+' mode)\n");
                // Launch using '+' mode conventions, but in the background
                launch_child(child_exec_path, env_file_path, '+', true);
                break;
            case 'q':
                printf("Command: q\nExiting.\n");
                // Note: Does not explicitly wait for background children.
                // They become orphans and are adopted by init/systemd.
                return EXIT_SUCCESS;
            default:
                printf("Unknown command: '%c'\n", ch);
                break;
        }
        // Re-prompt is handled by the while loop condition
    }

    printf("\nEOF reached or error reading input. Exiting.\n");
    return EXIT_SUCCESS; // Normal exit if EOF is reached
}

/**
 * @brief String comparison function for qsort.
 *
 * Compares two C-style strings using strcmp. Assumes LC_COLLATE="C" locale
 * for byte-wise comparison.
 *
 * @param a Pointer to the first string pointer.
 * @param b Pointer to the second string pointer.
 * @return int Result of strcmp (negative if a<b, 0 if a==b, positive if a>b).
 */
int compare_strings(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

/**
 * @brief Prints the parent process's environment variables sorted alphabetically.
 *
 * Copies the environment pointers, sorts them using qsort with LC_COLLATE="C",
 * and prints them to standard output. Handles memory allocation errors.
 *
 * @param void
 * @return void
 */
void print_sorted_environ(void) {
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
        // Non-fatal in interactive parent, just can't print environment
        return;
    }

    // Copy pointers from environ
    for (int i = 0; i < count; i++) {
        env_copy[i] = environ[i];
    }

    // Set locale specifically for sorting (byte order)
    // This avoids issues with user's locale affecting sort order here.
    if (setlocale(LC_COLLATE, "C") == NULL) {
        fprintf(stderr, "Warning: Failed to set locale LC_COLLATE=C for sorting\n");
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

    // Free the allocated array of pointers (not the strings themselves)
    free(env_copy);

    // Optional: Restore locale if needed elsewhere, though C locale is often safe.
    // setlocale(LC_COLLATE, ""); // Restore to environment default
}

/**
 * @brief Builds a new environment array (envp) for the child process.
 *
 * Reads variable names from the specified environment file (`full_env_file_path`).
 * For each name, retrieves the corresponding value from the *parent's* current
 * environment using getenv(). Creates a new NULL-terminated array of strings,
 * each in the "NAME=VALUE" format, suitable for execve's envp argument.
 * Uses dynamic allocation (malloc/realloc) for the array. Handles file and memory errors.
 * Uses getline() to read arbitrarily long variable names from the file.
 *
 * @param full_env_file_path The absolute path to the environment file listing desired variable names.
 * @return char** A dynamically allocated, NULL-terminated array of "NAME=VALUE" strings,
 *                or NULL if an error occurs (e.g., file not found, memory allocation failed).
 *                The caller is responsible for freeing this array and its contents using free_child_env().
 */
char** build_child_env(const char *full_env_file_path) {
    FILE *fp = NULL;
    char **child_envp = NULL;
    size_t env_count = 0;
    size_t env_capacity = 10; // Initial capacity

    char *line = NULL;      // Buffer for getline
    size_t line_cap = 0;    // Capacity for getline
    ssize_t line_len;       // Length from getline

    fp = fopen(full_env_file_path, "r");
    if (!fp) {
        perror("Parent: fopen env file failed");
        fprintf(stderr, "Parent: Tried to open: %s\n", full_env_file_path);
        return NULL;
    }

    child_envp = malloc(env_capacity * sizeof(char *));
    if (!child_envp) {
        perror("Parent: malloc for initial child_envp failed");
        fclose(fp);
        return NULL;
    }

    // Read lines using getline (handles arbitrary length)
    while ((line_len = getline(&line, &line_cap, fp)) != -1) {
        // Remove trailing newline if present
        if (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r')) {
            line[line_len - 1] = '\0';
            if (line_len > 1 && line[line_len - 2] == '\r') { // Handle CRLF
                line[line_len - 2] = '\0';
            }
        }

        // Trim leading/trailing whitespace (simple trim)
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
        char *var_value = getenv(var_name); // Get value from *parent's* env

        if (var_value) {
            // Resize dynamic array if needed (ensure space for entry + NULL terminator)
            if (env_count >= env_capacity - 1) {
                env_capacity *= 2;
                char **temp = realloc(child_envp, env_capacity * sizeof(char *));
                if (!temp) {
                    perror("Parent: realloc for child_envp failed");
                    // Clean up allocated strings and array before returning
                    for (size_t i = 0; i < env_count; i++) free(child_envp[i]);
                    free(child_envp);
                    fclose(fp);
                    free(line); // Free getline buffer
                    return NULL;
                }
                child_envp = temp;
            }

            // Allocate space and format "NAME=VALUE"
            // Use MAX_VAR_VALUE_LEN as a practical limit for the formatted string
            size_t entry_len_needed = strlen(var_name) + 1 + strlen(var_value) + 1; // NAME=VALUE\0
            if (entry_len_needed > MAX_VAR_VALUE_LEN) {
                fprintf(stderr, "Parent: Warning: Env variable '%s' value too long, skipping.\n", var_name);
                continue; // Skip this variable
            }

            child_envp[env_count] = malloc(entry_len_needed);
            if (!child_envp[env_count]) {
                perror("Parent: malloc for env entry failed");
                // Clean up (more complex cleanup ideally needed, simplified here)
                fclose(fp);
                free(line);
                free_child_env(child_envp); // Use helper to free partial array
                return NULL;
            }
            // Use snprintf for safety, though we calculated exact length
            snprintf(child_envp[env_count], entry_len_needed, "%s=%s", var_name, var_value);
            env_count++;
        } else {
            fprintf(stderr, "Parent: Info: Variable '%s' from %s not found in parent's environment, not passed to child in '*' mode.\n",
                    var_name, ENV_FILENAME);
        }
    } // end while getline

    // Check for getline errors
    if (ferror(fp)) {
        perror("Parent: Error reading env file");
        // Clean up
        fclose(fp);
        free(line);
        free_child_env(child_envp);
        return NULL;
    }

    free(line); // Free buffer allocated by getline
    fclose(fp);

    // NULL-terminate the array
    // Ensure space for NULL terminator (realloc if exactly full)
    if (env_count >= env_capacity) {
        char **temp = realloc(child_envp, (env_count + 1) * sizeof(char *));
        if(!temp) {
            perror("Parent: realloc for NULL terminator failed");
            free_child_env(child_envp); // Free existing parts
            return NULL;
        }
        child_envp = temp;
    }
    child_envp[env_count] = NULL;

    return child_envp;
}

/**
 * @brief Frees the memory allocated for a child environment array.
 *
 * Iterates through the NULL-terminated array `envp`, freeing each individual
 * string, and finally frees the array itself. Safely handles NULL input.
 *
 * @param envp The environment array (char**) to free, previously allocated by build_child_env().
 *             Can be NULL, in which case the function does nothing.
 * @return void
 */
void free_child_env(char **envp) {
    if (!envp) {
        return; // Safe to call with NULL
    }
    for (int i = 0; envp[i] != NULL; i++) {
        free(envp[i]); // Free each string "NAME=VALUE"
    }
    free(envp); // Free the array of pointers
}

/**
 * @brief Launches a child process using fork and execve.
 *
 * Prepares arguments and environment for the child based on the mode.
 * Mode '+': Child inherits parent env, gets env file path in argv[1]. Uses getenv().
 * Mode '*': Child gets a custom minimal env via envp. Gets no extra argv. Uses envp iteration.
 * Handles fork, execve errors. Manages foreground/background execution.
 * Uses nanosleep for a brief pause after launching foreground children to improve output ordering.
 *
 * @param child_exec_path Path to the child executable.
 * @param full_env_file_path Path to the env file (used by both modes indirectly or directly).
 * @param mode Character indicating the mode ('+' or '*').
 * @param is_background Boolean indicating whether to run the child in the background (true) or foreground (false).
 * @return void
 */
void launch_child(const char *child_exec_path, const char *full_env_file_path, char mode, bool is_background) {
    if (child_counter > 99) {
        fprintf(stderr, "Parent: Maximum child processes (99) reached.\n");
        return;
    }

    // 1. Build child's environment ('envp') ONLY for '*' mode.
    //    For '+' mode, the child uses parent's 'environ'.
    char **child_envp = NULL;
    if (mode == '*') {
        child_envp = build_child_env(full_env_file_path);
        if (!child_envp) {
            fprintf(stderr, "Parent: Failed to build custom child environment for '*' mode from %s.\n", full_env_file_path);
            return; // Don't proceed if env build failed
        }
    }
    // If mode == '+', child_envp remains NULL, and execve will use parent's 'environ'.

    // 2. Prepare child's arguments ('argv')
    char child_name[CHILD_NAME_LEN];
    snprintf(child_name, sizeof(child_name), "%s%02d", CHILD_NAME_PREFIX, child_counter);

    char *child_argv[3]; // Max 3: child_name, env_file_path (for '+'), NULL
    child_argv[0] = child_name;
    if (mode == '+') { // Also used for '&' background mode
        child_argv[1] = (char *)full_env_file_path; // Pass env file path as argument
        child_argv[2] = NULL;
    } else { // mode == '*'
        child_argv[1] = NULL; // No extra arguments needed
        child_argv[2] = NULL; // Keep terminated
    }

    // 3. Fork
    pid_t pid = fork();

    if (pid == -1) { // Fork failed
        perror("Parent: fork failed");
        if (mode == '*') {
            free_child_env(child_envp); // Clean up envp if fork fails
        }
    } else if (pid == 0) {
        // --- Child Process ---
        // Execute the child program.
        // If mode == '*', pass the custom built child_envp.
        // If mode == '+', pass NULL for envp, making execve use the parent's 'environ'.
        execve(child_exec_path, child_argv, (mode == '*') ? child_envp : environ);

        // execve only returns on error
        perror("Parent: execve failed in child");
        fprintf(stderr," Parent failed trying to execute: %s with mode '%c'\n", child_exec_path, mode);
        if (mode == '*') {
            free_child_env(child_envp); // Free envp in child *only* if exec fails
        }
        _exit(EXIT_FAILURE); // Use _exit in child after fork! Avoids flushing parent's stdio.

    } else {
        // --- Parent Process ---
        printf("Parent: Launched child '%s' (PID: %d)%s.\n",
               child_name, pid, is_background ? " in background" : "");
        child_counter++; // Increment counter only in parent after successful fork

        // Free the custom child environment if it was created (only for '*' mode)
        if (mode == '*') {
            free_child_env(child_envp);
        }

        // Conditional Wait/Pause for foreground process
        if (!is_background) {
            // Give foreground child a moment using nanosleep (POSIX standard)
            struct timespec sleep_time = {0, 100000000L}; // 0 seconds, 100 million nanoseconds = 100ms
            struct timespec remaining_time;
            // Loop in case nanosleep is interrupted by a signal
            while (nanosleep(&sleep_time, &remaining_time) == -1) {
                if (errno == EINTR) {
                    // Interrupted by signal, sleep for the remaining time
                    sleep_time = remaining_time;
                } else {
                    perror("Parent: nanosleep failed");
                    break; // Exit loop on other errors
                }
            }
            // Note: A more robust solution uses waitpid() or signals,
            // but nanosleep provides basic output synchronization for this example.
        }
        // For background (&), we don't wait or pause. Parent continues immediately.
        // No SIGCHLD handling implemented; background zombies may occur.
    }
}
