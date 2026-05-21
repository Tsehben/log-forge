# LogForge

A C++ distributed append-only log storage engine with a gRPC API, leader/follower replication, crash recovery, key compaction, and optional compression. Built from scratch as a systems programming project.

> **145,392 writes/sec · 6.77 µs avg append latency · 1.60 µs avg read latency · 82.8% disk savings with compression** *(Apple M2, 100k entries, 256B values)*

---

## Architecture

<img width="1536" height="1024" alt="design_image" src="https://github.com/user-attachments/assets/9d76014f-2d3c-4637-a275-17d0dc59d4f9" />


---

## Features

- [x] Append-only binary log with monotonically increasing offsets
- [x] Three in-memory indexes: offset → file position, key → offsets, timestamp → offset
- [x] FNV-1a checksum per entry and corrupted trailing byte truncation on recovery
- [x] gRPC API with Protocol Buffers
- [x] Leader/follower replication — every write fans out to two followers synchronously
- [x] Fault-tolerant writes — cluster accepts writes with only 1 of 2 followers alive
- [x] Key-based log compaction — keeps the latest value per key, rewrites with new offsets, propagates to followers
- [x] Optional zlib compression per entry — 82.8% disk savings on compressible payloads
- [x] 3-node Docker Compose cluster
- [x] 25 passing GoogleTest unit tests · custom benchmark tool

---

## Design

### Storage Engine

`LogStore` is an append-only binary file. Every `append()` call writes a new record at the end of the file and updates three in-memory indexes. No record is ever modified in place. Reads use the offset index to seek directly to the record's file position.

### On-Disk Format

Each log entry is a self-contained binary record:

```
[offset: 8B][timestamp: 8B][flags: 1B][key_size: 4B][key][value_size: 4B][value][checksum: 4B]
```

- **offset** — monotonically increasing, assigned by the leader
- **timestamp** — milliseconds since epoch at write time
- **flags** — bit 0 set when the value is zlib-compressed
- **value_size** — byte count of the stored value (compressed size when flag is set)
- **checksum** — FNV-1a over offset, timestamp, key, and the *uncompressed* value

When compression is enabled the stored value is: `[uncompressed_size: 4B][zlib deflate stream]`. The checksum always covers the original plaintext value so it can be verified during recovery without a separate pass.

### Indexes

All three indexes are rebuilt from the log file at startup (`recover()`):

| Index | Type | Purpose |
|---|---|---|
| `offset_index_` | `unordered_map<uint64_t, streampos>` | O(1) random read by offset |
| `key_index_` | `unordered_map<string, vector<uint64_t>>` | All offsets for a given key |
| `timestamp_index_` | `multimap<int64_t, uint64_t>` | Range scan by timestamp |

### Crash Recovery

On open, `recover()` scans the file from byte 0 forward. For each record it re-derives the FNV-1a checksum and compares it against the stored value. The first record that fails the check (or where the file ends mid-record) marks the truncation point — everything from that byte onward is removed with `ftruncate`. All prior valid records are indexed normally. This handles clean shutdowns, power loss mid-write, and partial writes.

### Replication

The leader serialises every `append()` to its own `LogStore`, then issues a `ReplicateLog` RPC to each follower. A follower calls `replicate()`, which writes the entry with the leader-assigned offset and timestamp. The leader counts acknowledgments: if at least one follower confirms, the write is considered durable (2-of-3). If a follower is unreachable the leader logs a warning and continues — writes never block on a dead follower.

### Compaction

`compact()` collects the latest entry per key by scanning `key_index_` (each key keeps a sorted list of offsets; the last is the most recent), writes those entries to a `.tmp` file with new sequential offsets, then atomically replaces the live log with `std::filesystem::rename`. Indexes are rebuilt from the new file. The leader then fans `CompactReplica` to all followers so every node converges to the same compacted state.

### Compression

Controlled by `--compression=true` (or `LOGFORGE_COMPRESSION=true`). When enabled, `append()` passes the value through zlib `compress2()` before writing and sets `flags = 0x01`. `read()` checks the flag and decompresses transparently before returning the `LogEntry` — callers always see the original value. `recover()` and `compact()` do the same.

---

## Quick Start (Docker Compose)

**Prerequisites:** [Docker Desktop](https://www.docker.com/products/docker-desktop/) with Compose v2+

```bash
# Build image
docker compose build

# Start 3-node cluster
docker compose up -d follower1 follower2 leader

# Run client demo
docker compose run --rm client

# View node logs
docker compose logs leader
docker compose logs follower1
docker compose logs follower2

# Stop (keep data volumes)
docker compose down

# Stop and delete all data
docker compose down -v
```

Automated build → start → demo → print logs → teardown:

```bash
./docker_test.sh
```

---

## Local Development

**Prerequisites (macOS):**

```bash
brew install cmake grpc protobuf
# zlib is included with Xcode Command Line Tools
```

**Build all targets:**

```bash
cmake -B build
cmake --build build
```

**Start the 3-node cluster manually:**

```bash
./build/logforge_server --role=follower --port=5002 --data=./data/follower1 &
./build/logforge_server --role=follower --port=5003 --data=./data/follower2 &
./build/logforge_server --role=leader  --port=5001 --data=./data/leader \
    --peers=localhost:5002,localhost:5003 &

./build/logforge_client --target=localhost:5001
```

**With compression enabled:**

```bash
./build/logforge_server --role=follower --port=5002 --data=./data/follower1 --compression=true &
./build/logforge_server --role=follower --port=5003 --data=./data/follower2 --compression=true &
./build/logforge_server --role=leader  --port=5001 --data=./data/leader    --compression=true \
    --peers=localhost:5002,localhost:5003 &
```

**Automated replication + compression verification:**

```bash
./run_test.sh
```

Runs two full client rounds (both followers up; one follower down), verifies 2/2 and 1/2 replication acknowledgments, then reports compression disk savings. All checks must print `PASS`.

---

## Tests

GoogleTest is downloaded automatically by CMake on first configure — no manual install required.

```bash
cmake --build build --target logstore_tests
ctest --test-dir build --output-on-failure
```

**25 tests, 0 failures.** Coverage:

| Category | Tests |
|---|---|
| Append + read | single entry, multi-entry, monotonic offsets |
| Missing offset | empty store, out-of-range offset |
| Key index | all entries for key, missing key |
| Timestamp range | mid-range, inclusive bounds, no match |
| Recovery after reopen | data survives, key index rebuilds, next offset resumes |
| Corrupted trailing bytes | garbage truncated, partial header truncated |
| Compaction | latest-per-key, offset reassignment, survives reopen, single key |
| Compression | round-trip on/off, smaller on disk, recovery, compaction with compression |

---

## Benchmarks

```bash
cmake --build build --target logforge_benchmark

./build/logforge_benchmark                              # 100k entries, 256B, compression off
./build/logforge_benchmark --compression=true           # compression on
./build/logforge_benchmark --entries=500000 --value-size=512
```

**Results — Apple M2, 100k entries, 256B values:**

| Metric | Compression OFF | Compression ON |
|---|---|---|
| Write throughput | 259,862 entries/sec | 145,392 entries/sec |
| Avg append latency | 3.73 µs | 6.77 µs |
| p95 append latency | 5.54 µs | 7.92 µs |
| Avg read latency | 1.18 µs | 1.60 µs |
| p95 read latency | 1.46 µs | 1.79 µs |
| Key search latency (avg) | 15,226 µs | 17,972 µs |
| Timestamp range (avg) | 66,870 µs | 89,097 µs |
| Compaction (100k → 10) | 7.06 ms | 4.01 ms |
| File size on disk | 28,320 KB | 4,883 KB |
| **Space saved** | — | **82.8%** |

> Key search and timestamp range latencies are proportional to result set size (~10k and ~50k entries respectively). Compaction is faster with compression on because the rewritten file is ~6× smaller.

---

## Repository Structure

```
LogForge/
├── proto/
│   └── logforge.proto          # gRPC service + message definitions
├── benchmarks/
│   └── logforge_benchmark.cpp  # standalone benchmark tool
├── tests/
│   └── logstore_test.cpp       # 25 GoogleTest unit tests
├── LogEntry.h                  # LogEntry struct
├── LogStore.h / LogStore.cpp   # core storage engine
├── server.cpp                  # gRPC server (leader + follower modes)
├── client.cpp                  # demo client
├── main.cpp                    # local interactive tool
├── CMakeLists.txt
├── Dockerfile
├── docker-compose.yml
├── run_test.sh                 # replication + compression integration test
└── docker_test.sh              # Docker end-to-end test
```

---

## Limitations

- **No Raft / consensus** — the leader is fixed; there is no automatic leader election or failover.
- **In-memory indexes only** — indexes are rebuilt from the full log on every startup; startup time grows linearly with log size.
- **Single log segment** — no segment rolling; one file grows unboundedly until compaction.
- **Synchronous replication** — the leader waits for all reachable followers before returning; no async pipeline.
- **No TLS or authentication** — all gRPC channels are plaintext.

---

## Future Improvements

- **Raft consensus** — replace the fixed leader with Raft for automatic leader election and split-brain safety
- **Leader election** — detect leader failure and promote a follower without manual intervention
- **Persistent index files** — write indexes to disk on shutdown so recovery is O(1) rather than O(n)
- **Segment rolling** — cap segment size and age-out old segments independently of compaction
- **Streaming search RPCs** — return search results as a server-side stream instead of a single response
- **Pagination** — add cursor-based pagination to key and timestamp search responses
- **TLS + authentication** — add mTLS and token-based auth to the gRPC layer

---
