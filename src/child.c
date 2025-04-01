#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#define MAX_VAR_NAME_LEN 256

// Access to the environment pointer (for '*' mode if main doesn't have envp)
// Alternatively, use the third argument to main.
extern char **environ;

int main(int argc, char *argv[], char *envp[]) { // Using envp parameter is cleaner
    printf("Child Process Report:\n");
    printf("  Name (argv[0]): %s\n", argv[0]);
    printf("  PID: %d\n", getpid());
    printf("  PPID: %d\n", getppid());
    printf("Child Environment Variables:\n");

    if (argc == 2) {
        // Mode '+' : Get variables listed in env file using getenv()
        const char *env_file_path = argv[1];
        printf("  Mode: '+' (using getenv based on %s)\n", env_file_path);
        FILE *fp = fopen(env_file_path, "r");
        if (!fp) {
            perror("Child failed to open env file");
            return EXIT_FAILURE;
        }

        char line[MAX_VAR_NAME_LEN];
        while (fgets(line, sizeof(line), fp)) {
            // Remove trailing newline
            line[strcspn(line, "\n\r")] = '\0';

            // Trim leading/trailing whitespace (simple trim)
            char *start = line;
            while(isspace((unsigned char)*start)) start++;
            char *end = start + strlen(start) - 1;
            while(end > start && isspace((unsigned char)*end)) end--;
            *(end + 1) = '\0';

            // Skip empty lines
            if (*start == '\0') {
                continue;
            }

            char *var_name = start;
            char *var_value = getenv(var_name);
            if (var_value) {
                printf("    %s=%s\n", var_name, var_value);
            } else {
                printf("    %s (not found via getenv)\n", var_name);
            }
        }
        fclose(fp);

    } else if (argc == 1) {
        // Mode '*' : Get variables by iterating through envp parameter
        printf("  Mode: '*' (iterating envp[] parameter)\n");
        if (envp == NULL) {
            printf("   Warning: envp parameter is NULL. Trying extern char **environ.\n");
            envp = environ; // Fallback, though envp should be passed by execve
        }

        if (envp) {
            for (int i = 0; envp[i] != NULL; i++) {
                printf("    %s\n", envp[i]);
            }
        } else {
            printf("   Error: Cannot access environment variables.\n");
        }
    } else {
        fprintf(stderr, "Child Usage: %s [<env_file_path> for + mode]\n", argv[0]);
        return EXIT_FAILURE;
    }

    printf("Child process finished.\n");
    return EXIT_SUCCESS;
}
