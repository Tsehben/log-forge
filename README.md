# LogForge

A C++ append-only distributed log storage engine with gRPC API and leader/follower replication.

## Architecture

| Component | Description |
|-----------|-------------|
| **LogStore** | Append-only binary log with FNV-1a checksums and crash recovery |
| **gRPC API** | `AppendLog`, `GetLog`, `SearchByKey`, `SearchByTimestampRange`, `ReplicateLog` |
| **Replication** | Fixed leader/follower — leader persists and fans out each write to two followers |
| **Fault Tolerance** | Writes succeed if leader + ≥1 follower durably persists the entry (2-of-3 durability) |

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

### Automated replication verification

```bash
./run_test.sh
```

Starts a clean 3-node cluster, runs two client rounds (one with both followers up, one with follower1 killed), and prints PASS/FAIL for 2/2 and 1/2 replication acknowledgments.
