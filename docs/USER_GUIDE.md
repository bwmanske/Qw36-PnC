# User Guide

## 1. Overview

This system consists of two command-line applications:

- **Producer** — Reads a job file, permutes the work items, and dispatches
  work units to one or more Consumers over the network.
- **Consumer** — Connects to a Producer, downloads the source job file if
  needed, processes work units using a thread pool, and returns results.

Both applications are available for Windows and Linux. They communicate over
Ethernet using IPv4, with a choice of TCP or UDP transport.

## 2. Quick Start

### Running on a Single Machine

```bash
# Terminal 1 — Start the Producer
producer --file jobs.json

# Terminal 2 — Start the Consumer
consumer
```

With default settings, the Producer listens on `0.0.0.0:9876` and the Consumer
connects to `127.0.0.1:9876`. The Consumer will process all jobs and report
results back.

### Running Across Machines

```bash
# On the Producer machine
producer --file jobs.json --port 9876

# On the Consumer machine
consumer --host 192.168.1.100 --port 9876
```

Replace `192.168.1.100` with the Producer machine's IP address.

### Running Multiple Consumers

```bash
# Terminal 1 — Producer
producer --file jobs.json

# Terminal 2 — Consumer 1
consumer --consumer-id worker-1

# Terminal 3 — Consumer 2
consumer --consumer-id worker-2

# Terminal 4 — Consumer 3
consumer --consumer-id worker-3
```

The Producer distributes work units across all connected Consumers. If one
Consumer disconnects, its pending work is automatically re-dispatched to the
remaining Consumers.

## 3. Producer

### Purpose

The Producer reads a job file, applies a permutation to determine dispatch
order, and sends work units to Consumers. It tracks every work unit's status
and can resume from a checkpoint if it shuts down before completing all jobs.

### Usage

```
producer --file PATH [OPTIONS]
```

### Required Arguments

| Flag       | Description                              |
|------------|------------------------------------------|
| `--file`   | Path to the job file (JSON array or NDJSON). This flag is mandatory. |

### Optional Arguments

| Flag               | Default        | Description                                      |
|--------------------|----------------|--------------------------------------------------|
| `--port`           | `9876`         | TCP/UDP port to bind on                          |
| `--transport`      | `tcp`          | Transport protocol: `tcp` or `udp`               |
| `--permutation`    | `sequential`   | Job dispatch order (see below)                   |
| `--seed`           | current time   | Random seed for `random` permutation             |
| `--duration`       | `0`            | Stop after N seconds (`0` = run until all jobs done) |
| `--gateway`        | `192.168.1.1`  | Default local gateway IPv4 address               |
| `--checkpoint-dir` | `./`           | Directory for checkpoint state files             |
| `--resume`         | off            | Resume from the last checkpoint if one exists    |

### Permutation Modes

The `--permutation` flag controls the order in which jobs are dispatched:

| Mode           | Description                                      |
|----------------|--------------------------------------------------|
| `sequential`   | Jobs dispatched in the order they appear in the file |
| `random`       | Jobs shuffled using a seeded random number generator |
| `round_robin`  | Jobs distributed round-robin across Consumers    |
| `reverse`      | Jobs dispatched in reverse file order            |

**Example**: Use `--permutation random --seed 42` for a reproducible random order.

### Job File Format

The job file can be either a JSON array or NDJSON (one JSON object per line).

**JSON array format:**
```json
[
  { "job_id": 1, "task": "render", "params": { "width": 1920, "height": 1080 } },
  { "job_id": 2, "task": "encode", "params": { "codec": "h264", "bitrate": 5000 } }
]
```

**NDJSON format:**
```json
{"job_id": 1, "task": "render", "params": { "width": 1920, "height": 1080 }}
{"job_id": 2, "task": "encode", "params": { "codec": "h264", "bitrate": 5000 }}
```

Each job object can contain any fields your workload needs. The Producer passes
them through to the Consumer unchanged.

### Checkpoint and Resume

The Producer saves its progress to a checkpoint file so it can resume after
an interruption.

- **Checkpoint file**: `<checkpoint-dir>/state.json`
- **Backup file**: `<checkpoint-dir>/state.backup.json`
- **Write interval**: Every 60 seconds while running
- **On shutdown**: Written immediately before exit

To resume after a crash or interruption:
```bash
producer --file jobs.json --resume
```

The Producer will re-read the job file, re-apply the same permutation, and
continue from the last successfully completed work unit.

### Exit Codes

| Code | Meaning                              |
|------|--------------------------------------|
| 0    | Normal completion or graceful shutdown |
| 1    | Invalid arguments or bad job file    |
| 2    | Port already in use                  |

### Example Commands

```bash
# Basic usage
producer --file jobs.json

# Random permutation with reproducible seed
producer --file jobs.json --permutation random --seed 42

# Custom port, checkpoint directory, and resume
producer --file jobs.json --port 8080 --checkpoint-dir /var/lib/producer --resume

# UDP transport, 30-minute timeout
producer --file jobs.json --transport udp --duration 1800

# Reverse order, custom gateway
producer --file jobs.json --permutation reverse --gateway 10.0.0.1
```

## 4. Consumer

### Purpose

The Consumer connects to a Producer, requests work units, processes them using
a thread pool, and returns results. It automatically downloads the source job
file from the Producer if it is not already available locally.

### Usage

```
consumer [OPTIONS]
```

### Optional Arguments

| Flag              | Default            | Description                                      |
|-------------------|--------------------|--------------------------------------------------|
| `--host`          | `127.0.0.1`        | Producer's IPv4 address                          |
| `--port`          | `9876`             | Producer's port                                  |
| `--transport`     | `tcp`              | Transport protocol: `tcp` or `udp`               |
| `--threads`       | 1 per core         | Number of processing threads                     |
| `--file-dir`      | `./`               | Local directory for downloaded source files      |
| `--max-messages`  | `0`                | Stop after N completed work units (`0` = no limit) |
| `--local`         | off                | Force connection to `127.0.0.1`                  |
| `--gateway`       | `192.168.1.1`      | Default local gateway IPv4 address               |
| `--consumer-id`   | auto-generated     | Unique identifier for this Consumer              |

### Thread Pool

By default, the Consumer creates one processing thread per logical CPU core.
Use `--threads` to override:

```bash
# Use exactly 4 threads regardless of CPU count
consumer --threads 4

# Use all available cores (default behavior)
consumer
```

### Source File Download

When the Consumer receives its first work unit, it checks whether the source
job file is available locally:

1. If the file exists in `--file-dir` and its SHA-256 hash matches, it is used as-is.
2. If the file is missing or the hash doesn't match, the Consumer downloads it
   from the Producer over a secondary connection (`port + 1`).
3. If the download fails, the Consumer exits with code 3.

### Consumer ID

If you don't specify `--consumer-id`, the Consumer generates one automatically
using the hostname and process ID (e.g., `cons-myhost-12345`). When running
multiple Consumers, giving each a distinct ID makes it easier to track which
Consumer processed which work unit.

```bash
consumer --consumer-id render-node-1
consumer --consumer-id render-node-2
```

### Exit Codes

| Code | Meaning                                    |
|------|--------------------------------------------|
| 0    | Normal completion or graceful shutdown     |
| 2    | Cannot connect to Producer after retries   |
| 3    | Source file download or hash verification failed |

### Example Commands

```bash
# Connect to local Producer (default)
consumer

# Connect to remote Producer
consumer --host 192.168.1.100 --port 9876

# Custom thread count and consumer ID
consumer --host 192.168.1.100 --threads 8 --consumer-id worker-alpha

# UDP transport, stop after 500 work units
consumer --transport udp --max-messages 500

# Force localhost, custom file directory
consumer --local --file-dir /tmp/consumer-files
```

## 5. Network Configuration

### Same Machine

When running Producer and Consumer on the same machine, the Consumer connects
to `127.0.0.1` by default. No special configuration is needed.

### Different Machines

Ensure the following:

1. The Producer's firewall allows inbound connections on the configured port
   (default `9876`) and the file transfer port (`9877`).
2. The Consumer can reach the Producer's IP address over the network.
3. Both machines use the same transport protocol (`tcp` or `udp`).

### UDP Mode

In UDP mode, the Producer sends datagrams to the Consumer's address. UDP is
connectionless — the Producer does not track individual Consumer connections.
Use TCP when you need reliable delivery and work unit tracking.

**Note**: UDP datagrams are limited to the network MTU (typically 1500 bytes).
Work units larger than the MTU are dropped with a warning.

## 6. Monitoring Progress

### Producer Output

While running, the Producer logs:
- Consumer connections and disconnections
- Work unit dispatches
- Received results
- Checkpoint saves

On shutdown, the Producer prints a summary:
```
=== Producer Statistics ===
Total jobs:              1000
Work units dispatched:   1000
Work units completed:    847
Work units failed:       12
Work units pending:      141
Consumers connected:     3
Duration:                125.4s
Throughput:              6.75 completed/s
Checkpoint:              ./state.json (written)
```

### Consumer Output

While running, the Consumer logs:
- Connection status
- Source file download progress
- Work unit processing (work_unit_id, job_id, task)
- Results sent

On shutdown, the Consumer prints a summary:
```
=== Consumer Statistics ===
Work units received:     500
Work units completed:    498
Work units failed:       2
Work units unprocessed:  0
Source file:             jobs.json (downloaded)
Thread pool size:        8
Duration:                62.1s
Throughput:              8.02 completed/s
Sequence range:          1 - 500
```

## 7. Graceful Shutdown

Press `Ctrl+C` in the terminal to shut down either application gracefully.

**Producer shutdown behavior:**
- Stops dispatching new work units
- Saves checkpoint immediately
- Waits for in-flight messages to complete
- Prints statistics and exits

**Consumer shutdown behavior:**
- Stops receiving new work units
- Finishes processing current work units
- Sends results for completed units
- Returns uncompleted units as failures (so the Producer can re-dispatch)
- Prints statistics and exits

Both applications should shut down within 5 seconds of receiving `Ctrl+C`.

## 8. Troubleshooting

### Consumer Cannot Connect to Producer

- Verify the Producer is running and listening on the expected port.
- Check that `--host` matches the Producer's IP address.
- Ensure the firewall allows inbound connections on the Producer's port.
- Try `--local` if running on the same machine.

### Source File Download Fails

- Verify the Producer has `--file` pointing to a valid, readable file.
- Check that the file transfer port (`--port + 1`) is not blocked by a firewall.
- Check the Consumer's `--file-dir` is writable.

### Work Units Not Being Processed

- Verify the Consumer's `--transport` matches the Producer's `--transport`.
- Check that the Consumer's thread pool is large enough (`--threads`).
- Look for validation errors in the Consumer's stderr output.

### Checkpoint Resume Fails

- Ensure `--file` points to the same job file used in the previous session.
- The checkpoint stores the permutation mode and seed; these must match.
- If `state.json` is corrupt, the Producer will fall back to `state.backup.json`.
- If both are corrupt, the Producer starts fresh from the beginning.

### High Rate of Failed Work Units

- Check network stability between Producer and Consumer.
- Increase the Consumer's thread pool size if work units are timing out.
- Check the Consumer's logs for processing errors.

## 9. File Layout on Disk

After running, you will find:

```
<working-directory>/
├── state.json              # Producer checkpoint (primary)
├── state.backup.json       # Producer checkpoint (backup)
├── <job-filename>          # Consumer's local copy of the source file
└── ...
```

Use `--checkpoint-dir` and `--file-dir` to control where these files are stored.
