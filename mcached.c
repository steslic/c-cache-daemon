/* Memory cache daemon mcached.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "mcached.h"

#define HASH_SIZE 1024
#define MAX_KEY_SIZE 250
#define VERSION_STR "C-Memcached 1.0"

/* Create hash table */
hash_table_t *create_hash_table() {
    hash_table_t *table = malloc(sizeof(hash_table_t));
    memset(table, 0, sizeof(hash_table_t));
    for (int i = 0; i < HASH_SIZE; i++) {
        pthread_mutex_init(&table->locks[i], NULL);
    }
    return table;
}

/* Main function */
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <port> <num_threads>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // string to int 
    int port = atoi(argv[1]);
    int num_threads = atoi(argv[2]);

    if (port <= 0 || num_threads <= 0) {
        fprintf(stderr, "Invalid port or number of threads\n");
        exit(EXIT_FAILURE);
    }

    // Create server socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Prepare server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Bind socket to port
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(server_fd, 10) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // printf("Server started on port %d with %d worker threads.\n", port, num_threads);

    // Create hash table
    hash_table_t *table = create_hash_table();

    // Create the specified number of worker threads
    pthread_t threads[num_threads];

    for (int i = 0; i < num_threads; i++) {
        // allocate memory for each thread
        thread_arg_t *arg = malloc(sizeof(thread_arg_t));
        if (!arg) {
            perror("malloc failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }
        arg->server_fd = server_fd;
        arg->table = table;

        // create worker thread 
        if (pthread_create(&threads[i], NULL, worker_thread, arg) != 0) {
            perror("Thread creation failed");
            free(arg);  // Free the allocated memory on failure
            close(server_fd);
            exit(EXIT_FAILURE);
        }
    }

    // Wait for worker threads (they should not terminate)
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    // Clean up 
    close(server_fd);
    return 0;
}

/* Worker thread - accepts client connections */
void *worker_thread(void *arg) {
    thread_arg_t *thread_arg = (thread_arg_t *)arg;
    int server_fd = thread_arg->server_fd;
    hash_table_t *table = thread_arg->table;

    free(thread_arg);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        // Accept a client connection
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("Accept client connection failed");
            continue;
        }

        // Handle the client connection
        process_client(client_fd, table);

        // Close client connection
        close(client_fd);
    }
    return NULL;
}

/* Process requests from a client */
void process_client(int client_fd, hash_table_t *table) {
    memcache_req_header_t header;
    ssize_t bytes_read;

    // printf("Client connected. Waiting for requests...\n");

    while ((bytes_read = read(client_fd, &header, sizeof(header))) == sizeof(header)) {
        header.key_length = ntohs(header.key_length);
        header.vbucket_id = ntohs(header.vbucket_id);
        header.total_body_length = ntohl(header.total_body_length);

        // printf("Received request: magic=0x%02x, opcode=0x%02x, key_length=%d, total_body_length=%d\n",
               // header.magic, header.opcode, header.key_length, header.total_body_length);

        // Check magic number
        if (header.magic != 0x80) {
            // printf("Invalid magic number: 0x%02x (expected 0x80)\n", header.magic);
            send_response(client_fd, 0xFF, RES_ERROR, NULL, 0);
            continue;
        }

        // Check total body length
        if (header.total_body_length < header.key_length + header.extras_length) {
            send_response(client_fd, header.opcode, RES_ERROR, NULL, 0);
            continue;
        }

        // Read the key and value
        char *key = NULL;
        char *value = NULL;

        if (header.key_length > 0) {
            key = malloc(header.key_length);
            read(client_fd, key, header.key_length);
        }

        uint32_t value_length = header.total_body_length - header.key_length - header.extras_length;
        if (value_length > 0) {
            value = malloc(value_length);
            if (read(client_fd, value, value_length) != value_length) {
                perror("Failed to read value");
                free(key);
                free(value);
                return;
            }
            // read(client_fd, value, value_length);
        }

        // printf("Dispatching command. Thread ID: %ld; Opcode: 0x%02x\n", pthread_self(), header.opcode);

        // Process a command
        switch(header.opcode) {
            case CMD_GET: 
                handle_get(client_fd, table, key, header.key_length);
                break;

            case CMD_SET:
                handle_set_add(client_fd, table, key, header.key_length, value, value_length, header.opcode);
                break;

            case CMD_ADD:
                handle_set_add(client_fd, table, key, header.key_length, value, value_length, header.opcode);
                break;

            case CMD_DELETE:
                handle_delete(client_fd, table, key, header.key_length);
                break;

            case CMD_VERSION:
                handle_version(client_fd);
                break;

            case CMD_OUTPUT:
                handle_output(client_fd, table);
                break;

            default:
                // printf("Unknown opcode: 0x%02x\n", header.opcode);
                send_response(client_fd, header.opcode, RES_ERROR, NULL, 0);
        }
        if (key) {
            free(key);
        }
        if (value) {
            free(value);
        }
    }
    // printf("Client disconnected or error occurred.\n");
}

/* Handle GET */
void handle_get(int sockfd, hash_table_t *table, const char *key, uint16_t key_len) {
    unsigned int index = hash_function(key, key_len); // hash of the key
    pthread_mutex_lock(&table->locks[index]);

    entry_t *entry = table->entries[index];
    // search for a matching key
    while (entry) {
        if (entry->key_length == key_len && memcmp(entry->key, key, key_len) == 0) { 
            // found a match. lock entry
            pthread_mutex_lock(&entry->lock);
            pthread_mutex_unlock(&table->locks[index]);

            // send a response containing the value 
            send_response(sockfd, CMD_GET, RES_OK, entry->value, entry->value_length);

            // unlock the entry
            pthread_mutex_unlock(&entry->lock);
            return;
        }
        entry = entry->next;
    }

    // no match found
    pthread_mutex_unlock(&table->locks[index]);
    send_response(sockfd, CMD_GET, RES_NOT_FOUND, NULL, 0);
}

/* Handle SET ADD */
void handle_set_add(int sockfd, hash_table_t *table, const char *key, uint16_t key_len, const char *value, uint32_t value_len, uint8_t opcode) {
    unsigned int index = hash_function(key, key_len); // calculate the hash of the key
    pthread_mutex_lock(&table->locks[index]);

    entry_t *entry = table->entries[index];
    entry_t *prev = NULL;

    // Search for key
    while (entry) {
        if (entry->key_length == key_len && memcmp(entry->key, key, key_len) == 0) {
            // The key exists. Release the lock and send an "Exists" response
            if (opcode == CMD_ADD) {
                pthread_mutex_unlock(&table->locks[index]);
                send_response(sockfd, opcode, RES_EXISTS, NULL, 0);
                return;
            }

            pthread_mutex_lock(&entry->lock);
            if (entry->value) {
                free(entry->value);
            }

            entry->value = malloc(value_len);
            memcpy(entry->value, value, value_len);
            entry->value_length = value_len;

            pthread_mutex_unlock(&entry->lock);
            pthread_mutex_unlock(&table->locks[index]);

            send_response(sockfd, opcode, RES_OK, NULL, 0);
            return;
        }
        prev = entry;
        entry = entry->next;
    }

    // Key didnt exist, create new key
    entry_t *new_entry = malloc(sizeof(entry_t));
    new_entry->key = malloc(key_len);
    memcpy(new_entry->key, key, key_len);
    new_entry->key_length = key_len;
    
    new_entry->value = malloc(value_len);
    memcpy(new_entry->value, value, value_len);
    new_entry->value_length = value_len;
    
    pthread_mutex_init(&new_entry->lock, NULL);
    new_entry->next = NULL;
    
    if (prev) {
        prev->next = new_entry;
    } else {
        table->entries[index] = new_entry;
    }
    
    pthread_mutex_unlock(&table->locks[index]);
    send_response(sockfd, opcode, RES_OK, NULL, 0);
}

/* Handle DELETE */
void handle_delete(int sockfd, hash_table_t *table, const char *key, uint16_t key_len) {
    unsigned int index = hash_function(key, key_len);
    pthread_mutex_lock(&table->locks[index]);

    entry_t *entry = table->entries[index];
    entry_t *prev = NULL;

    while (entry) {
        if (entry->key_length == key_len && memcmp(entry->key, key, key_len) == 0) {
            // Remove the entry
            if (prev) {
                prev->next = entry->next;
            } else {
                table->entries[index] = entry->next;
            }

            pthread_mutex_lock(&entry->lock);
            free(entry->key);
            if (entry->value) {
                free(entry->value);
            }
            // unlock and destroy the entry's mutex
            pthread_mutex_unlock(&entry->lock);
            pthread_mutex_destroy(&entry->lock);
            free(entry);

            // release lock and send succcess response
            pthread_mutex_unlock(&table->locks[index]);
            send_response(sockfd, CMD_DELETE, RES_OK, NULL, 0);
            return;
        }
        prev = entry;
        entry = entry->next;
    }

    // key not found. release lock and send "Not Found" response
    pthread_mutex_unlock(&table->locks[index]);
    send_response(sockfd, CMD_DELETE, RES_NOT_FOUND, NULL, 0);
}

/* Handle VERSION */
void handle_version(int sockfd) {
    // Send a response containing the server version string
    send_response(sockfd, CMD_VERSION, RES_OK, VERSION_STR, strlen(VERSION_STR));
}

/* Handle OUTPUT */
// Have to print here
void handle_output(int sockfd, hash_table_t *table) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts); // Timestamp

    // Lock all buckets - acquire a global lock on the hash table
    for (int i = 0; i < HASH_SIZE; i++) {
        pthread_mutex_lock(&table->locks[i]);
    }

    // Iterate throuugh all key/value pairs 
    for (int i = 0; i < HASH_SIZE; i++) {
        entry_t *entry = table->entries[i];
        while (entry) {
            pthread_mutex_lock(&entry->lock);

            // Print seconds and nanosconds in hex
            printf("%lx:%lx:", (unsigned long)ts.tv_sec, (unsigned long)ts.tv_nsec);

            // Print key in hex
            for (int j = 0; j < entry->key_length; j++) {
                printf("%02x", (unsigned char)entry->key[j]);
            }
            printf(":");

            // Print value in hex
            for (int j = 0; j < entry->value_length; j++) {
                printf("%02x", (unsigned char)entry->value[j]);
            }
            printf("\n");

            pthread_mutex_unlock(&entry->lock);
            entry = entry->next;
        }
    }

    // Release the global lock
    for (int i = 0; i < HASH_SIZE; i++) {
        pthread_mutex_unlock(&table->locks[i]);
    }

    // Send a success response
    send_response(sockfd, CMD_OUTPUT, RES_OK, NULL, 0);
}

/* Calculate the hash for a key */
unsigned int hash_function(const char *key, int len) {
    unsigned int hash = 0;
    for (size_t i = 0; i < len; i++) {
        hash = (hash * 31) + key[i];
    }
    return hash % HASH_SIZE;
}

/* Send a response */
void send_response(int sockfd, uint8_t opcode, uint16_t status, const char *data, uint32_t data_len) {
    memcache_req_header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = 0x81;
    header.opcode = opcode;
    header.vbucket_id = htons(status);
    header.total_body_length = htonl(data_len);
    header.key_length = 0;
    header.extras_length = 0; 
    // send header
    write(sockfd, &header, sizeof(header));
    // send body
    if (data && data_len > 0) {
        write(sockfd, data, data_len);
    }
}
