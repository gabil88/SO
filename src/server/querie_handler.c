#include "../../include/server/document_manager.h"
#include "../../include/server/parsing.h"
#include "../../include/server/cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define pathToFile "DatasetTest/Gdataset"

int get_number_of_lines_with_keyword(Document* doc, char* keyword, int stop_on_first_match) {
    if (doc == NULL) {
        return -1; // Document not found
    }

    int total_count = 0;
    char paths_copy[sizeof(doc->path)]; 
    strncpy(paths_copy, doc->path, sizeof(paths_copy));
    paths_copy[sizeof(paths_copy)-1] = '\0'; // Ensure null-termination

    char *token = strtok(paths_copy, ";");
    while (token != NULL) {
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", pathToFile, token);
        printf("Full path: %s\n", full_path);

        struct stat buffer;
        if (stat(full_path, &buffer) != 0) {
            perror("File does not exist");
            token = strtok(NULL, ";");
            continue;
        }

        int pipefd[2];
        if (pipe(pipefd) == -1) {
            perror("pipe");
            token = strtok(NULL, ";");
            continue;
        }

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            close(pipefd[0]);
            close(pipefd[1]);
            token = strtok(NULL, ";");
            continue;
        }

        if (pid == 0) {  
            // Child process
            close(pipefd[0]);  // Close read end
            dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe write end
            close(pipefd[1]); // Close original pipe write end

            char cmd[4096];
            // The command still counts all lines in the current file.
            // The parent process will decide whether to stop based on this count and the flag.
            long unsigned int size = snprintf(cmd, sizeof(cmd), "grep \"%s\" \"%s\" | wc -l", keyword, full_path);
            if (size >= sizeof(cmd)) {
                fprintf(stderr, "Command too long for buffer\n");
                exit(EXIT_FAILURE);
            }
            execlp("sh", "sh", "-c", cmd, NULL);
            perror("execlp failed"); // If execlp returns, it's an error
            exit(EXIT_FAILURE);
        } else {  // Parent process
            close(pipefd[1]);  // Close write end
            char result[32] = {0}; // Buffer for wc -l output
            ssize_t bytes_read = read(pipefd[0], result, sizeof(result) - 1);
            close(pipefd[0]); // Close read end

            int status;
            waitpid(pid, &status, 0); // Wait for child to complete

            if (bytes_read > 0) {
                int count_in_file = atoi(result); // Lines with keyword in current file
                if (stop_on_first_match) {
                    if (count_in_file > 0) {
                        total_count = 1; // Signal that keyword was found
                    }
                } else {
                    total_count += count_in_file; // Accumulate total lines
                }
            }
            // If read failed or child had an issue, count_in_file might be 0 or result in 0 from atoi.
            // total_count would not be positively affected or would be based on previous files if not stopping early.
        }

        // If stop_on_first_match is true and we've found the keyword (total_count is 1),
        // then break out of the loop over files.
        if (stop_on_first_match && total_count > 0) {
            break; 
        }

        token = strtok(NULL, ";");
    }
    return total_count;
}

int* get_keys_of_docs_with_keyword(char* keyword, int nr_process, int max_key, int* out_count) {
    int work_load = max_key / nr_process;
    int rest = max_key % nr_process;
    pid_t pids[nr_process];
    int pipes[nr_process][2];
    int* all_keys = malloc(sizeof(int) * (max_key + 1));
    int total_count = 0;

    for (int i = 0; i < nr_process; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            free(all_keys);
            return NULL;
        }
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            free(all_keys);
            return NULL;
        }
        if (pid == 0) {  // Child
            close(pipes[i][0]);
            int start = i * work_load;
            int end = start + work_load;
            if (i == nr_process - 1) end += rest;
            int* keys = malloc(sizeof(int) * (end - start));
            int count = 0;
            for (int j = start; j < end; j++) {
                Document* doc = consult_document(j);
                int c = get_number_of_lines_with_keyword(doc, keyword, 1);
                if (c > 0) {
                    keys[count++] = j;
                }
                if (doc) free(doc);
            }
            // Write count first, then the array
            ssize_t w = write(pipes[i][1], &count, sizeof(int));
            if (w != sizeof(int)) {
                perror("write to pipe failed");
                free(keys);
                close(pipes[i][1]);
                exit(1);
            }
            if (count > 0) {
                ssize_t w2 = write(pipes[i][1], keys, sizeof(int) * count);
                if (w2 != (ssize_t)(sizeof(int) * count)) {
                    perror("write to pipe failed");
                    free(keys);
                    close(pipes[i][1]);
                    exit(1);
                }
            }
            free(keys);
            close(pipes[i][1]);
            exit(0);
        } else {
            close(pipes[i][1]);
            pids[i] = pid;
        }
    }
    // Parent collects all keys
    for (int i = 0; i < nr_process; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        int count = 0;
        ssize_t r = read(pipes[i][0], &count, sizeof(int));
        if (r != sizeof(int)) {
            perror("read from pipe failed");
            count = 0; // Set to 0 in case of error
        } else if (count > 0) {
            int* keys = malloc(sizeof(int) * count);
            ssize_t r2 = read(pipes[i][0], keys, sizeof(int) * count);
            if (r2 != (ssize_t)(sizeof(int) * count)) {
                perror("read from pipe failed");
                free(keys);
                close(pipes[i][0]);
                continue;
            }
            for (int k = 0; k < count; k++) {
                all_keys[total_count++] = keys[k];
            }
            free(keys);
        }
        close(pipes[i][0]);
    }
    *out_count = total_count;
    return all_keys;
}
