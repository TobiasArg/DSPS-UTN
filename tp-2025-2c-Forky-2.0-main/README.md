# System Technical-Product Documentation
## Information Systems Engineering Project (UTN)


**Project type:** Distributed Systems Platform  
**Focus module:** `MASTER`  
**Document profile:** Academic Engineering
**Date:** 2025   

---

## 1. Technical-product system synthesis (global view)

This project is, first and foremost, a **distributed systems platform** for query execution, oriented to advanced Operating Systems practices and built around explicit process-level role separation:

- **Query Control**: client interface that sends a query (path + priority), receives confirmation, intermediate read messages, and final result.
- **Master**: distributed control-plane coordinator and **query orchestrator**; manages query sessions, worker registration, scheduling-based assignment, and result consolidation.
- **Worker**: instruction executor; interprets queries, interacts with Storage, and reports progress/result/error to Master.
- **Storage**: persistence backend over blocks/metadata for file and tag operations.
- **Utils**: shared network/protocol/log/config library for cross-module consistency.

From a product and architecture perspective, the system solves:

1. **Load admission** (submission of concurrent queries).
2. **Multiprocess scheduling** (FIFO or priority with aging).
3. **Distributed execution** (workers decoupled from the submitting client).
4. **Operational observability** (connection, assignment, eviction, completion, and error logging).
5. **Partial fault tolerance** (disconnections and state-consistency watchdog).

As an undergraduate capstone-style project (UTN), it integrates competencies in: distributed architecture, protocol design, concurrency, synchronization, resource management, scheduling, fault handling, and end-to-end validation.

### 1.1 System positioning

This repository should be described as a **distributed system** (multiple autonomous networked processes with partitioned responsibilities and coordination protocols), not merely as a single query orchestrator.

Within that distributed system, `MASTER` is the orchestration core for query lifecycle and scheduling decisions.

---

## 2. Architecture and interaction model

## 2.1 Logical architecture

- **External client**: Query Control.
- **Control plane**: Master.
- **Execution plane**: Workers.
- **Data plane**: Storage.

The main topology is **Master-centric** for coordination:

- Query Control ↔ Master
- Worker ↔ Master
- Worker ↔ Storage

There is no direct Query Control ↔ Worker or Query Control ↔ Storage coupling.

## 2.2 Summarized end-to-end flow

1. Query Control sends `OP_QUERY_SUBMIT` with `query_path` and `priority`.
2. Master assigns `query_id`, creates session, and responds `OP_QUERY_CONFIRM`.
3. Scheduler selects query and worker based on configured algorithm.
4. Master sends assignment to Worker (`OP_MASTER_SEND_PATH`).
5. Worker executes instructions; if there is a `READ`, it sends `OP_WORKER_READ_RESULT`.
6. Master forwards read data to Query Control (`OP_QUERY_READ_MESSAGE`).
7. Completion:
   - Success: `OP_WORKER_QUERY_FINISHED` → Master returns `SUCCESS` to Query Control.
   - Functional error: `OP_WORKER_QUERY_ERROR` → Master propagates error details.
   - Alternative full-result path: `OP_MASTER_QUERY_COMPLETE`.

---

## 3. Protocol and communication contracts

The protocol is binary opcode-based, with framing and serialization implemented in `utils`.

## 3.1 Master ↔ Query Control

- `OP_QUERY_SUBMIT`: query submission with priority.
- `OP_QUERY_CONFIRM`: confirmation with assigned `query_id`.
- `OP_QUERY_READ_MESSAGE`: intermediate read message (`file:tag|content`).
- `OP_QUERY_RESULT`: final result (`SUCCESS`, `ERROR`, or textual payload).

## 3.2 Master ↔ Worker

- `OP_MASTER_WORKER_REGISTER` / `ACK`: worker registration.
- `OP_MASTER_SEND_PATH`: query dispatch + path + program counter (`pc`).
- `OP_MASTER_EVICT` / `OP_WORKER_EVICT_ACK`: eviction (preemption).
- `OP_WORKER_READ_RESULT`: read-progress message.
- `OP_WORKER_QUERY_FINISHED`: successful completion.
- `OP_WORKER_QUERY_ERROR`: completion with error.
- `OP_MASTER_QUERY_COMPLETE`: full-result path.

## 3.3 Worker ↔ Storage

Logical filesystem operations (`CREATE`, `READ`, `WRITE`, `TRUNCATE`, `TAG`, `DELETE`, `FLUSH`, `COMMIT`) with `ST_OK`/`ST_ERROR` status codes.

---

## 4. MASTER module: technical-product definition

## 4.1 Business and engineering role

Within the distributed system, `MASTER` is the **query orchestrator** and central scheduler-dispatcher. Its functional contract is to:

- accept requests,
- manage queue/sessions,
- assign execution under configurable policy,
- supervise distributed-state consistency,
- return final response to the client.

From an engineering viewpoint, it combines **concurrent server + scheduler + health monitor**.

## 4.2 Core data structures

### a) `t_query_session`
Represents a live query in the system:

- identity: `query_id`
- associated sockets: Query Control / Worker
- execution state: `query_path`, `pc`, `is_active`, `estado`
- scheduling state: `priority`, `original_priority`, `ready_since`
- preemption control: `version` to validate `EVICT_ACK`

### b) `t_worker_info`
Represents a registered worker:

- identity: `worker_id`
- connection: `socket_conn`
- occupancy state: `is_busy`
- eviction control: `awaiting_evict_response`, `evict_request_time`

### c) Shared structures

- `query_sessions` (`t_list`) + `queries_by_id` (`t_dictionary`) for O(1) lookup.
- `workers_list` (`t_list`) + `workers_by_socket` (`t_dictionary`) for fast lookup.
- `query_next_id` with mutex for atomic assignment.
- `worker_count` with mutex for multiprocess-level metric.

## 4.3 MASTER lifecycle

1. Startup parameter validation and config loading.
2. Logger initialization and algorithm validation (`FIFO`/`PRIORIDADES`).
3. Server socket creation.
4. Lists, dictionaries, and synchronization primitives initialization.
5. Background thread launch:
   - connection monitor,
   - scheduler,
   - aging (if enabled),
   - watchdog.
6. Main `accept` loop + per-client handler.

## 4.4 Admission and client classification logic

The handler identifies client type by initial `opcode` (`MSG_PEEK`):

- Query Control (`OP_QUERY_SUBMIT`): creates session and confirms ID.
- Worker (`OP_MASTER_WORKER_REGISTER`): registers worker, increments count, sets it to `idle`.

This enables persistent connections with later asynchronous message flow.

---

## 5. Scheduling and assignment (Scheduling/Dispatch)

## 5.1 Supported algorithms

### FIFO
- Selects first `READY` query.
- Assigns it to first non-busy worker.
- No priority-based eviction.

### PRIORITIES
- Ascending priority order (lower number = higher priority).
- If a worker is free: direct assignment.
- If no worker is free: preemption evaluation over lower-priority active query.

## 5.2 Aging

When `TIEMPO_AGING > 0`:

- `READY` queries reduce their numeric priority value (down to minimum 1),
- `ready_since` is reset,
- queue is re-sorted,
- scheduler is signaled.

Product objective: mitigate starvation and improve fairness under high contention.

## 5.3 Preemption (Eviction)

Policy uses handshake-based eviction:

1. Master selects victim query (`RUNNING` with lower priority than incoming query).
2. It increments session `version` and sends `OP_MASTER_EVICT`.
3. Worker replies with `OP_WORKER_EVICT_ACK` containing `pc` and `version`.
4. Master validates version before accepting ACK.
5. Query returns to `READY` with updated `pc`.

### Expected ideal design

- version-safe preemption,
- no worker reassignment until valid ACK or controlled timeout,
- immediate re-scheduling after eviction.

### Observed state

- version control and ACK-wait flags exist,
- traces of alternate approaches coexist (commented blocks),
- there are pending evolution zones for full robustness under severe timeout scenarios.

---

## 6. Concurrency, synchronization, and threading

## 6.1 Main threads

- **Main accept loop**: socket admission.
- **Per-client handler**: continuous per-connection message processing.
- **Scheduler thread**: FIFO or PRIORITIES.
- **Aging thread**: priority aging (optional).
- **Connection checker**: disconnection detection.
- **Watchdog**: hung/inconsistent state verification.

## 6.2 Used primitives

- `pthread_rwlock_t` for shared lists/dictionaries.
- `pthread_mutex_t` for counters and point coordination.
- `pthread_cond_t` for event-driven scheduler signaling.
- `_Atomic(time_t)` in `ready_since` for safe temporal reads/writes.

### Semaphore-oriented interpretation

The MASTER synchronization model follows a **semaphore-based design logic**, even though this codebase does not currently declare POSIX `sem_t` objects explicitly:

- **Binary semaphore semantics** are implemented with `pthread_mutex_t` (mutual exclusion on critical sections such as `query_next_id` and `worker_count`).
- **Reader-writer semaphore semantics** are implemented with `pthread_rwlock_t` (multiple concurrent readers, single writer for shared scheduling structures).
- **Event/counting semaphore-like signaling** is implemented with `pthread_cond_t` + `pthread_mutex_t` (scheduler wake-up when new work, free workers, or priority changes occur).

From a systems-engineering standpoint, this keeps the same conceptual objective as semaphores: controlled access, deterministic ordering of critical updates, and safe thread coordination under contention.

## 6.3 Synchronization pattern

- concurrent reads allowed for frequently queried structures,
- exclusive writes for state transitions,
- explicit scheduler signaling on relevant events (new query, free worker, aging/eviction updates).

## 6.4 Technical considerations

- Throughput is prioritized via detached threads.
- There is no formal ordered shutdown sequence (long-lived process design).
- Some paths require strict lock-order discipline to avoid races/deadlocks in future extensions.

---

## 7. Memory and resource management

## 7.1 Ownership model

- `t_query_session` and `t_worker_info` are heap objects with explicit lifecycle.
- Dedicated release functions are provided:
  - `free_query_session`
  - `free_worker_info`

## 7.2 Operational strategy

- Allocation: on worker registration or query submit.
- Mutation: during scheduler/events.
- Deallocation: on query completion, error, disconnection, or worker cleanup.

## 7.3 Network resources

- One socket per client in persistent connection mode.
- Socket closure upon detected disconnection plus associated structure cleanup.

## 7.4 Memory/resource risks

- In persistent multithreaded design, cleanup hygiene per error branch is critical.
- There is no full `graceful shutdown` phase that consolidates process-wide resource release.

---

## 8. Validation, error handling, and resilience

## 8.1 Input and configuration validation

- startup argument validation,
- scheduling algorithm validation,
- config and server creation error handling.

## 8.2 Runtime errors

`MASTER` propagates functional errors from Worker to Query Control while preserving `query_id` traceability.

Test-covered cases include:

- existing-file creation attempt,
- write over already committed file,
- out-of-bounds read,
- duplicate tag.

## 8.3 Disconnection handling

- **Query Control disconnected**: associated query is finished/canceled in coordination with worker state.
- **Worker disconnected**: capacity reduction, state cleanup, and impacted session closure.

## 8.4 Watchdog

Main responsibilities:

1. detect overlong active queries,
2. fix workers marked busy without valid active query,
3. manage `EVICT_ACK` waiting timeout.

### Expected ideal design

- self-healing inconsistency correction without blocking scheduler,
- gradual capacity recovery from problematic workers,
- early alarming for anomalous timings.

### Observed state

- watchdog is operational and useful as a consistency defense,
- full automatic recovery of blocked query paths is not yet fully closed in every branch.

---

## 9. Security and attack surface

## 9.1 Technical assessment

The system is academically/functionally focused, with **non-hardened** transport security and authentication:

- no TLS,
- no strong client authentication,
- no role-based authorization beyond opcode protocol behavior.

## 9.2 Main risks

- client spoofing by any actor that can speak protocol,
- malformed/oversized payload injection,
- query path usage without strict path-policy normalization.

## 9.3 Recommended hardening (roadmap)

1. mTLS or encrypted transport channel.
2. authenticated handshake via token/certificate.
3. global payload limits and per-socket rate limiting.
4. strict `query_path` validation (allowlist + canonicalization).
5. security-event auditing separated from functional log.

---

## 10. Operational configuration and deployment

## 10.1 MASTER parameters

- `PUERTO_ESCUCHA`
- `IP_ESCUCHA`
- `LOG_LEVEL`
- `ALGORITMO_PLANIFICACION`
- `TIEMPO_AGING`

## 10.2 Build and dependencies

- per-module build with `make`,
- shared dependency `so-commons-library`,
- orchestrated compilation scripts (`compilar_todo.sh`),
- deployment support with Docker and run scripts.

## 10.3 Expected operation

- start Storage,
- start Master,
- register Workers,
- execute Query Controls,
- monitor logs and test script results.

---

## 11. Product validation (tests and quality)

## 11.1 Relevant functional evidence

- FIFO and PRIORITIES suites,
- aging stress with multiple concurrent queries,
- end-to-end error propagation suite,
- general stability scripts.

## 11.2 Technical-product acceptance criteria

1. correct submit admission with ID confirmation.
2. consistent assignment under configured algorithm.
3. intermediate READ message propagation to client.
4. correct final result (`SUCCESS`/`ERROR`/payload).
5. consistent behavior on client or worker disconnections.
6. observable fairness in PRIORITIES + aging.

---

## 12. Target state (ideal) vs implemented state

| Area | Ideal target state | Observed state |
|---|---|---|
| FIFO Scheduling | deterministic, no preemption | implemented |
| Priority Scheduling | priority-based selection + stable preemption | implemented with ongoing evolution in timeout/ack branches |
| Aging | configurable anti-starvation | implemented when `TIEMPO_AGING > 0` |
| Resilience | inconsistency self-healing | watchdog operational, full recovery still incremental |
| Error propagation | E2E and query_id-traceable | implemented and tested via dedicated scripts |
| Security | authentication + encryption + hardening | functional baseline without strong hardening |

---

## 13. Technical risks and prioritized debt

### Highest-impact risks

1. race conditions under high contention between scheduler, handlers, and connection monitoring.
2. absence of orderly shutdown for full resource cleanup.
3. minimal network security (academic environment, not internet-exposed production).

### Prioritized technical debt

1. definitively unify preemption/ACK/timeout strategy into one flow.
2. formalize explicit query cancellation on Query Control disconnection.
3. externalize watchdog parameters (interval/timeout) to configuration.
4. broaden extreme-concurrency and intermittent-network-failure tests.

---

## 14. Academic conclusion

The system represents a **solid integrative case** for Information Systems Engineering (UTN), combining distributed design, process scheduling, multithread synchronization, and operational control. Within the ecosystem, `MASTER` has a central and high-complexity role: it sustains the execution model, applies fairness policies, coordinates partial fault tolerance, and materializes the service contract toward the user.

From a technical-product synthesis standpoint, the project demonstrates advanced undergraduate-level maturity: clear modular architecture, operational custom protocol, and real concurrent scheduling capabilities, with a well-identified evolution path toward stronger robustness and security.

---

## 15. Short glossary
- **Query Session**: lifecycle context of a query.
- **Scheduler**: assignment planning component.
- **Dispatch**: effective sending of query to worker.
- **Preemption / Evict**: eviction of running query.
- **Aging**: progressive priority improvement to avoid starvation.
- **Watchdog**: state-health monitoring thread.
- **Ready / Running**: query states waiting or executing.
- **Multiprocessing level**: number of active workers available for concurrent execution.
