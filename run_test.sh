#!/usr/bin/env bash
# run_test.sh — LogForge Leader/Follower Replication Verification
# Usage: Run from the project root. Binaries must already be built in ./build/.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

LEADER_PID=""
F1_PID=""
F2_PID=""

# --------------------------------------------------------------------------- #
# Cleanup: kill all server processes on exit (including on Ctrl+C)
# --------------------------------------------------------------------------- #
cleanup() {
    echo ""
    echo "==> Cleaning up server processes..."
    [ -n "$LEADER_PID" ] && kill "$LEADER_PID" 2>/dev/null; LEADER_PID=""
    [ -n "$F1_PID" ]     && kill "$F1_PID"     2>/dev/null; F1_PID=""
    [ -n "$F2_PID" ]     && kill "$F2_PID"     2>/dev/null; F2_PID=""
    wait 2>/dev/null
    echo "    Done."
}
trap cleanup EXIT INT TERM

# Validate binaries exist
if [ ! -x "$BUILD_DIR/logforge_server" ] || [ ! -x "$BUILD_DIR/logforge_client" ]; then
    echo "[ERROR] Binaries not found in $BUILD_DIR. Run cmake --build ./build first."
    exit 1
fi

cd "$BUILD_DIR"

# =========================================================================== #
# 0. Evict any stale server processes already holding our ports
# =========================================================================== #
echo "==> [0] Evicting stale processes on ports 5001, 5002, 5003..."
for port in 5001 5002 5003; do
    pid=$(lsof -ti tcp:$port 2>/dev/null)
    if [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null || true
        echo "    Killed stale PID $pid on port $port"
    fi
done
sleep 0.5

# =========================================================================== #
# 1. Clean old data and logs
# =========================================================================== #
echo "==> [1] Deleting old data directories and logs..."
rm -rf data/leader data/follower1 data/follower2
rm -f  leader.log f1.log f2.log client_first.log client_after_failure.log
echo "    Clean."

# =========================================================================== #
# 2. Start follower1 (port 5002)
# =========================================================================== #
echo ""
echo "==> [2] Starting follower1 on port 5002..."
./logforge_server --role=follower --port=5002 --data=./data/follower1 > f1.log 2>&1 &
F1_PID=$!
echo "    PID: $F1_PID"

# =========================================================================== #
# 3. Start follower2 (port 5003)
# =========================================================================== #
echo ""
echo "==> [3] Starting follower2 on port 5003..."
./logforge_server --role=follower --port=5003 --data=./data/follower2 > f2.log 2>&1 &
F2_PID=$!
echo "    PID: $F2_PID"

# =========================================================================== #
# 4. Start leader (port 5001, peers: both followers)
# =========================================================================== #
echo ""
echo "==> [4] Starting leader on port 5001 (peers: localhost:5002,localhost:5003)..."
./logforge_server --role=leader --port=5001 --data=./data/leader \
    --peers=localhost:5002,localhost:5003 > leader.log 2>&1 &
LEADER_PID=$!
echo "    PID: $LEADER_PID"

echo ""
echo "    Waiting 1s for all servers to initialize..."
sleep 1

# =========================================================================== #
# 5. First client run — both followers are up
# =========================================================================== #
echo ""
echo "==> [5] Running client (both followers up) -> client_first.log"
./logforge_client --target=localhost:5001 > client_first.log 2>&1
echo "    Done."

# =========================================================================== #
# 6. Kill follower1
# =========================================================================== #
echo ""
echo "==> [6] Killing follower1 (PID $F1_PID)..."
kill "$F1_PID" 2>/dev/null || true
F1_PID=""
echo "    follower1 terminated. Waiting 0.5s for leader to detect failure..."
sleep 0.5

# =========================================================================== #
# 7. Second client run — follower1 is down
# =========================================================================== #
echo ""
echo "==> [7] Running client (follower1 DOWN) -> client_after_failure.log"
./logforge_client --target=localhost:5001 > client_after_failure.log 2>&1
echo "    Done."

# =========================================================================== #
# 8. Print log file contents
# =========================================================================== #
for logfile in leader.log f1.log f2.log client_first.log client_after_failure.log; do
    echo ""
    echo "################################################################"
    echo "#  $logfile"
    echo "################################################################"
    cat "$logfile"
done

# =========================================================================== #
# 9. Verification — check acknowledgment counts
# =========================================================================== #
echo ""
echo "################################################################"
echo "#  VERIFICATION RESULTS"
echo "################################################################"
echo ""

# Count via wc -l to avoid non-zero grep exit codes halting the script
TWO_OF_TWO=$(grep "Replication acknowledgments: 2/2" leader.log 2>/dev/null | wc -l | tr -d ' ')
ONE_OF_TWO=$(grep "Replication acknowledgments: 1/2" leader.log 2>/dev/null | wc -l | tr -d ' ')

FIRST_APPENDS=$(grep "Successfully Appended" client_first.log 2>/dev/null | wc -l | tr -d ' ')
SECOND_APPENDS=$(grep "Successfully Appended" client_after_failure.log 2>/dev/null | wc -l | tr -d ' ')

# Run 1 checks
echo "--- Run 1: Both followers up ---"
printf "  Successful client appends : %s (>= 4)  " "$FIRST_APPENDS"
[ "$FIRST_APPENDS" -ge 4 ] && echo "[PASS]" || echo "[FAIL]"

printf "  Leader 2/2 acks in log    : %s      " "$TWO_OF_TWO"
[ "$TWO_OF_TWO" -ge 4 ] && echo "[PASS]  (all writes confirmed by both followers)" \
                        || echo "[FAIL]  (expected >= 4, got $TWO_OF_TWO)"

# Run 2 checks
echo ""
echo "--- Run 2: follower1 is down ---"
printf "  Successful client appends : %s (>= 4)  " "$SECOND_APPENDS"
[ "$SECOND_APPENDS" -ge 4 ] && echo "[PASS]" || echo "[FAIL]"

printf "  Leader 1/2 acks in log    : %s      " "$ONE_OF_TWO"
[ "$ONE_OF_TWO" -ge 4 ] && echo "[PASS]  (writes succeeded despite one follower being down)" \
                        || echo "[FAIL]  (expected >= 4, got $ONE_OF_TWO)"

# Summary
echo ""
echo "--- Summary ---"
R1_WRITE=$(  [ "$FIRST_APPENDS"  -ge 4 ] && echo PASS || echo FAIL)
R1_REPL=$(   [ "$TWO_OF_TWO"     -ge 4 ] && echo PASS || echo FAIL)
R2_WRITE=$(  [ "$SECOND_APPENDS" -ge 4 ] && echo PASS || echo FAIL)
R2_FAULT=$(  [ "$ONE_OF_TWO"     -ge 4 ] && echo PASS || echo FAIL)

echo "  Replication (2/2, run 1)          : $R1_REPL"
echo "  All writes success (run 1)         : $R1_WRITE"
echo "  Fault-tolerant writes (1/2, run 2) : $R2_FAULT"
echo "  All writes success (run 2)         : $R2_WRITE"
echo ""

# Overall result
if [ "$R1_REPL" = "PASS" ] && [ "$R1_WRITE" = "PASS" ] && \
   [ "$R2_FAULT" = "PASS" ] && [ "$R2_WRITE" = "PASS" ]; then
    echo "  OVERALL: ALL CHECKS PASSED"
else
    echo "  OVERALL: ONE OR MORE CHECKS FAILED"
fi
echo ""

# =========================================================================== #
# 10. Compression comparison
# =========================================================================== #

# Stop remaining servers from the main test
[ -n "$LEADER_PID" ] && kill "$LEADER_PID" 2>/dev/null; LEADER_PID=""
[ -n "$F2_PID" ]     && kill "$F2_PID"     2>/dev/null; F2_PID=""
wait 2>/dev/null; sleep 0.3

echo "################################################################"
echo "#  COMPRESSION COMPARISON"
echo "################################################################"
echo ""

# --- 10a: Without compression ---
echo "==> [10a] Without compression..."
rm -rf data/leader data/follower1 data/follower2
./logforge_server --role=follower --port=5002 --data=./data/follower1 > /dev/null 2>&1 &
CMP_F1=$!
./logforge_server --role=follower --port=5003 --data=./data/follower2 > /dev/null 2>&1 &
CMP_F2=$!
./logforge_server --role=leader --port=5001 --data=./data/leader \
    --peers=localhost:5002,localhost:5003 > /dev/null 2>&1 &
CMP_L=$!
sleep 1
./logforge_client --target=localhost:5001 > /dev/null 2>&1
NO_COMPRESS_SIZE=$(wc -c < data/leader/server_log.bin 2>/dev/null | tr -d ' ')
kill $CMP_F1 $CMP_F2 $CMP_L 2>/dev/null
wait $CMP_F1 $CMP_F2 $CMP_L 2>/dev/null
echo "    Done. Leader log: ${NO_COMPRESS_SIZE} bytes"
sleep 0.3

# --- 10b: With compression ---
echo "==> [10b] With compression (--compression=true)..."
rm -rf data/leader data/follower1 data/follower2
./logforge_server --role=follower --port=5002 --data=./data/follower1 --compression=true > /dev/null 2>&1 &
CMP_F1=$!
./logforge_server --role=follower --port=5003 --data=./data/follower2 --compression=true > /dev/null 2>&1 &
CMP_F2=$!
./logforge_server --role=leader --port=5001 --data=./data/leader \
    --peers=localhost:5002,localhost:5003 --compression=true > /dev/null 2>&1 &
CMP_L=$!
sleep 1
./logforge_client --target=localhost:5001 > /dev/null 2>&1
COMPRESS_SIZE=$(wc -c < data/leader/server_log.bin 2>/dev/null | tr -d ' ')
kill $CMP_F1 $CMP_F2 $CMP_L 2>/dev/null
wait $CMP_F1 $CMP_F2 $CMP_L 2>/dev/null
echo "    Done. Leader log: ${COMPRESS_SIZE} bytes"
echo ""

echo "--- Results ---"
echo "  Uncompressed log : ${NO_COMPRESS_SIZE} bytes"
echo "  Compressed log   : ${COMPRESS_SIZE} bytes"
if [ "${NO_COMPRESS_SIZE:-0}" -gt 0 ] && [ "${COMPRESS_SIZE:-0}" -gt 0 ]; then
    SAVINGS=$(( (NO_COMPRESS_SIZE - COMPRESS_SIZE) * 100 / NO_COMPRESS_SIZE ))
    echo "  Space saved      : ${SAVINGS}%"
fi
echo ""
