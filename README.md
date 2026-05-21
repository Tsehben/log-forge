# LogForge

A C++ append-only distributed log storage engine with gRPC API and leader/follower replication.

## Architecture

| Component | Description |
|-----------|-------------|
| **LogStore** | Append-only binary log with FNV-1a checksums and crash recovery |
| **gRPC API** | `AppendLog`, `GetLog`, `SearchByKey`, `SearchByTimestampRange`, `ReplicateLog`, `CompactLog`, `CompactReplica` |
| **Replication** | Fixed leader/follower — leader persists and fans out each write to two followers |
| **Fault Tolerance** | Writes succeed if leader + ≥1 follower durably persists the entry (2-of-3 durability) |
| **Compaction** | `CompactLog` keeps only the latest entry per key, rewrites with new sequential offsets, atomically replaces the log file, and fans the operation out to all followers via `CompactReplica` |

---

## Docker Compose (Recommended)

### Prerequisites
- [Docker Desktop](https://www.docker.com/products/docker-desktop/) with Compose v2+

### Build the image

```bash
docker compose build
```

### Start the 3-node cluster

```bash
docker compose up -d follower1 follower2 leader
```

### Run the client demo

```bash
docker compose run --rm client
```

### View logs

```bash
# All nodes
docker compose logs

# Individual node
docker compose logs leader
docker compose logs follower1
docker compose logs follower2
```

### Shut down

```bash
# Stop containers, keep volumes
docker compose down

# Stop containers and delete all data volumes
docker compose down -v
```

### Automated build + run + teardown

```bash
./docker_test.sh
```

Builds the image, starts the cluster, runs the client demo, prints all node logs, and shuts down cleanly.

---

## Local Development (macOS)

### Prerequisites

```bash
brew install cmake grpc protobuf
```

### Build

```bash
cmake -B build
cmake --build build
```

### Start the cluster manually

```bash
# Followers first
./build/logforge_server --role=follower --port=5002 --data=./data/follower1 &
./build/logforge_server --role=follower --port=5003 --data=./data/follower2 &

# Then the leader
./build/logforge_server --role=leader --port=5001 --data=./data/leader \
    --peers=localhost:5002,localhost:5003 &

# Run the client
./build/logforge_client --target=localhost:5001
```

### Unit Tests (GoogleTest)

GoogleTest is fetched automatically by CMake the first time you configure — no manual install needed.

```bash
cmake -B build
cmake --build build --target logstore_tests
ctest --test-dir build --output-on-failure
```

The test suite covers:
- Append + read (single and multi-entry)
- Missing offset returns `nullopt`
- Key index (present and absent keys)
- Timestamp range search with inclusive bounds
- Recovery after reopen (data and indexes rebuild, next offset resumes)
- Corrupted trailing bytes are truncated; prior valid entries survive
- Compaction keeps only the latest value per key, reassigns offsets sequentially, survives reopen
- Compression round-trip (enabled and disabled)
- Compressed files are smaller on disk than uncompressed
- Compression survives recovery after reopen
- Compaction preserves correct values when compression is enabled

### Automated replication verification

```bash
./run_test.sh
```

Starts a clean 3-node cluster, runs two client rounds (one with both followers up, one with follower1 killed), prints PASS/FAIL for replication acknowledgments, and finishes with a compression comparison showing byte savings.
