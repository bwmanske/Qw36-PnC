# Testing

## Running Tests

`ctest` does NOT discover tests on Windows. Run test executables directly:

```powershell
# After building with -DBUILD_TESTS=ON
build\tests\Release\test_message.exe
build\tests\Release\test_queue.exe
build\tests\Release\test_work_tracker.exe
build\tests\Release\test_checkpoint.exe
build\tests\Release\test_integration.exe
```

## Test Suite Overview

| Suite | Target | Tests | Links Against |
|-------|--------|-------|---------------|
| `test_message` | `test_message.exe` | 6 | `common` |
| `test_queue` | `test_queue.exe` | 8 | `common` |
| `test_work_tracker` | `test_work_tracker.exe` | 10 | `producer_lib` |
| `test_checkpoint` | `test_checkpoint.exe` | 6 | `common` |
| `test_integration` | `test_integration.exe` | 4 | `producer_lib`, `consumer_lib` |
| **Total** | | **34** | |

---

## test_message (6 tests)

### WorkUnitMessage

| Test | What It Verifies |
|------|-----------------|
| `RoundTrip` | All fields (source_file, permutation, permutation_seed, work_unit_id, seq, timestamp, producer_id, job, source_hash) survive `to_string()` → `from_string()` serialization |
| `OptionalFields` | `permutation_seed` and `source_hash` are omitted from JSON when not set; `msg_type` is always `"work_unit"` |

### ResultMessage

| Test | What It Verifies |
|------|-----------------|
| `RoundTrip` | All fields (work_unit_id, seq, consumer_id, status, result, timestamp) survive serialization round-trip |
| `FailureStatus` | `status: "failure"` serializes correctly; `msg_type` is `"result"` |

### WorkRequestMessage

| Test | What It Verifies |
|------|-----------------|
| `RoundTrip` | All fields (consumer_id, threads_available, timestamp) survive serialization round-trip |
| `MsgType` | `msg_type` is `"work_request"` in serialized JSON |

---

## test_queue (8 tests)

### BoundedQueue

| Test | What It Verifies |
|------|-----------------|
| `PushPop` | FIFO ordering: push 1, 2, 3 → pop returns 1, 2, 3 |
| `Size` | `size()` and `empty()` are correct after push/pop operations |
| `BlocksWhenFull` | `push()` blocks when queue is at capacity; unblocks after a `pop()` frees space |
| `TryPushTimeout` | `try_push()` returns `false` when queue is full and timeout expires |
| `TryPopTimeout` | `try_pop()` returns `nullopt` when queue is empty and timeout expires |
| `Shutdown` | Calling `shutdown()` unblocks a thread waiting on `pop()` |
| `ConcurrentProducersConsumers` | 1000 items pushed and popped across two threads with no data loss |
| `DrainAfterShutdown` | After `shutdown()`, remaining items can still be popped (throwing after exhaustion) |

---

## test_work_tracker (10 tests)

### WorkTracker

| Test | What It Verifies |
|------|-----------------|
| `AddAndFind` | `add_pending()` + `find()` returns the entry with correct seq and status |
| `MarkSent` | `mark_sent()` transitions status to `Sent` and records `consumer_id` |
| `MarkCompleted` | `mark_completed()` updates `completed_count` and `last_completed_seq` |
| `MarkFailedReturnsToPending` | `mark_failed()` resets status to `Pending`, clears `consumer_id`, increments `failed_count` |
| `GetPending` | `get_pending(N)` returns exactly N pending entries |
| `GetPendingSkipsSent` | `get_pending()` excludes entries already marked `Sent` |
| `FailedForConsumer` | `get_failed_for_consumer()` returns only entries sent to that consumer; resets them to `Pending`; leaves other consumers' entries untouched |
| `PendingCount` | `pending_count()` correctly excludes completed entries |
| `ToCheckpoint` | `to_checkpoint()` produces a `CheckpointState` with correct total_jobs, last_completed_seq, completed_count, and pending_count |
| `NotFound` | `find()` returns `nullopt` for a non-existent work_unit_id |

---

## test_checkpoint (6 tests)

### CheckpointState

| Test | What It Verifies |
|------|-----------------|
| `RoundTrip` | All fields (producer_id, source_file, permutation, permutation_seed, total_jobs, last_completed_seq, last_completed_work_unit_id, completed_count, pending_count, consumers_connected) survive JSON serialization round-trip |

### CheckpointManager

| Test | What It Verifies |
|------|-----------------|
| `SaveAndLoad` | `save()` writes to disk; `exists()` returns `true`; `load()` returns the saved state with correct fields |
| `BackupCreated` | Second `save()` creates `state.backup.json` containing the previous primary's state; primary contains the new state |
| `CorruptPrimaryUsesBackup` | When `state.json` is corrupted, `load()` falls back to `state.backup.json` and returns the backup's data |
| `NotExists` | `exists()` returns `false` and `load()` returns `nullopt` when no checkpoint files exist |
| `Paths` | `primary_path()` is `dir/state.json`; `backup_path()` is `dir/state.backup.json` |

---

## test_integration (4 tests)

### Integration

| Test | What It Verifies |
|------|-----------------|
| `MessageAndQueue` | `WorkUnitMessage` objects flow through a `BoundedQueue` across two threads with correct ordering and field integrity |
| `WorkTrackerFullCycle` | End-to-end lifecycle: add 5 entries → dispatch 3 to a consumer → complete 2 → simulate consumer disconnect → verify re-dispatch of 1 remaining sent unit → checkpoint reflects 2 completed |
| `CheckpointResume` | Save checkpoint at seq 49 → load it → resume index is 50 (last_completed_seq + 1); permutation_seed is preserved |
| `MessageSerializationAllTypes` | All three message types (`WorkUnitMessage`, `ResultMessage`, `WorkRequestMessage`) serialize and deserialize correctly with realistic payloads |

---

## What Is NOT Covered (Manual / Future)

The following require running the actual executables or network connectivity:

- File transfer protocol over `port + 1`
- SHA-256 hash computation and verification
- TCP/UDP socket framing (`send_frame` / `recv_frame`)
- Signal handling and graceful shutdown
- Producer-consumer end-to-end communication
- Thread pool execution and handler dispatch
- `PWD_NextUnit` permutation logic
- `PWD_Handler` processing
- Multi-consumer connections and re-dispatch
- Work request throttling
- Checkpoint data directory (`get_data_directory()`)
