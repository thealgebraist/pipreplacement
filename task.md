# Task: EPYC Class Scaling for SPIP

**Goal**: Achieve 90%+ average CPU utilization on high-density server hardware (e.g., AMD EPYC 9004 series with 128+ cores). 

## Current Status
- Single-instance `spip` is currently limited by:
    - **I/O Latency**: Even with 64 threads, `curl` and `git` operations spend significant time in `iowait`.
    - **Git Index Contention**: While semaphores helped, Git's internal object database- [x] **EPYC Class Scaling (V3)**: 
    - [x] Implemented `master`/`worker` distributed orchestration via SQLite task queue.
    - [x] Integrated `tmpfs` RAM-disk support for `envs/` root to eliminate I/O wait.
    - [x] Split telemetry into isolated per-run databases with 5-second batch transactions.
- [x] Implement an **Aggressive Matrix Sharder** that splits a 1600+ combination test into $N$ workloads across different repo replicas.
- [x] Standardize a shared-nothing architecture to avoid any shared mutexes across the $N$ instances.

### 2. RAM-Backed Test Environments (tmpfs)
- [x] Move `matrix/` worktrees to a `tmpfs` (RAM disk) mount by default on large servers.
- [x] Benchmark the elimination of SSD/NVMe `iowait` during bulk file extractions.
- [x] Implement a "Pre-Heat" logic that copies the base repo to RAM once.

### 3. Distributed Telemetry Aggregation
- [x] Introduce a `test_run_uuid` that spans across multiple processes.
- [x] Update `TelemetryLogger` to support isolated per-run DB files.
- [x] Implement 5s batch transactions to minimize DB lock contention.

### 4. Zero-Cleanup Fast Mode
- [ ] Option to leave worktrees alive for re-use if the Python version hasn't changed.
- [ ] Optimize the "Prune" logic to run as a background low-priority task instead of on the critical path.

## Success Metrics
- [ ] **Utilization**: >90% average CPU usage during the bulk execution phase.
- [ ] **Throughput**: >100 Python compatibility tests processed per second.
- [ ] **Linearity**: Doubling the core count (from 64 to 128) results in <10% overhead increase.

---
*Created by Antigravity - 2026-01-31*
