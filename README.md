# Document Search Service

Client-server system for indexing and searching text documents stored locally, developed for an Operating Systems course at Universidade do Minho. The server manages a persistent document store with an in-memory cache and communicates with clients exclusively through **named pipes (FIFOs)**, leveraging multi-process concurrency via `fork()` for parallel query handling.

## Grade

**Final Grade:** 16 / 20 ⭐

## Authors

- *Gabriel Dantas* -> [@gabil88](https://github.com/gabil88)
- *José Fernandes* -> [@JoseLourencoFernandes](https://github.com/JoseLourencoFernandes)
- *Simão Oliveira* -> [@SimaoOliveira05](https://github.com/SimaoOliveira05)

## Features

- 🗂️ **Document Indexing** — Add, consult, and delete document metadata with persistent binary storage
- 🔍 **Keyword Search** — Search across all documents for a keyword, with parallel multi-process support
- 📄 **Line Counting** — Count lines containing a keyword in a specific document (via `grep | wc -l` pipelines)
- ⚡ **In-Memory Cache** — Configurable cache with **LRU** and **Least Used** eviction policies
- 🔀 **Multi-Process Concurrency** — Three-level `fork()` architecture to handle queries without blocking the main loop
- 📡 **Named Pipes (FIFOs)** — All client-server communication through FIFOs; no sockets or shared memory
- 📝 **Logging** — Server redirects `stderr` to a log file via `dup2()`

## OS Concepts Used

| Concept | Usage |
|---------|-------|
| `fork()` | Multi-level process creation for concurrent query handling |
| `exec()` / `execlp()` | Running `grep \| wc -l` shell pipelines for keyword search |
| `pipe()` | Anonymous pipes for parent↔child data transfer |
| `mkfifo()` | Named pipes for all client-server communication |
| `open()` / `read()` / `write()` | Low-level I/O throughout (no `fopen`/`fprintf` for data) |
| `lseek()` | O(1) direct-access document storage by key |
| `dup2()` | Redirecting stderr to log file; stdout to pipe |
| `waitpid()` | Reaping child/grandchild processes; zombie prevention |
| `O_NONBLOCK` | Non-blocking reads on internal FIFO for priority handling |

## Requirements

- GCC
- GLib 2.0

```bash
# Ubuntu / Debian
sudo apt-get install libglib2.0-dev
```

## Building

```bash
make
```

## Running

### Start the server

```bash
./bin/dserver <dataset_path> <cache_size>
```

Example:

```bash
./bin/dserver GDatasetTest/Gdataset 10
```

### Client commands

```bash
./bin/dclient <option> [arguments]
```

| Command | Description | Example |
|---------|-------------|---------|
| `-a "title" "authors" "year" "path"` | Index a new document | `./bin/dclient -a "Romeo and Juliet" "William Shakespeare" 1997 "1112.txt"` |
| `-c <key>` | Consult document metadata | `./bin/dclient -c 1` |
| `-d <key>` | Delete a document | `./bin/dclient -d 1` |
| `-l <key> <keyword>` | Count lines with keyword | `./bin/dclient -l 1 "amor"` |
| `-s <keyword>` | Search documents for keyword | `./bin/dclient -s "praia"` |
| `-s <keyword> <nr_processes>` | Parallel keyword search | `./bin/dclient -s "praia" 5` |
| `-f` | Shutdown the server | `./bin/dclient -f` |

## Cache

The server includes a configurable in-memory cache with hit/miss tracking and two eviction policies:

| Policy | Strategy |
|--------|----------|
| **LRU** | Evicts the entry with the oldest access timestamp |
| **Least Used** | Evicts the entry with the fewest total accesses |

Cache statistics (hit rate) are printed on server shutdown.

## Architecture

```
┌──────────┐    FIFO     ┌──────────────────────────────────────┐
│  dclient │ ──────────> │             dserver                  │
│          │ <────────── │                                      │
└──────────┘  client FIFO│  ┌─────────┐  ┌───────┐  ┌───────┐ │
                         │  │  Cache   │  │ Store │  │  Log  │ │
                         │  └─────────┘  └───────┘  └───────┘ │
                         │                                      │
                         │  fork() ──> child ──> grandchild     │
                         └──────────────────────────────────────┘
```

## Project Structure

```
.
├── bin/            # Compiled binaries (dserver, dclient)
├── src/
│   ├── server/     # Server source files
│   └── client/     # Client source files
├── include/
│   ├── server/     # Server headers
│   └── client/     # Client headers
├── storage/        # Persistent document storage
├── logs/           # Server log files
├── scripts/        # Test/utility scripts
├── tests/          # Test scripts
├── target/         # Compiled object files (.o)
├── Makefile        # Build script
└── README.md
```

## Cleanup

```bash
make clean
```

## Technologies

- **C** (POSIX)
- **Named Pipes (FIFOs)** for IPC
- **Multi-process concurrency** (`fork` / `exec` / `waitpid`)
- **GLib 2.0** for dynamic arrays
- **Low-level system calls** (`open`, `read`, `write`, `lseek`, `dup2`, `pipe`, `mkfifo`)

