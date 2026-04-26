# C Memory Cache Daemon (mcached) 

A multithreaded in-memory key-value store written in C, inspired by memcached. Worker threads independently accept and handle client connections over TCP,
with a fine-grained locked hash table for thread-safe concurrent access.

## Features
- Binary memcached protocol (request/response over TCP) 
- Concurrent client handling via multiple worker threads sharing a server socket
- Hash table with per-bucket mutex locking for concurrent access
- Supports GET, SET, ADD, DELETE, VERSION, and OUTPUT commands 

## Build
gcc -o mcached mcached.c -lpthread

## Usage
./mcached <port> <num_threads>

## Testing
You can test the server using a binary client or by sending raw binary
frames. The server speaks the memcached binary protocol (magic byte 0x80).

A compatible client must construct the memcache_req_header_t packet
as defined in mcached.h.

## Protocol
The server implements a subset of the memcached binary protocol: 

| Opcode | Command  | Description                        |
|--------|----------|------------------------------------|
| 0x00   | GET      | Retrieve value by key              |
| 0x01   | SET      | Set a key-value pair               |
| 0x02   | ADD      | Add key only if it does not exist  |
| 0x04   | DELETE   | Delete a key                       |
| 0x0b   | VERSION  | Returns server version string      |
| 0x0c   | OUTPUT   | Dumps all key-value pairs to stdout|

### Response Status Codes

| Status | Meaning   |
|--------|-----------|
| 0x0000 | OK        |
| 0x0001 | Not Found |
| 0x0002 | Exists    |
| 0x0004 | Error     |

## Implementation Details
**Binary Protocol**
- Fixed 24-byte request/response header (packed struct)
- Network byte order for multi-byte fields
- Magic byte 0x80 (request) / 0x81 (response)

**Hash Table**
- 1024 buckets with separate chaining for collision resolution
- Each bucket has its own mutex for fine-grained locking
- Each entry also has its own mutex for safe value updates 

**Concurrency Model**
- N worker threads are created at startup, each independently calling accept() 
- Threads compete for incoming connections directly on the shared server socket 
- Each thread handles one client connection at a time from start to finish
- Thread count is configurable at startup 