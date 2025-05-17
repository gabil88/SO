#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include "../../include/server/cache.h"


// Helper to check if a path exists in a semicolon-separated list
int path_exists(const char* all_paths, const char* new_path) {
    if (!all_paths || !new_path) return 0;
    char paths_copy[512];
    strncpy(paths_copy, all_paths, sizeof(paths_copy) - 1);
    paths_copy[sizeof(paths_copy) - 1] = '\0';
    char* token = strtok(paths_copy, ";");
    while (token) {
        if (strcmp(token, new_path) == 0) return 1;
        token = strtok(NULL, ";");
    }
    return 0;
}

Cache* cache_init(int max_size, CachePolicy policy){
    Cache* cache = malloc(sizeof(Cache));
    if (cache == NULL) {
        perror("Error allocating memory for cache");
        return NULL;
    }
    cache->items = malloc(sizeof(CacheItem) * max_size);
    if (cache->items == NULL) {
        perror("Error allocating memory for cache items");
        free(cache);
        return NULL;
    }
    for (int i = 0; i < max_size; i++) {
        cache->items[i].doc = NULL;
        cache->items[i].last_access_time = 0;
        cache->items[i].access_count = 0;
    }
    cache->count = 0;
    cache->misses = 0;
    cache->hits = 0;
    cache->max_size = max_size;
    cache->policy = policy;
    return cache;
}

void cache_clean(Cache* cache) {
    if (!cache) return;
    for (int i = 0; i < cache->max_size; i++) {
        if (cache->items[i].doc != NULL) {
            free(cache->items[i].doc);
        }
    }
    free(cache->items);
    free(cache);
}

int cache_add(Cache* cache, Document *doc, int skip_check) {
    if (!cache || !doc) return -1;  
    // Verifica se o documento já existe no cache
    for (int i = 0; i < cache->max_size; i++) {
        printf("Cache item %d: %s\n", i, cache->items[i].doc ? cache->items[i].doc->title : "NULL");
        if (cache->items[i].doc != NULL && strcmp(cache->items[i].doc->title, doc->title) == 0) {
            if (path_exists(cache->items[i].doc->path, doc->path)) {
                printf("Document already exists in cache\n");
                cache->items[i].last_access_time = time(NULL);
                cache->items[i].access_count++;
                return 2; // Exists in cache
            } else {
                add_filepath(cache->items[i].doc, doc->path); // Documento já existe no cache, path diferente
                cache->items[i].last_access_time = time(NULL);
                cache->items[i].access_count++;
                printf("Document already exists in cache, added new path: %s\n", cache->items[i].doc->path);
                return 4; // Exists in cache, but path is different
            }
        }
    }
    cache->misses++;

    if(!skip_check){
        Document* disk_doc = consult_document_by_title(doc->title);
        if(disk_doc != NULL){
            printf("Document already exists in storage: %s\n", doc->title);
            if (path_exists(disk_doc->path, doc->path)) {
                free(disk_doc);
                return 3; // Exists in disk
            } else {
                add_filepath(disk_doc, doc->path);
                int fd = open(pathToDoc, O_RDWR);
                if (fd >= 0) {
                    off_t offset = disk_doc->key * sizeof(Document);
                    if (lseek(fd, offset, SEEK_SET) != (off_t)-1) {
                        int escrita = write(fd, disk_doc, sizeof(Document));
                        if (escrita < 0) {
                            perror("Error writing to file");
                        } else if (escrita != sizeof(Document)) {
                            fprintf(stderr, "Partial write occurred\n");
                        }
                    }
                    close(fd);
                }
                printf("Added new path to existing document on disk: %s\n", disk_doc->path);
                free(disk_doc);
                return 4; // Exists in disk, but path is different
            }
        }
    }

    // Cria uma cópia do documento para guardar na cache
    Document* doc_copy = malloc(sizeof(Document));
    if (!doc_copy) {
        perror("Error allocating memory for document copy");
        return -1;
    }
    memcpy(doc_copy, doc, sizeof(Document));

    // Always try to find an empty slot first
    for (int i = 0; i < cache->max_size; i++) {
        if (cache->items[i].doc == NULL) {
            cache->items[i].doc = doc_copy;
            cache->items[i].last_access_time = time(NULL);
            cache->items[i].access_count = 1;
            cache->count++;
            return 0; 
        }
    }

    // Select victim index based on policy
    int victim_index = 0;
    if (cache->policy == CACHE_POLICY_LRU) {
        time_t oldest_time = cache->items[0].last_access_time;
        for (int i = 1; i < cache->max_size; i++) {
            if (cache->items[i].last_access_time < oldest_time) {
                oldest_time = cache->items[i].last_access_time;
                victim_index = i;
            }
        }
    } else if (cache->policy == CACHE_POLICY_LEAST_USED) {
        int least_used = cache->items[0].access_count;
        for (int i = 1; i < cache->max_size; i++) {
            if (cache->items[i].access_count < least_used) {
                least_used = cache->items[i].access_count;
                victim_index = i;
            }
        }
    }

    // Always update document on eviction (no dirty check)
    if (cache->items[victim_index].doc != NULL) {
        free(cache->items[victim_index].doc);
    }
    
    // Replace the LRU item
    cache->items[victim_index].doc = doc_copy;
    cache->items[victim_index].last_access_time = time(NULL);
    cache->items[victim_index].access_count = 1;
    return 0;  // adicionado com sucesso
}

int cache_remove(Cache* cache, int key){
    if (!cache) return -1; 

    for (int i = 0; i < cache->max_size; i++) {
        if (cache->items[i].doc != NULL && cache->items[i].doc->key == key) {
            free(cache->items[i].doc);
            cache->items[i].doc = NULL;
            cache->items[i].last_access_time = 0;
            cache->items[i].access_count = 0;
            cache->count--;
            int res = remove_document(key);
            if (res == 0) {
                return 0; // removed from cache and disk
            } else {
                return -1; // failed to remove from disk
            }
        }
    }

    return remove_document(key); // Not in cache, try to remove from disk if 0 good if -1 bad
}

Document* cache_get(Cache* cache, int key){
    if (!cache) return NULL;  

    for (int i = 0; i < cache->max_size; i++) {
        if (cache->items[i].doc != NULL && cache->items[i].doc->key == key) {
            cache->items[i].last_access_time = time(NULL);
            cache->items[i].access_count++;
            cache->hits++;
            return cache->items[i].doc;
        }
    }
    return NULL;
}

int cache_update(Cache* cache,int key){
    if (!cache) return -1;  

    for (int i = 0; i < cache->max_size; i++) {
        if (cache->items[i].doc != NULL && cache->items[i].doc->key == key) {
            cache->items[i].last_access_time = time(NULL);
            cache->items[i].access_count++;
            cache->hits++;
            return 0;  // atualizado com sucesso
        }
    }
    return -1;  // não encontrado
}


