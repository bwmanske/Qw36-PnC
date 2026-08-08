# Implementation Guide

## 1. Project File Structure

```
project/
├── CMakeLists.txt                  # Top-level CMake configuration
├── specs/
│   ├── spec.md                     # Umbrella specification
│   ├── Producer-Spec.md            # Producer detailed specification
│   └── Consumer-Spec.md            # Consumer detailed specification
├── docs/
│   ├── IMPLEMENTATION.md           # This document
│   └── USER_GUIDE.md               # End-user documentation
├── include/
│   ├── common/
│   │   ├── message.h               # JSON message types (work_unit, result, work_request)
│   │   ├── queue.h                 # Thread-safe bounded queue
│   │   ├── socket.h                # Platform-abstracted TCP/UDP socket wrapper
│   │   ├── types.h                 # Shared type definitions
│   │   └── checkpoint.h            # Checkpoint state read/write
│   ├── producer/
│   │   ├── producer.h              # Producer engine
│   │   └── work_tracker.h          # Work unit tracking table
│   └── consumer/
│       ├── consumer.h              # Consumer engine
│       └── thread_pool.h           # Consumer thread pool
├── src/
│   ├── common/
│   │   ├── message.cpp             # Message serialization/deserialization
│   │   ├── queue.cpp               # BoundedQueue implementation
│   │   ├── socket.cpp              # Platform-specific socket code (#ifdef _WIN32)
│   │   ├── signal_handler.cpp      # Platform-specific signal handling
│   │   └── checkpoint.cpp          # Checkpoint save/load logic
│   ├── producer/
│   │   ├── producer.cpp            # Producer engine implementation
│   │   ├── work_tracker.cpp        # Work unit lifecycle management
│   │   └── main.cpp                # Producer entry point, CLI parsing
│   └── consumer/
│       ├── consumer.cpp            # Consumer engine implementation
│       ├── thread_pool.cpp         # Thread pool implementation
│       └── main.cpp                # Consumer entry point, CLI parsing
└── tests/
    ├── CMakeLists.txt              # Test CMake configuration
    ├── test_message.cpp            # Message serialization round-trip
    ├── test_queue.cpp              # Thread-safe queue stress test
    ├── test_work_tracker.cpp       # Work unit lifecycle tests
    ├── test_checkpoint.cpp         # Checkpoint save/resume tests
    └── test_integration.cpp        # Producer-Consumer end-to-end
```

## 2. Build System

### Requirements

- CMake 3.20+
- C++17-compatible compiler (MSVC on Windows, GCC/Clang on Linux)
- No external dependencies at build time — all fetched via `FetchContent`

### Dependencies (FetchContent)

| Library       | Purpose                              |
|---------------|--------------------------------------|
| nlohmann/json | JSON serialization/deserialization   |
| GTest         | Unit and integration testing         |

### Build Commands

```bash
# Configure
cmake -B build

# Build all targets
cmake --build build

# Build with tests disabled
cmake -B build -DBUILD_TESTS=OFF
cmake --build build
```

### CMake Targets

CMake selects targets based on `CMAKE_SYSTEM_NAME`:

| Target           | Platform | Description            |
|------------------|----------|------------------------|
| `producer_win`   | Windows  | Producer CLI (MSVC)    |
| `producer_linux` | Linux    | Producer CLI (GCC/Clang) |
| `consumer_win`   | Windows  | Consumer CLI (MSVC)    |
| `consumer_linux` | Linux    | Consumer CLI (GCC/Clang) |

Platform-specific linking:
- Windows: `ws2_32` for WinSock2
- Linux: no extra libraries needed (POSIX sockets)

## 3. Platform Conditionals

All platform-specific code uses `#ifdef _WIN32` / `#else` guards. Areas requiring conditionals:

### Socket Layer (`socket.cpp`)

| Concern              | Windows                    | Linux                    |
|----------------------|----------------------------|--------------------------|
| Initialization       | `WSAStartup()`             | None                     |
| Cleanup              | `WSACleanup()`             | None                     |
| Socket type          | `SOCKET`                   | `int`                    |
| Close                | `closesocket()`            | `close()`                |
| Error code           | `WSAGetLastError()`        | `errno`                  |
| Address family       | `AF_INET`                  | `AF_INET`                |
| Socket type constant | `SOCK_STREAM` / `SOCK_DGRAM` | `SOCK_STREAM` / `SOCK_DGRAM` |

### Signal Handling (`signal_handler.cpp`)

| Concern              | Windows                    | Linux                    |
|----------------------|----------------------------|--------------------------|
| Handler registration | `signal(SIGINT, handler)`  | `sigaction(SIGINT, ...)` |
| Ctrl+C               | `SetConsoleCtrlHandler()`  | `SIGINT`                 |
| Termination          | `SIGTERM`                  | `SIGTERM`                |

### Hostname (`consumer.cpp`)

| Concern              | Windows                    | Linux                    |
|----------------------|----------------------------|--------------------------|
| Function             | `GetComputerNameExW()`     | `gethostname()`          |
| Header               | `<windows.h>`              | `<unistd.h>`             |

### File Paths

Use `std::filesystem` (C++17) wherever possible. It abstracts path separators
across platforms. Avoid hardcoding `/` or `\`.

## 4. Common Library Architecture

### `message.h` / `message.cpp`

Defines the three message types as structs with serialization helpers:

```cpp
// Conceptual structure (not final code)
enum class MessageType { WorkUnit, Result, WorkRequest };

struct WorkUnitMessage {
    std::string source_file;
    std::string permutation;
    int64_t permutation_seed;
    std::string work_unit_id;
    int64_t seq;
    std::string timestamp;
    std::string producer_id;
    nlohmann::json job;
};

struct ResultMessage {
    std::string work_unit_id;
    int64_t seq;
    std::string consumer_id;
    std::string status;  // "success" or "failure"
    nlohmann::json result;
    std::string timestamp;
};

struct WorkRequestMessage {
    std::string consumer_id;
    int threads_available;
    std::string timestamp;
};
```

Each struct provides:
- `to_json()` → `nlohmann::json`
- `from_json(const nlohmann::json&)` → struct
- `to_string()` → serialized JSON string
- `from_string(const std::string&)` → struct (static)

### `queue.h` / `queue.cpp`

Bounded, thread-safe queue:

```cpp
// Conceptual structure
template<typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t capacity);
    void push(T item);              // Blocks if full
    T pop();                        // Blocks if empty, throws on shutdown
    bool try_push(T item, std::chrono::milliseconds timeout);
    bool try_pop(T& item, std::chrono::milliseconds timeout);
    size_t size() const;
    void shutdown();                // Unblocks all waiters
private:
    std::mutex mtx_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
    std::deque<T> buffer_;
    size_t capacity_;
    bool shutdown_ = false;
};
```

### `socket.h` / `socket.cpp`

Platform-abstracted socket wrapper supporting both TCP and UDP:

```cpp
// Conceptual structure
enum class Transport { TCP, UDP };

class Socket {
public:
    Socket(Transport transport);
    ~Socket();
    void bind(const std::string& address, uint16_t port);
    void listen(int backlog = 5);
    Socket accept();               // TCP only, returns new connected socket
    void connect(const std::string& address, uint16_t port);
    ssize_t send(const uint8_t* data, size_t len);
    ssize_t recv(uint8_t* buffer, size_t maxlen);
    void close();
    // UDP-specific
    ssize_t sendto(const std::string& address, uint16_t port, const uint8_t* data, size_t len);
    ssize_t recvfrom(std::string& address, uint16_t& port, uint8_t* buffer, size_t maxlen);
};
```

Frame helper functions (length-prefixed):
- `send_frame(Socket& sock, const std::string& json)` — writes 4-byte length + payload
- `recv_frame(Socket& sock)` — reads 4-byte length, then reads payload, returns string

### `signal_handler.cpp`

Sets up platform-appropriate signal handling:

```cpp
// Conceptual structure
class SignalHandler {
public:
    static void install();         // Register handlers for SIGINT/SIGTERM
    static bool is_stop_requested();
    static void reset();
private:
    static std::atomic<bool> stop_requested_;
};
```

### `checkpoint.h` / `checkpoint.cpp`

Reads and writes checkpoint state:

```cpp
// Conceptual structure
struct CheckpointState {
    std::string producer_id;
    std::string source_file;
    std::string permutation;
    int64_t permutation_seed;
    int64_t total_jobs;
    int64_t last_completed_seq;
    std::string last_completed_work_unit_id;
    int64_t completed_count;
    int64_t pending_count;
    int64_t failed_count;
    std::string checkpoint_timestamp;
    std::vector<ConsumerState> consumers_connected;
};

class CheckpointManager {
public:
    explicit CheckpointManager(const std::string& directory);
    void save(const CheckpointState& state);   // Writes backup, then primary
    CheckpointState load() const;              // Tries primary, falls back to backup
    bool exists() const;
private:
    std::string dir_;
    std::string primary_path_;
    std::string backup_path_;
};
```

## 5. Producer Architecture

### `work_tracker.h` / `work_tracker.cpp`

Maintains the work unit tracking table:

```cpp
// Conceptual structure
enum class WorkUnitStatus { Pending, Sent, Completed, Failed };

struct WorkUnitEntry {
    std::string work_unit_id;
    int64_t seq;
    nlohmann::json job;
    WorkUnitStatus status;
    std::string consumer_id;
    std::string sent_at;
    std::string completed_at;
};

class WorkTracker {
public:
    void add_pending(const WorkUnitEntry& entry);
    void mark_sent(const std::string& work_unit_id, const std::string& consumer_id);
    void mark_completed(const std::string& work_unit_id);
    void mark_failed(const std::string& work_unit_id);  // Returns to pending
    std::vector<WorkUnitEntry> get_pending(int count);   // For dispatch
    WorkUnitEntry find(const std::string& work_unit_id) const;
    CheckpointState to_checkpoint() const;
    int64_t last_completed_seq() const;
};
```

### `producer.h` / `producer.cpp`

Main Producer engine coordinating all threads:

```cpp
// Conceptual structure
class Producer {
public:
    Producer(const ProducerConfig& config);
    void run();
    void shutdown();
private:
    void load_job_file();
    void apply_permutation();
    void load_checkpoint();
    void dispatcher_loop();
    void handle_work_request(const WorkRequestMessage& req, Socket& client);
    void handle_result(const ResultMessage& result);
    void checkpoint_loop();
    // ... members
};
```

### `main.cpp`

Entry point:
1. Parse CLI arguments
2. Validate `--file`
3. Check for checkpoint (if `--resume`)
4. Construct and run `Producer`
5. Handle signals
6. Print statistics

## 6. Consumer Architecture

### `thread_pool.h` / `thread_pool.cpp`

Thread pool with work request coordination:

```cpp
// Conceptual structure
class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();
    void start();
    void shutdown();
    void submit(WorkUnitMessage work);
    size_t idle_count() const;
    void on_idle();  // Called by threads when queue is empty
private:
    std::vector<std::thread> workers_;
    BoundedQueue<WorkUnitMessage> queue_;
    std::atomic<size_t> idle_count_;
    // ... members
};
```

### `consumer.h` / `consumer.cpp`

Main Consumer engine:

```cpp
// Conceptual structure
class Consumer {
public:
    Consumer(const ConsumerConfig& config);
    void run();
    void shutdown();
private:
    void connect_to_producer();
    void receiver_loop();
    void download_source_file(const std::string& source_file);
    void send_work_request(int threads_available);
    void send_result(const ResultMessage& result);
    void process_work_unit(WorkUnitMessage work);
    // ... members
};
```

### `main.cpp`

Entry point:
1. Parse CLI arguments
2. Generate `consumer_id` (if not provided)
3. Construct and run `Consumer`
4. Handle signals
5. Print statistics

## 7. Network Protocol

### TCP Framing

```
┌─────────────┬────────────────────────┐
│ 4-byte BE   │ N-byte JSON payload    │
│ uint32_t    │ (UTF-8)                │
│ (length)    │                        │
└─────────────┴────────────────────────┘
```

### File Transfer Protocol (port + 1)

**Request (Consumer → Producer):**
```
┌──────────┬──────────────────┬──────┐
│ 0x01     │ filename (UTF-8) │ \0   │
│ (1 byte) │                  │      │
└──────────┴──────────────────┴──────┘
```

**Response (Producer → Consumer):**
```
┌──────────────┬──────────────────┐
│ 4-byte BE    │ file contents    │
│ uint32_t     │ (raw bytes)      │
│ (file size)  │                  │
└──────────────┴──────────────────┘
```

Zero-byte response means file not found.

## 8. Testing Strategy

| Test File              | What It Tests                          |
|------------------------|----------------------------------------|
| `test_message.cpp`     | JSON round-trip for all 3 message types |
| `test_queue.cpp`       | BoundedQueue concurrency, shutdown     |
| `test_work_tracker.cpp`| Work unit lifecycle, status transitions |
| `test_checkpoint.cpp`  | Save/load, backup, resume              |
| `test_integration.cpp` | Spawn Producer, connect Consumer, verify full cycle |

## 9. Implementation Order (Recommended)

1. **Common library** — `types.h`, `message.h/cpp`, `queue.h/cpp`
2. **Socket layer** — `socket.h/cpp` with TCP framing
3. **Signal handler** — `signal_handler.cpp`
4. **Producer core** — `work_tracker.h/cpp`, `producer.h/cpp`, `main.cpp`
5. **Consumer core** — `thread_pool.h/cpp`, `consumer.h/cpp`, `main.cpp`
6. **Checkpoint** — `checkpoint.h/cpp`
7. **File transfer** — secondary TCP connection
8. **UDP support** — extend socket layer
9. **Tests** — unit tests, then integration test
10. **Polish** — statistics, error handling, edge cases

## 10. Known Design Decisions

- IPv4 only — no IPv6 support in Phase 1
- Big-endian for all network integers (cross-platform consistency)
- Checkpoint backup written before primary (atomic-ish safety)
- Consumer thread pool defaults to `std::thread::hardware_concurrency()`
- Work request throttling: max 1 request per 50ms per consumer connection
- File transfer on `port + 1` (simple, no multiplexing needed)
- SHA-256 for file integrity verification
- `nlohmann::json` for all JSON work (header-only, FetchContent)
