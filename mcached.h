

/* header file for the mcached protocol.
 */ 
#ifdef _CACHED_
#define _CACHED_
#else

#define CMD_GET     0x00
#define CMD_SET     0x01
#define CMD_ADD     0x02
#define CMD_DELETE  0x04
#define CMD_VERSION 0x0b
#define CMD_OUTPUT  0x0c
#define RES_OK         0x0000
#define RES_NOT_FOUND  0x0001
#define RES_EXISTS     0x0002
#define RES_ERROR      0x0004

typedef struct {
    uint8_t magic;
    uint8_t opcode;
    uint16_t key_length;
    uint8_t extras_length;
    uint8_t data_type;
    uint16_t vbucket_id;
    uint32_t total_body_length;
    uint32_t opaque;
    uint64_t cas;
} __attribute__((packed)) memcache_req_header_t;

//#endif

// Entry struct for key-value pairs
typedef struct entry {
    char *key;
    uint16_t key_length;
    char *value;
    uint32_t value_length;
    pthread_mutex_t lock;
    struct entry *next;
} entry_t;

// Hash table 
typedef struct {
    entry_t *entries[1024];
    pthread_mutex_t locks[1024];
} hash_table_t;

// Thread 
typedef struct {
    int server_fd;
    hash_table_t *table;
} thread_arg_t;


#define CMD_GET 0x00
#define CMD_SET 0x01
#define CMD_ADD 0x02
#define CMD_DELETE 0x04
#define CMD_VERSION 0x0b
#define CMD_OUTPUT 0x0c

#define RES_OK 0x0000
#define RES_NOT_FOUND 0x0001
#define RES_EXISTS 0x0002
#define RES_ERROR 0x0004

#endif

void *worker_thread(void *arg);
void process_client(int client_fd, hash_table_t *table);
unsigned int hash_function(const char *key, int len);
hash_table_t *create_hash_table();
void handle_get(int sockfd, hash_table_t *table, const char *key, uint16_t key_len);
void handle_set_add(int sockfd, hash_table_t *table, const char *key, uint16_t key_len, const char *value, uint32_t value_len, uint8_t opcode);
void handle_delete(int sockfd, hash_table_t *table, const char *key, uint16_t key_len);
void handle_version(int sockfd);
void handle_output(int sockfd, hash_table_t *table);
void send_response(int sockfd, uint8_t opcode, uint16_t status, const char *data, uint32_t data_len);


