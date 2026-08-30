# Implementation Guide

## 1. Project File Structure

```
project/
├── CMakeLists.txt                  # Top-level CMake configuration
├── AGENTS.md                       # Build/test instructions, MSVC gotchas, architecture
├── specs/
│   ├── spec.md                     # Umbrella specification
│   ├── Producer-Spec.md            # Producer detailed specification
│   └── Consumer-Spec.md            # Consumer detailed specification
├── docs/
│   ├── BUILDING.md                 # Build instructions (Windows/Linux)
│   ├── COMMUNICATION.md            # Network protocol reference
│   ├── IMPLEMENTATION.md           # This document
│   ├── PROGRESS.md                 # Remaining tasks tracker
│   ├── TESTING.md                  # Testing strategy
│   └── USER_GUIDE.md               # End-user documentation
├── include/
│   ├── common/
│   │   ├── archive_validator.h     # libarchive wrapper for ZIP/RAR/7Z validation
│   │   ├── checkpoint.h            # CheckpointState + CheckpointManager
│   │   ├── message.h               # 5 message types: version, work_unit, result, work_request, heartbeat
│   │   ├── queue.h                 # Thread-safe bounded queue (template, header-only impl)
│   │   ├── signal_handler.h        # Platform-abstracted signal handling
│   │   ├── socket.h                # Platform-abstracted TCP/UDP socket wrapper
│   │   ├── types.h                 # Shared enums and structs (Transport, MessageType, etc.)
│   │   ├── util.h                  # SHA-256 (RFC 6234) + get_data_directory()
│   │   └── version.h               # PC_VERSION constant
│   ├── consumer/
│   │   ├── consumer.h              # Consumer engine
│   │   ├── file_result_sink.h      # JSON-lines result sink with running stats
│   │   ├── result_sink.h           # IResultSink interface
│   │   ├── thread_pool.h           # Consumer thread pool with callbacks
│   │   ├── work_unit_handler.h     # IWorkUnitHandler interface
│   │   ├── PWD_Handler.h           # Password brute-force handler
│   │   ├── BENCH_Handler.h         # File chunk benchmark handler
│   │   └── ECHO_Handler.h          # Echo/verification handler
│   └── producer/
│       ├── producer.h              # Producer engine
│       ├── work_tracker.h          # Work unit tracking table
│       ├── test_plugin.h           # TestPlugin dispatch table (4 std::function members)
│       ├── PWD_plugin.h            # Password brute-force plugin factory
│       ├── BENCH_plugin.h          # File chunk benchmark plugin factory
│       └── ECHO_plugin.h           # Echo/verification plugin factory
├── src/
│   ├── common/
│   │   ├── archive_validator.cpp   # libarchive integration
│   │   ├── checkpoint.cpp          # Checkpoint save/load logic
│   │   ├── message.cpp             # Message serialization/deserialization
│   │   ├── queue.cpp               # (Empty — template impl is in header)
│   │   ├── signal_handler.cpp      # Platform-specific signal handling
│   │   ├── socket.cpp              # Platform-specific socket code (pimpl)
│   │   └── util.cpp                # SHA-256 + data directory
│   ├── consumer/
│   │   ├── consumer.cpp            # Consumer engine implementation
│   │   ├── main.cpp                # Consumer entry point, CLI parsing
│   │   ├── file_result_sink.cpp    # JSON-lines output implementation
│   │   ├── PWD_Handler.cpp         # PWD handler implementation
│   │   ├── BENCH_Handler.cpp       # BENCH handler implementation
│   │   ├── ECHO_Handler.cpp        # ECHO handler implementation
│   │   └── thread_pool.cpp         # Thread pool implementation
│   └── producer/
│       ├── main.cpp                # Producer entry point, CLI parsing
│       ├── producer.cpp            # Producer engine implementation
│       ├── work_tracker.cpp        # Work unit lifecycle management
│       ├── PWD_NextUnit.h          # Legacy C-style permutation generator
│       ├── PWD_NextUnit.cpp        # Legacy C-style permutation generator
│       ├── PWD_plugin.cpp          # PWD TestPlugin wrapper
│       ├── BENCH_plugin.cpp        # BENCH TestPlugin implementation
│       └── ECHO_plugin.cpp         # ECHO TestPlugin implementation
└── tests/
    ├── CMakeLists.txt              # Test CMake configuration
    ├── test_checkpoint.cpp         # Checkpoint save/resume tests
    ├── test_echo.cpp               # ECHO plugin + handler (generation, resume, hash verify)
    ├── test_file_result_sink.cpp   # FileResultSink JSON lines + stats
    ├── test_integration.cpp        # Producer-Consumer end-to-end (spawns real processes)
    ├── test_message.cpp            # Message serialization round-trip (all 5 types)
    ├── test_pwd_next_unit.cpp      # PWD_NextUnit permutation engine
    ├── test_queue.cpp              # Thread-safe queue stress test
    ├── test_sha256.cpp             # SHA-256 RFC 6234 vectors + file hash
    ├── test_socket.cpp             # TCP/UDP frame round-trip tests
    ├── test_thread_pool.cpp        # ThreadPool dispatch, idle callback, drain, shutdown
    ├── test_util.cpp               # parse_duration tests
    └── test_work_tracker.cpp       # Work unit lifecycle tests
```

## 2. Build System

### Requirements

- CMake 3.20+
- C++17-compatible compiler (MSVC on Windows, GCC/Clang on Linux)
- No external dependencies at build time — all fetched via `FetchContent`

### Dependencies (FetchContent)

| Library       | Version | Purpose                              |
|---------------|---------|--------------------------------------|
| nlohmann/json | v3.11.3 | JSON serialization/deserialization   |
| GTest         | v1.14.0 | Unit and integration testing         |
| zlib          | v1.3.1  | Required by libarchive for ZIP       |
| libarchive    | v3.7.9  | ZIP, RAR, 7Z archive validation      |

libarchive is built static with only ZIP, RAR, and 7ZIP formats enabled. All other
compression backends (bzip2, lzma, zstd, lz4, xz, etc.) are disabled to minimize
build time and binary size.

### Build Commands

```powershell
# Configure (one-time or after CMakeLists.txt changes)
cmake -B build -DBUILD_TESTS=ON

# Build Release
cmake --build build --config Release

# Run tests (ctest does NOT discover tests on Windows; run executables directly):
build\tests\Release\test_message.exe
build\tests\Release\test_queue.exe
build\tests\Release\test_work_tracker.exe
build\tests\Release\test_checkpoint.exe
build\tests\Release\test_integration.exe
build\tests\Release\test_pwd_next_unit.exe
build\tests\Release\test_sha256.exe
build\tests\Release\test_file_result_sink.exe
build\tests\Release\test_util.exe
build\tests\Release\test_thread_pool.exe
build\tests\Release\test_echo.exe
build\tests\Release\test_socket.exe
```

### CMake Targets

| Target           | Type      | Description                                      |
|------------------|-----------|--------------------------------------------------|
| `common`         | static lib| Sockets, messages, queue, checkpoint, SHA-256, signal handling, archive validator |
| `producer_lib`   | static lib| Producer logic, work tracker, PWD_NextUnit, PWD/BENCH/ECHO plugins |
| `consumer_lib`   | static lib| Consumer logic, thread pool, PWD/BENCH/ECHO handlers, file result sink |
| `producer`       | executable| CLI entry point                                  |
| `consumer`       | executable| CLI entry point                                  |

Tests link against `common`, `producer_lib`, and/or `consumer_lib` — never the executables.

Platform-specific linking:
- Windows: `ws2_32` for WinSock2
- Linux: no extra libraries needed (POSIX sockets)

## 3. Platform Conditionals

All platform-specific code uses `#ifdef _WIN32` / `#else` guards.

### Socket Layer (`socket.cpp` — pimpl pattern)

| Concern              | Windows                    | Linux                    |
|----------------------|----------------------------|--------------------------|
| Initialization       | `WSAStartup()`             | None                     |
| Cleanup              | `WSACleanup()`             | None                     |
| Socket type          | `SOCKET`                   | `int`                    |
| Close                | `closesocket()`            | `close()`                |
| Error code           | `WSAGetLastError()`        | `errno`                  |
| Recv timeout         | `setsockopt(SO_RCVTIMEO, struct timeval)` | `setsockopt(SO_RCVTIMEO, struct timeval)` |

The `Socket` class uses a pimpl (`class Impl`) to hide platform-specific members.
Methods `send_data`/`recv_data` are named to avoid WinSock2 macro collisions.

### Signal Handling (`signal_handler.cpp`)

| Concern              | Windows                    | Linux                    |
|----------------------|----------------------------|--------------------------|
| Handler registration | `SetConsoleCtrlHandler()`  | `sigaction(SIGINT, ...)` |
| Ctrl+C               | `CTRL_C_EVENT`             | `SIGINT`                 |
| Termination          | `CTRL_BREAK_EVENT`         | `SIGTERM`                |

### Hostname (`consumer.cpp`)

| Concern              | Windows                    | Linux                    |
|----------------------|----------------------------|--------------------------|
| Function             | `GetComputerNameExW()`     | `gethostname()`          |
| Header               | `<windows.h>`              | `<unistd.h>`             |

### Data Directory (`util.cpp`)

| Platform | Path                              |
|----------|-----------------------------------|
| Windows  | `%APPDATA%\Producer\`             |
| Linux    | `~/.local/share/producer/`        |

`get_data_directory()` creates the directory if it does not exist.

### File Paths

Use `std::filesystem` (C++17) wherever possible. It abstracts path separators
across platforms. Avoid hardcoding `/` or `\`.

## 4. MSVC Gotchas

- **Always `#define NOMINMAX` before `<windows.h>`** — MSVC's `min`/`max` macros break `std::min`/`std::max`.
- **Never name functions `send` or `recv`** — WinSock2 defines them as macros. Use `send_data`/`recv_data` (already done in `socket.h/cpp`).
- **`ssize_t` not defined on Windows** — the `common/` namespace provides `using ssize_t = int;` in `socket.h`.
- **`std::istreambuf_iterator` unreliable on MSVC** — use `tellg` + `seekg` + `read` for file-to-vector operations.
- **libarchive `__declspec(dllimport)` mismatch** — `#define LIBARCHIVE_STATIC` is set via `target_compile_definitions` in CMake.

## 5. Common Library Architecture

### `types.h`

Shared type definitions in the `pc` namespace:

```cpp
enum class Transport { TCP, UDP };
enum class MessageType { WorkUnit, Result, WorkRequest };
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

struct ConsumerState {
    std::string consumer_id;
    int pending_units;
};
```

### `message.h` / `message.cpp`

Five message types, each with `to_json()`, `from_json()`, `to_string()`, `from_string()`:

```cpp
struct VersionMessage {
    std::string version;
    std::string consumer_id;
};

struct WorkUnitMessage {
    std::string test_type;
    std::string source_file;
    std::string permutation;
    std::optional<int64_t> permutation_seed;
    std::string work_unit_id;
    int64_t seq;
    std::string timestamp;
    std::string producer_id;
    nlohmann::json job;
    std::optional<std::string> source_hash;
};

struct ResultMessage {
    std::string work_unit_id;
    int64_t seq;
    std::string consumer_id;
    std::string status;          // "success" or "failure"
    nlohmann::json result;
    std::string timestamp;
    std::optional<std::string> found_password;
    std::optional<std::string> file_error;
};

struct WorkRequestMessage {
    std::string consumer_id;
    int threads_available;
    std::string timestamp;
};

struct HeartbeatMessage {
    std::string consumer_id;
    std::string timestamp;
};
```

`VersionMessage` is exchanged as the first message on connection (TCP or UDP) to verify
that producer and consumer share the same protocol version (`PC_VERSION` from `version.h`).

### `version.h`

Single-line version constant:

```cpp
constexpr const char* PC_VERSION = "0.6";
```

Incremented by 0.1 whenever the producer-consumer communication protocol changes.

### `queue.h`

Thread-safe bounded queue — full template implementation in the header:

```cpp
template<typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t capacity);
    void push(T item);                          // Blocks if full
    T pop();                                    // Blocks if empty, throws on shutdown
    bool try_push(T item, std::chrono::milliseconds timeout);
    std::optional<T> try_pop(std::chrono::milliseconds timeout);
    size_t size() const;
    bool empty() const;
    void shutdown();                            // Unblocks all waiters
private:
    std::deque<T> buffer_;
    size_t capacity_;
    mutable std::mutex mtx_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
    bool shutdown_ = false;
};
```

### `socket.h` / `socket.cpp`

Platform-abstracted socket wrapper with pimpl pattern:

```cpp
class Socket {
public:
    explicit Socket(Transport transport = Transport::TCP);
    // Non-copyable, movable
    void bind(const std::string& address, uint16_t port);
    void listen(int backlog = 5);
    Socket accept();                            // TCP only
    void connect(const std::string& address, uint16_t port);
    ssize_t send_data(const uint8_t* data, size_t len);
    ssize_t recv_data(uint8_t* buffer, size_t maxlen);
    void set_recv_timeout(int milliseconds);    // SO_RCVTIMEO
    // UDP-specific
    ssize_t send_to(const std::string& address, uint16_t port, const uint8_t* data, size_t len);
    ssize_t recv_from(std::string& address, uint16_t& port, uint8_t* buffer, size_t maxlen);
    void close();
    bool is_open() const;
};

// Frame helpers (length-prefixed)
void send_frame(Socket& sock, const std::string& json);
std::string recv_frame(Socket& sock);
void send_frame_udp(Socket& sock, const std::string& address, uint16_t port, const std::string& json);
std::string recv_frame_udp(Socket& sock, std::string& from_address, uint16_t& from_port);
```

### `signal_handler.h` / `signal_handler.cpp`

Platform-appropriate signal handling:

```cpp
class SignalHandler {
public:
    static void install();
    static bool is_stop_requested();
    static void reset();
private:
    static std::atomic<bool> stop_requested_;
    // Platform-specific callback (console_handler on Windows, signal_callback on Linux)
};
```

### `checkpoint.h` / `checkpoint.cpp`

Checkpoint state with plugin state support:

```cpp
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
    std::optional<nlohmann::json> plugin_state;
    // to_json() / from_json()
};

class CheckpointManager {
public:
    explicit CheckpointManager(const std::string& directory);
    void save(const CheckpointState& state);     // Writes backup, then primary
    std::optional<CheckpointState> load() const;  // Tries primary, falls back to backup
    bool exists() const;
    const std::string& directory() const;
    const std::string& primary_path() const;
    const std::string& backup_path() const;
};
```

### `util.h` / `util.cpp`

Pure C++ SHA-256 (RFC 6234 implementation), platform data directory, and duration parsing:

```cpp
std::string sha256_file(const std::string& path);
std::string sha256_bytes(const uint8_t* data, size_t len);
std::string get_data_directory();
int parse_duration(const std::string& s);   // "30" → 30, "30s" → 30, "5m" → 300, "1h" → 3600; "" → 0
```

### `archive_validator.h` / `archive_validator.cpp`

libarchive wrapper for password-protected archive validation:

```cpp
class ArchiveValidator {
public:
    enum class Error { None, WrongPassword, FileError };
    struct Result {
        bool valid = false;
        Error error = Error::None;
        std::string message;
    };
    static Result validate(const std::string& path, const std::string& password);
};
```

Supports ZIP, RAR, and 7Z formats. Used by `PWD_Handler` to verify candidate passwords.

## 6. Producer Architecture

### `test_plugin.h` — Plugin Dispatch Table

The plugin system uses a struct of `std::function` members:

```cpp
struct TestPlugin {
    std::function<void(const std::string&, const nlohmann::json&)> startup;
    //   (config_path, resume_state) — reads plugin config, restores from checkpoint

    std::function<bool(WorkUnitMessage&)> next_unit;
    //   (out) — generates next work unit, returns false when exhausted

    std::function<nlohmann::json()> checkpoint;
    //   () — returns plugin-specific state for checkpoint merge

    std::function<bool()> exit_conditions;
    //   () — returns true when plugin wants to stop

    std::function<std::string()> status;
    //   () — optional "output plugin": returns plugin-specific status lines
    //        for the producer's in-place console display (may be empty)

    bool is_valid() const;  // The first four functions must be non-null
};
```

### Existing Plugins

| Plugin | Creator Function | Purpose |
|--------|-----------------|---------|
| `PWD` | `create_pwd_plugin()` | Password permutation generator, wraps legacy `PWD_NextUnit` |
| `BENCH` | `create_bench_plugin()` | File chunk benchmark |
| `ECHO` | `create_echo_plugin()` | Sequential payload generator (A-Z cycling), configurable size and total units |

### `work_tracker.h` / `work_tracker.cpp`

Maintains the work unit tracking table with thread-safe access:

```cpp
class WorkTracker {
public:
    void add_pending(const WorkUnitEntry& entry);
    void mark_sent(const std::string& work_unit_id, const std::string& consumer_id);
    void mark_completed(const std::string& work_unit_id);
    void mark_failed(const std::string& work_unit_id);
    std::vector<WorkUnitEntry> get_pending(int count);
    std::optional<WorkUnitEntry> find(const std::string& work_unit_id) const;
    int64_t last_completed_seq() const;
    int64_t completed_count() const;
    int64_t pending_count() const;
    int64_t failed_count() const;
    std::vector<std::string> get_failed_for_consumer(const std::string& consumer_id);
    CheckpointState to_checkpoint(int64_t total_jobs) const;
};
```

### `producer.h` / `producer.cpp`

Main Producer engine with multi-threaded architecture:

```cpp
class Producer {
    // Core lifecycle
    void run();
    void shutdown();

    // Job loading
    void load_job_config();    // Reads JSON config, extracts test_type, source_file, etc.
    void load_checkpoint();    // Restores from checkpoint if --resume
    void init_plugin();        // Creates TestPlugin based on test_type

    // Threading
    void dispatcher_loop();           // Pops from dispatch_queue_, sends to consumers
    void handle_client(Socket);       // Per-consumer connection handler
    void checkpoint_loop();           // Periodic checkpoint saves
    void file_transfer_loop();        // Listens on port+1 for file requests
    void monitor_connections();       // Detects stale consumers, disconnects after 30s

    // Message handling
    void handle_work_request(const WorkRequestMessage&, Socket&);
    void handle_result(const ResultMessage&);

    // Consumer registration
    void register_consumer(const std::string& consumer_id, Socket& socket);
    void register_consumer_udp(const std::string& consumer_id, const std::string& address, uint16_t port);
    void unregister_consumer(const std::string& consumer_id);
    void update_consumer_activity(const std::string& consumer_id);

    // UDP
    void udp_loop();
    void handle_udp_message(const std::string& consumer_id, const std::string& address, uint16_t port, const std::string& frame);
    void handle_udp_work_request(const WorkRequestMessage& req, const std::string& address, uint16_t port);

    // Internal
    struct ConsumerInfo {
        Socket* socket;
        std::string address;
        uint16_t port;
        std::chrono::steady_clock::time_point last_activity;
        std::chrono::steady_clock::time_point registered_at;
    };
};
```

Key threads:
- **Main thread**: Accepts TCP connections, spawns `handle_client` per consumer; idle loop for UDP
- **UDP thread** (`udp_loop`): Receives datagrams, dispatches to `handle_udp_message`
- **Dispatcher thread**: Generates work units via plugin, pushes to `dispatch_queue_`
- **Checkpoint thread**: Periodically saves state via `CheckpointManager`
- **File transfer thread**: Listens on `port + 1` for file download requests
- **Monitor thread**: Scans `connected_consumers_` for stale connections (>30s idle)
- **Status thread** (`status_loop`): Renders the in-place console status block once per second (disabled by `--no-status`); `status_mutex_` serializes it against `log()` event output

### `main.cpp`

Entry point:
1. Parse CLI arguments (`--file`, `--port`, `--transport`, `--permutation`, `--seed`, `--duration`, `--max-time`, `--gateway`, `--checkpoint-dir`, `--resume`, `--test-type`, `--transfer-siblings`, `--no-status`)
2. Validate config file
3. Check for checkpoint (if `--resume`)
4. Construct and run `Producer`
5. Handle signals via `SignalHandler`
6. Print statistics on exit

`--max-time DUR` is parsed with `parse_duration()` (accepts `s`/`m`/`h` suffixes) and stored in `ProducerConfig::max_time_sec`. The dispatcher loop checks elapsed time against `start_time_` and shuts down when the limit is reached.

## 7. Consumer Architecture

### `work_unit_handler.h` — Handler Interface

```cpp
class IWorkUnitHandler {
public:
    virtual ~IWorkUnitHandler() = default;
    virtual std::string type() const = 0;
    virtual ResultMessage handle(const WorkUnitMessage& work) = 0;
    virtual void configure(const std::string& config_path) {}  // Default no-op
};
```

### `result_sink.h` — Result Sink Interface

```cpp
class IResultSink {
public:
    virtual ~IResultSink() = default;
    virtual std::string type() const = 0;
    virtual void on_result(const ResultMessage& result) = 0;
    virtual bool should_stop() const = 0;
    virtual nlohmann::json summary() const = 0;
};
```

### `file_result_sink.h` / `file_result_sink.cpp`

JSON-lines output with running statistics:

```cpp
class FileResultSink : public IResultSink {
    // Constructor: FileResultSink(file_path, max_failures=0, max_duration_sec=0)
    // Writes each result as a JSON line to file_path_
    // Tracks total_, successes_, failures_ behind a mutex
    // should_stop() returns true when failures_ >= max_failures_ or elapsed >= max_duration_sec_
    // summary() returns { total, successes, failures }
};
```

### Existing Handlers

| Handler | Type() | Purpose |
|---------|--------|---------|
| `PWD_Handler` | `"PWD"` | Brute-force password validation using `ArchiveValidator` |
| `BENCH_Handler` | `"BENCH"` | File chunk processing benchmark |
| `ECHO_Handler` | `"ECHO"` | Payload hash verification with configurable power-law delay |

### `thread_pool.h` / `thread_pool.cpp`

Thread pool with work request coordination:

```cpp
class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    void start();
    void shutdown();
    void submit(WorkUnitMessage work);
    size_t idle_count() const;
    size_t active_count() const;
    bool queue_empty() const;
    size_t total_completed() const;
    size_t total_failed() const;
    void set_handler(std::shared_ptr<IWorkUnitHandler> handler);
    void set_result_callback(ResultCallback cb);
    void set_idle_callback(IdleCallback cb);
    std::vector<WorkUnitMessage> drain_pending();  // Returns queued + active work units
};
```

Worker threads pull from an internal `BoundedQueue<WorkUnitMessage>`, invoke the
handler, and fire the result callback. When a thread goes idle, it fires the idle
callback which triggers work requests to the producer. `idle_count_` is
initialized to `num_threads_` in `start()` (it must never underflow, since it is
a `size_t`).

### `consumer.h` / `consumer.cpp`

Main Consumer engine:

```cpp
class Consumer {
    // Core lifecycle
    void run();
    void shutdown();

    // Network
    void connect_to_producer();
    void receiver_loop();
    void heartbeat_loop();              // Sends heartbeat every 5s
    void download_source_file(const std::string& source_file, const std::string& source_hash);
    void send_work_request(int threads_available);
    void send_result(const ResultMessage& result);

    // Duplicate detection
    bool is_completed(const std::string& work_unit_id);
    void mark_completed(const std::string& work_unit_id);
    // LRU cache: std::list + std::unordered_set, max 3000 entries

    // Members
    std::shared_ptr<IWorkUnitHandler> handler_;
    std::shared_ptr<IResultSink> sink_;
    std::chrono::steady_clock::time_point last_request_time_;  // Throttle: 50ms min interval
    std::chrono::steady_clock::time_point last_comm_time_;     // --timeout idle detection
    std::mutex comm_time_mutex_;
};
```

Key threads:
- **Main thread**: Runs `run()`, manages lifecycle
- **Receiver thread**: Reads frames from producer, dispatches to work queue
- **Heartbeat thread**: Sends `HeartbeatMessage` every 5 seconds
- **Pool threads**: Process work units via handler, report results

### `main.cpp`

Entry point:
1. Parse CLI arguments (`--host`, `--port`, `--transport`, `--threads`, `--file-dir`, `--max-messages`, `--local`, `--gateway`, `--consumer-id`, `--handler`, `--handler-config`, `--result-file`, `--max-failures`, `--max-duration`, `--timeout`)
2. Generate `consumer_id` (if not provided)
3. Construct handler based on `--handler` type
4. Call `handler_->configure(config.handler_config)` if `--handler-config` provided
5. Construct result sink (default: `FileResultSink` with auto-generated path if no `--result-file`)
6. Construct and run `Consumer`
7. Handle signals via `SignalHandler`
8. Print statistics on exit

`--timeout SEC` is stored in `ConsumerConfig::idle_timeout_sec`. The main loop shuts down the consumer when no producer communication (any received frame) has occurred for that many seconds.

## 8. Network Protocol

### TCP Framing (Control Channel — `--port`)

```
+-------------+------------------------+
| 4-byte BE   | N-byte JSON payload    |
| uint32_t    | (UTF-8)                |
| (length)    |                        |
+-------------+------------------------+
```

### File Transfer Protocol (port + 1)

**Request (Consumer -> Producer):**
```
+--------+--------------------+------+
| 0x01   | filename (UTF-8)   | \0   |
| (1B)   |                    |      |
+--------+--------------------+------+
```

**Response (Producer -> Consumer):**
```
+--------------+----------------+
| 4-byte BE    | file contents  |
| uint32_t     | (raw bytes)    |
| (file size)  |                |
+--------------+----------------+
```

Size `0` means file not found.

### Heartbeat Protocol

- Consumer sends `HeartbeatMessage` every **5 seconds**
- Producer tracks `last_activity` per consumer in `ConsumerInfo`
- Monitor thread disconnects consumers idle for **>30 seconds**
- Heartbeats also update `last_activity` alongside work requests and results

### Work Request Throttling

- Consumer enforces a **50ms minimum interval** between work requests
- Tracked via `last_request_time_` (`std::chrono::steady_clock::time_point`)
- **Idle safety net**: the consumer main loop also sends a work request whenever
  `pool_->active_count() == 0 && pool_->queue_empty()`. This guards against the
  throttle dropping the idle-callback request when a burst of work just finished.

### Idle Timeout (`--timeout`)

- `last_comm_time_` is stamped (under `comm_time_mutex_`) every time the
  receiver loop receives a frame from the producer
- The main loop shuts down the consumer when
  `now - last_comm_time_ >= idle_timeout_sec` (if `idle_timeout_sec > 0`)

### Duplicate Detection

- Consumer maintains an LRU cache of completed `work_unit_id`s
- Implementation: `std::list<std::string>` (LRU order) + `std::unordered_set<std::string>` (O(1) lookup)
- Maximum **3000 entries** — oldest evicted on overflow
- Protected by `completed_ids_mutex_`

## 9. Adding a New Test Type

1. Create `XXX_plugin.h/cpp` in `src/producer/` implementing `TestPlugin`
2. Create `XXX_Handler.h/cpp` in `src/consumer/` implementing `IWorkUnitHandler`
3. Register in `producer.cpp::init_plugin()` — add case for `test_type`
4. Register in `consumer.cpp` constructor — add case for `handler_type`
5. Add to `producer_lib` and `consumer_lib` source lists in `CMakeLists.txt`

## 10. Testing Strategy

| Test File                    | Links Against              | What It Tests                          |
|------------------------------|----------------------------|----------------------------------------|
| `test_message.cpp`           | `common`                   | JSON round-trip for all 5 message types (15 tests) |
| `test_queue.cpp`             | `common`                   | BoundedQueue concurrency, shutdown (8 tests) |
| `test_work_tracker.cpp`      | `producer_lib`             | Work unit lifecycle, status transitions (10 tests) |
| `test_checkpoint.cpp`        | `common`                   | Save/load, backup, resume (6 tests) |
| `test_integration.cpp`       | `producer_lib`, `consumer_lib` | Lifecycle tests + real-process end-to-end (6 tests) |
| `test_pwd_next_unit.cpp`     | `producer_lib`             | Permutation engine, char sets, ordering (20 tests) |
| `test_sha256.cpp`            | `common`                   | RFC 6234 vectors, file hashing (8 tests) |
| `test_file_result_sink.cpp`  | `consumer_lib`             | JSON lines output, concurrent writes, stopping criteria (8 tests) |
| `test_util.cpp`              | `common`                   | `parse_duration` suffix handling (5 tests) |
| `test_thread_pool.cpp`       | `consumer_lib`             | Dispatch, failure counting, idle callback, drain, shutdown (7 tests) |
| `test_echo.cpp`              | `producer_lib`, `consumer_lib` | ECHO plugin generation/resume + handler hash verification (10 tests) |
| `test_socket.cpp`            | `common`                   | TCP/UDP frame round-trip, bidirectional, error paths (7 tests) |

**Total: 110 tests.** On Windows, run test executables directly rather than through
`ctest` (GTest discovery is not reliably supported by `gtest_discover_tests` on all
MSVC configurations). The `EndToEnd_*` integration tests spawn the real
`producer.exe`/`consumer.exe`, so build the full project first.

## 11. Known Design Decisions

- **IPv4 only** — no IPv6 support
- **Big-endian** for all network integers (cross-platform consistency)
- **Checkpoint backup** written before primary (atomic-ish safety)
- **Consumer thread pool** defaults to `std::thread::hardware_concurrency()`
- **Work request throttling**: max 1 request per 50ms per consumer connection
- **File transfer** on `port + 1` (simple, no multiplexing needed)
- **SHA-256** for file integrity verification (pure C++ RFC 6234, no external dependency)
- **`nlohmann::json`** for all JSON work (header-only, FetchContent)
- **`BoundedQueue`** is a header-only template — `queue.cpp` is empty
- **`Socket`** uses pimpl to hide platform-specific `SOCKET` vs `int` types
- **`ssize_t`** typedef in `pc` namespace for Windows compatibility
- **libarchive** built static with `LIBARCHIVE_STATIC` defined to avoid `__declspec(dllimport)` mismatch
