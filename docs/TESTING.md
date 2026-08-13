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
build\tests\Release\test_pwd_next_unit.exe
build\tests\Release\test_sha256.exe
build\tests\Release\test_file_result_sink.exe
```

## Test Suite Overview

| Suite | Target | Tests | Links Against |
|-------|--------|-------|---------------|
| `test_message` | `test_message.exe` | 6 | `common` |
| `test_queue` | `test_queue.exe` | 8 | `common` |
| `test_work_tracker` | `test_work_tracker.exe` | 10 | `producer_lib` |
| `test_checkpoint` | `test_checkpoint.exe` | 6 | `common` |
| `test_integration` | `test_integration.exe` | 4 | `producer_lib`, `consumer_lib` |
| `test_pwd_next_unit` | `test_pwd_next_unit.exe` | 20 | `producer_lib` |
| `test_sha256` | `test_sha256.exe` | 8 | `common` |
| `test_file_result_sink` | `test_file_result_sink.exe` | 5 | `consumer_lib` |
| **Total** | | **67** | |

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

## test_pwd_next_unit (20 tests)

### PWD_NextUnit — Lowercase-only

| Test | What It Verifies |
|------|-----------------|
| `LowerAlpha_FirstChar` | First `setNext()` returns `"a"` |
| `LowerAlpha_Sequence` | First five passwords are `"a"`, `"b"`, `"c"`, `"d"`, `"e"` in order |
| `LowerAlpha_WrapsToTwoChars` | After exhausting `"a"`–`"z"` (26 calls), next password is `"aa"` |
| `LowerAlpha_TwoCharProgression` | Two-char sequence starts `"aa"`, `"ab"` (rightmost char changes fastest) |
| `LowerAlpha_Done` | 1000 consecutive calls all return `PERMUTE_SUCCESS` with valid lowercase-only passwords |

### PWD_NextUnit — Multi-character-set

| Test | What It Verifies |
|------|-----------------|
| `AllSets_FirstChar` | With all 4 sets enabled, first password is still `"a"` |
| `NumericOnly` | Numeric-only mode produces `"0"`, `"1"`, ... |
| `NonAlphaOnly` | Non-alpha-only mode starts with `"~"` (first char in `nonAlpha[]`) |
| `NoOptions_ReturnsError` | With no character sets enabled, `setNext()` returns `PERMUTE_NO_OPTION` |
| `UpperAndLower_Transition` | After exhausting lowercase `"a"`–`"z"`, next password is `"A"` (transitions to uppercase) |

### PWD_NextUnit — Output formatting

| Test | What It Verifies |
|------|-----------------|
| `pwdAsIndicies_Format` | `get_pwdAsIndicies()` returns `"len,idx[N-1],...,idx[0]"` format (high index first) |
| `pwdAsText_ContainsChar` | `get_pwdAsText()` output contains the expected character |
| `PlainPassword_NotNull` | `get_plainPassword()` returns non-null after successful `setNext()` |
| `PasswordLength_Increases` | `testPwdLen` increases from 1 to 2 after exhausting single-char permutations |

### PWD_NextUnit — Character set completeness

| Test | What It Verifies |
|------|-----------------|
| `NonAlpha_ContainsCaret` | The `^` (caret) character is present in the `nonAlpha[]` array (NA_COUNT = 24) |

### PWD_NextUnit — Checkpoint resume

| Test | What It Verifies |
|------|-----------------|
| `ResumeViaIndicies` | Saving `charIndicies` and `testPwdLen`, restoring in a new generator, and calling `setNext()` produces the next password in sequence |

### PWD_NextUnit — Reverse ordering (index 0 = rightmost)

| Test | What It Verifies |
|------|-----------------|
| `TwoChar_RightmostChangesFastest` | Two-char sequence is `"aa"`, `"ab"`, `"ac"`, `"ad"` — rightmost character increments fastest (standard odometer behavior) |
| `TwoChar_az_to_ba` | After `"az"`, rightmost wraps to `'a'` and leftmost increments → `"ba"` |
| `TwoChar_FullCycle` | All 676 two-char permutations (`"aa"` through `"zz"`) are generated; next is three-char `"aaa"` |
| `Indicies_Match_Reversed_String` | `get_pwdAsIndicies()` output `"2,0,3"` matches `charIndicies[1]=0` (left='a'), `charIndicies[0]=3` (right='d') → password `"ad"` |

---

## test_sha256 (8 tests)

### SHA-256 — RFC 6234 vectors

| Test | What It Verifies |
|------|-----------------|
| `EmptyString` | `sha256_bytes(nullptr, 0)` returns the known hash for empty input: `e3b0c442...` |
| `RFC6234_abc` | `sha256_bytes("abc", 3)` matches RFC 6234 Appendix A: `ba7816bf...` |
| `RFC6234_abcabc` | 64-byte message `"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"` matches RFC 6234: `248d6a61...` |
| `RFC6234_a_repeated_1million` | 1,000,000 `'a'` bytes match RFC 6234: `cdc76f52...` (**PENDING** — implementation produces `cdc76e5c...`, bug under investigation) |

### SHA-256 — File hashing

| Test | What It Verifies |
|------|-----------------|
| `FileHash_MatchesBytes` | `sha256_file(path)` produces the same hash as `sha256_bytes()` for the same content |
| `FileHash_MissingFile` | `sha256_file()` returns empty string for a non-existent file |
| `FileHash_EmptyFile` | `sha256_file()` on an empty file returns the SHA-256 of empty input |

### Platform utilities

| Test | What It Verifies |
|------|-----------------|
| `GetDataDirectory` | `get_data_directory()` returns a non-empty path that exists and is a directory |

---

## test_file_result_sink (5 tests)

### FileResultSink

| Test | What It Verifies |
|------|-----------------|
| `WritesJsonLines` | `on_result()` writes a single JSON line to disk with `msg_type`, `status`, and `sink_stats` fields |
| `CountsSuccessesAndFailures` | After 3 results (2 success, 1 failure), `summary()` returns `total=3`, `successes=2`, `failures=1` |
| `ConcurrentWrites` | 10 threads each writing 1 result produces exactly 10 lines in the output file (thread-safe) |
| `ShouldStop` | `should_stop()` always returns `false` |
| `Summary_FilePath` | `summary()["file"]` matches the constructor's `file_path` argument |

---

## What Is NOT Covered (Manual / Future)

The following require running the actual executables or network connectivity:

- File transfer protocol over `port + 1`
- TCP/UDP socket framing (`send_frame` / `recv_frame`)
- Signal handling and graceful shutdown
- Producer-consumer end-to-end communication
- Thread pool execution and handler dispatch
- `PWD_Handler` processing
- `ArchiveValidator` with real archive files
- Multi-consumer connections and re-dispatch
- Work request throttling
- Heartbeat protocol and consumer registration
