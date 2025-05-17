#include "../../include/server/server_handlers.h"
#include "../../include/server/server_utils.h"
#include "../../include/server/parsing.h"
#include "../../include/server/querie_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>



void handle_parent_types(int type, Cache* cache, GArray* deleted_keys, int* max_key, int fifo_fd, int internal_fifo_fd, char* pipe_name, char* request, Document* doc) {
    switch(type) {
        case 0: // Shutdown
            send_message(pipe_name, "Server shutting down.");
            double hit_rate = (double)cache->hits / (cache->hits + cache->misses);
            printf("Cache hit rate: %.2f%%\n", hit_rate * 100);
            g_array_free(deleted_keys, TRUE);
            cache_clean(cache);
            close(fifo_fd);
            close(internal_fifo_fd);
            unlink(FIFO_PATH);
            unlink(INTERNAL_FIFO_PATH);
            send_message(pipe_name, "Server shutting down.");
            exit(0);
        case 1: // Add document
            if (deleted_keys->len > 0) {
                doc->key = g_array_index(deleted_keys, int, deleted_keys->len - 1);
                g_array_remove_index(deleted_keys, deleted_keys->len - 1);
            } else {
                doc->key = *max_key + 1;
            }
            int result = -1;
            int status = cache_add(cache, doc, 0);
            printf("Cache add status: %d\n", status);
            if (status == 0) {
                int result = add_document(doc);
                if (result == -1) {
                    send_message(pipe_name, "Error adding document to disk.");
                    free(doc);
                    return;
                }
                char message[256];
                snprintf(message, sizeof(message), "Document %d indexed\n", doc->key);
                send_message(pipe_name, message);
                printf("Document %d indexed\n", doc->key);
                (*max_key)++;
            } else if (status == 2) {
                send_message(pipe_name, "Document already exists.");
            } else if (status == 3) {
                send_message(pipe_name, "Document already exists.");
            } else if (status == 4) {
                result = add_document(doc);
                if (result == -1) {
                    send_message(pipe_name, "Error adding document to disk.");
                    free(doc);
                    return;
                }
                send_message(pipe_name, "Added new new path to the document.");

            } else {
                send_message(pipe_name, "Unexpected Error adding document.");
            }
            print_cache_state(cache);
            break;
        case 3: // Remove document  
            status = cache_remove(cache, doc->key);
            print_cache_state(cache);
            if (status == 0) {
                char message[256];
                snprintf(message, sizeof(message), "Index entry %d deleted", doc->key);
                send_message(pipe_name, message);
                g_array_append_val(deleted_keys, doc->key);
                printf("]\n");
            } else {
                send_message(pipe_name, "Document not found in cache or disk.");
            }
            break;
        case 6: // Wait for child
            pid_t pid = atoi(request + 3);
            if (pid > 0) {
                int status;
                waitpid(pid, &status, 0);
            }
            break;
        case 7: // Add from disk to cache
        {
            int key_to_add = atoi(request + 4);
            Document* d = consult_document(key_to_add);
            if (d) cache_add(cache, d, 1);
        }
            break;
        case 8: // Update time in cache
        {
            int key_to_update = atoi(request + 4);
            cache_update(cache, key_to_update);
        }
            break;
    }
    free(doc);
}

// Handles child/grandchild request types (2, 4, 5)
void handle_child_types(int type, Request* req, Cache* cache, int max_key) {
    char* request = req->request;
    char* pipe_name = req->pipe;
    Document* doc = malloc(sizeof(Document));
    if (!doc) { send_message(pipe_name, "Server error: Unable to allocate memory."); exit(1); }
    initialize_document(doc);
    parsing(request, doc);
    pid_t child_pid = fork();
    if (child_pid < 0) { free(doc); return; }
    if (child_pid == 0) {
        pid_t grandchild_pid = fork();
        if (grandchild_pid < 0) { free(doc); exit(1); }
        if (grandchild_pid == 0) {
            // Grandchild process: handle consult/query
            switch(type) {
                case 2: {
                    Document* found_doc = cache_get(cache, doc->key);
                    if (found_doc) {
                        char message[2048];
                        snprintf(message, sizeof(message),
                            "Title: %s\nAuthors: %s\nYear: %d\nPath: %s",
                            found_doc->title, found_doc->author, found_doc->year, found_doc->path);
                        send_message(pipe_name, message);
                        char updateCache[256];
                        snprintf(updateCache, sizeof(updateCache), "-uc/%d", found_doc->key);
                        send_internal_request_to_server(updateCache);
                    } else {
                        found_doc = consult_document(doc->key);
                        if (found_doc) {
                            char message[2048];
                            snprintf(message, sizeof(message),
                                "Title: %s\nAuthors: %s\nYear: %d\nPath: %s",
                                found_doc->title, found_doc->author, found_doc->year, found_doc->path);
                            send_message(pipe_name, message);
                            char addCache[256];
                            snprintf(addCache, sizeof(addCache), "-ac/%d", found_doc->key);
                            send_internal_request_to_server(addCache);
                            free(found_doc);
                        } else {
                            send_message(pipe_name, "Document not found.");
                        }
                    }
                    break;
                }
                case 4: {
                    printf("Query -l detected: %s\n", request);
                    char** extracted = special_parsing(request);
                    if (!extracted) { send_message(pipe_name, "Invalid request format."); free(doc); exit(1); }
                    int key = atoi(extracted[0]);
                    Document* extracted_doc = cache_get(cache, key);
                    if(extracted_doc == NULL) {
                        extracted_doc = consult_document(key);
                    }
                    printf("Full path: %s\n", extracted_doc->path);
                    if (extracted_doc) {
                        char updateCache[256];
                        snprintf(updateCache, sizeof(updateCache), "-uc/%d", key);
                        send_internal_request_to_server(updateCache);
                        int count = get_number_of_lines_with_keyword(extracted_doc, extracted[1], 0);
                        printf("Count: %d\n", count);
                        if (count == -1) {
                            send_message(pipe_name, "Error reading file.");
                        } else {
                            char message[256];
                            snprintf(message, sizeof(message), "%d",count);
                            send_message(pipe_name, message);
                        }
                        fflush(stdout);
                    } else {
                        extracted_doc = consult_document(key);
                        if (!extracted_doc) {
                            send_message(pipe_name, "Document not found.");
                        } else {
                            char addCache[256];
                            snprintf(addCache, sizeof(addCache), "-ac/%d", key);
                            send_internal_request_to_server(addCache);
                            int count = get_number_of_lines_with_keyword(extracted_doc, extracted[1], 0);
                            if (count == -1) {
                                send_message(pipe_name, "Error reading file.");
                            } else {
                                char message[256];
                                snprintf(message, sizeof(message), "Number of lines with keyword '%s': %d", extracted[1], count);
                                send_message(pipe_name, message);
                            }
                            free(extracted_doc);
                        }
                    }
                    free(extracted[0]);
                    free(extracted[1]);
                    free(extracted);
                    break;
                }
                case 5: {
                    printf("Query -s detected: %s\n", request);
                    char** extracted = special_parsing(request);
                    int nr_process = atoi(extracted[1]);
                    int key_count = 0;
                    int* keys = get_keys_of_docs_with_keyword(extracted[0], nr_process, max_key, &key_count);
                    if (!keys) {
                        send_message(pipe_name, "Error reading file.");
                    } else {
                        char message[8192];
                        int offset = 0;
                        offset += snprintf(message + offset, sizeof(message) - offset, "[");
                        for (int i = 0; i < key_count; i++) {
                            offset += snprintf(message + offset, sizeof(message) - offset, "%d%s", keys[i], (i < key_count - 1) ? "," : "");
                        }
                        snprintf(message + offset, sizeof(message) - offset, "]");
                        send_message(pipe_name, message);
                        free(keys);
                    }
                    free(extracted[0]);
                    free(extracted[1]);
                    free(extracted);
                    break;
                }
            }
            free(doc);
            exit(0);
        } else {
            // Child process waits for grandchild
            int status;
            waitpid(grandchild_pid, &status, 0);
            char notify[256];
            snprintf(notify, sizeof(notify), "-w/%d", getpid());
            Request notify_req = {0};
            snprintf(notify_req.request, sizeof(notify_req.request), "%s", notify);
            snprintf(notify_req.pipe, sizeof(notify_req.pipe), "%s", pipe_name);
            int notify_fd = open(INTERNAL_FIFO_PATH, O_WRONLY);
            if (notify_fd != -1) {
                if (write(notify_fd, &notify_req, sizeof(Request)) == -1) { perror("write"); }
                close(notify_fd);
            }
            free(doc);
            exit(0);
        }
    }
}

