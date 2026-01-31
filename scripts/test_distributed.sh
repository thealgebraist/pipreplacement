#!/bin/bash
set -e

# SPIP Distributed Test Suite
# This script validates the Master/Worker orchestration

SPIP="./spip"
PKG="idna" # Small package for fast testing
DB_PATH="$HOME/.spip/queue.db"

echo "🧹 Cleaning up previous state..."
rm -f "$DB_PATH"
rm -rf "$HOME/.spip/telemetry/"
rm -rf "$HOME/.spip/envs/"

echo "🏗️  Building spip..."
ninja spip

echo "👑 Initializing Master for $PKG..."
$SPIP master "$PKG"

# Count pending tasks
PENDING=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM work_queue WHERE status='PENDING';")
echo "📊 Tasks in queue: $PENDING"

if [ "$PENDING" -eq 0 ]; then
    echo "❌ Error: Master failed to populate the queue."
    exit 1
fi

echo "👷 Starting 4 parallel workers..."
for i in {1..4}; do
    $SPIP worker &
    WORKER_PIDS[$i]=$!
done

echo "⏳ Waiting for tasks to be processed (30s timeout)..."
SECONDS=0
while [ $SECONDS -lt 30 ]; do
    COMPLETED=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM work_queue WHERE status='COMPLETED';")
    FAILED=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM work_queue WHERE status='FAILED';")
    TOTAL=$((COMPLETED + FAILED))
    
    echo -ne "\rProgress: $TOTAL/$PENDING (Completed: $COMPLETED, Failed: $FAILED)   "
    
    if [ "$TOTAL" -ge "$PENDING" ]; then
        echo -e "\n✅ All tasks processed!"
        break
    fi
    sleep 2
done

# Kill workers
echo "🛑 Shutting down workers..."
for pid in "${WORKER_PIDS[@]}"; do
    kill -INT "$pid" 2>/dev/null || true
done

# Final Verification
FINAL_PENDING=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM work_queue WHERE status='PENDING';")
if [ "$FINAL_PENDING" -gt 0 ]; then
    echo "❌ Fail: $FINAL_PENDING tasks remained PENDING."
    exit 1
fi

echo "🎉 Distributed Test Suite PASSED!"
