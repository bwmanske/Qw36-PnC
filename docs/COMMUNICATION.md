# Producer-Consumer Communication Protocol

**Current version: 0.6**

Version is defined in `include/common/version.h` as `PC_VERSION`.

**Rule:** Increment the version by 0.1 whenever the communication protocol between producer and consumer is modified.

---

## Transport

### Control Channel
- **Protocol:** TCP or UDP (`--transport tcp|udp`, default: tcp)
- **Port:** `--port` (default: 9876)
- **Frame format:** Length-prefixed JSON — 4-byte big-endian `uint32_t` length + UTF-8 JSON payload

**TCP mode:** Connection-oriented. Consumer calls `connect()`, producer `accept()`. Each consumer gets a dedicated thread on the producer. Messages sent via `send_frame`/`recv_frame`.

**UDP mode:** Connectionless. Both sides bind a UDP socket. Messages sent via `send_frame_udp`/`recv_frame_udp`. Producer tracks consumers by IP:port. Same JSON message format as TCP. UDP datagrams are limited to ~64KB (frame header + JSON payload).

### File Transfer Channel
- **Protocol:** TCP (always, regardless of control channel transport)
- **Port:** `--port + 1`
- Used for transferring source files and additional sibling files to remote consumers

---

## Message Types

All control channel messages are length-prefixed JSON frames with a `msg_type` field.

### `version` (Handshake)
**Direction:** Consumer → Producer, then Producer → Consumer

Sent as the very first message after TCP connection is established, or as the first UDP datagram.

**Request (Consumer → Producer):**
```json
{
  "msg_type": "version",
  "version": "0.6",
  "consumer_id": "cons-hostname-1234"
}
```

**Response (Producer → Consumer):**
```json
{
  "msg_type": "version",
  "version": "0.6",
  "status": "ok"
}
```

If versions mismatch, producer responds with `"status": "mismatch"`. For TCP, the connection is closed. For UDP, the producer stops responding to that consumer. Consumer exits with code 4.

### `work_request`
**Direction:** Consumer → Producer

Consumer signals available threads to receive work units.

```json
{
  "msg_type": "work_request",
  "consumer_id": "cons-hostname-1234",
  "threads_available": 8,
  "timestamp": "2025-01-15T10:30:00.000Z"
}
```

Producer responds by sending `threads_available` number of `work_unit` messages.

### `work_unit`
**Direction:** Producer → Consumer

A unit of work to be processed.

```json
{
  "msg_type": "work_unit",
  "test_type": "PWD",
  "source_file": "data.bin",
  "permutation": "sequential",
  "work_unit_id": "prod-4321-0",
  "seq": 0,
  "timestamp": "2025-01-15T10:30:00.000Z",
  "producer_id": "prod-4321",
  "job": { ... },
  "source_hash": "abc123..."
}
```

### `result`
**Direction:** Consumer → Producer

Processing result for a work unit.

```json
{
  "msg_type": "result",
  "work_unit_id": "prod-4321-0",
  "seq": 0,
  "consumer_id": "cons-hostname-1234",
  "status": "success",
  "result": { ... },
  "timestamp": "2025-01-15T10:30:05.000Z"
}
```

Status values: `"success"`, `"failure"`. Optional fields: `found_password`, `file_error`.

### `heartbeat`
**Direction:** Consumer → Producer

Sent every 5 seconds to keep connection alive. Producer considers consumer stale after 30s of inactivity.

```json
{
  "msg_type": "heartbeat",
  "consumer_id": "cons-hostname-1234",
  "timestamp": "2025-01-15T10:30:10.000Z"
}
```

---

## File Transfer Protocol (Port + 1)

Binary protocol for transferring files from producer to consumer.

### `0x01` — File Download
**Request:** `0x01` + null-terminated filename (UTF-8) + `\0`

**Response:** 4-byte big-endian `uint32_t` file size + raw file bytes

If file not found, size is `0` and no bytes follow.

### `0x02` — Manifest Request
**Request:** `0x02` (single byte)

**Response:** 4-byte big-endian `uint32_t` JSON length + JSON array

```json
[
  {
    "name": "file.bin",
    "size": 12345,
    "sha256": "abc123..."
  }
]
```

Used by `--transfer-siblings` to discover and download all sibling files in the config directory.

---

## Connection Lifecycle

### TCP
1. Consumer calls `connect()` to producer on control channel (port)
2. **Version handshake** — consumer sends `version` message, producer responds with `ok` or `mismatch`
3. Consumer sends `work_request` with thread count
4. Producer sends `work_unit` messages (one per available thread)
5. On first `work_unit`, consumer downloads source file via file transfer channel (port+1, TCP)
6. If `--transfer-siblings` is enabled, consumer downloads additional files via manifest (`0x02`)
7. Consumer starts thread pool, processes work units, sends `result` messages
8. Consumer sends `heartbeat` every 5 seconds
9. When threads finish work, consumer sends `work_request` for more units
10. On shutdown, consumer returns in-progress units as `"failure"` results

### UDP
1. Consumer binds a local UDP socket (no `connect()` call)
2. **Version handshake** — consumer sends `version` datagram to producer, producer responds to consumer's IP:port
3. Consumer sends `work_request` datagram with thread count
4. Producer sends `work_unit` datagrams back to consumer's IP:port
5. On first `work_unit`, consumer downloads source file via file transfer channel (port+1, TCP — always TCP)
6. Same as TCP for remaining steps
7. No connection to close — consumer just stops sending/receiving datagrams

---

## Version History

### 0.6 (Current)
- UDP transport integration for control channel
- Producer runs UDP receive loop alongside TCP accept loop
- Consumer supports `--transport udp` flag for connectionless mode
- UDP consumers tracked by IP:port instead of socket connection
- File transfer channel remains TCP-only (reliable byte stream required)
- Socket layer now creates OS socket in constructor (SOCK_STREAM/SOCK_DGRAM)

### 0.5
- Establishes version numbering and version handshake protocol
- Consumer and producer exchange `version` messages on connection
- Version mismatch causes immediate disconnection
- Version printed to console on startup for both producer and consumer

### 0.4
- Added `0x02` manifest request to file transfer protocol
- `--transfer-siblings` flag for bulk file transfer
- SHA-256 verification for downloaded files
- Added `permutation` and `permutation_seed` fields to `work_unit`
- Added `source_hash` field to `work_unit` for file verification

### 0.3
- Added `heartbeat` message type for connection liveness
- Consumer stale detection (30s timeout, 5s check interval)
- Work unit reclamation from stale consumers
- Added `ECHO` test type

### 0.2
- Added `result` message with `found_password` and `file_error` optional fields
- Added `BENCH` test type
- File transfer channel (port+1) with `0x01` file download protocol
- Consumer shutdown returns in-progress units as `"failure"`
- Work request throttling (max 1 per 50ms per consumer)
- Duplicate `work_unit_id` tracking in consumer (LRU cache, 3000 entries)

### 0.1
- Initial protocol
- `work_request` / `work_unit` / `result` message types
- Length-prefixed JSON frames over TCP
- `PWD` test type only
