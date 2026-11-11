# AutoLink V2.2 Current Protocol Specification

Status: **Implementer reference for the published v2.2.0 software baseline**

Wire compatibility: `VER=0x01`, `PROTO_VER=0x0200`

Declarative source: `spec/autolink_spec.py`

This document describes the implementer contract for the software baseline.
Use it alongside the generated tables and the matching tagged source.

## 1. Frame

Every frame is:

`VER | ADDR | SEQ | FUNC | LEN | DATA[LEN] | CRC16-LE`

- Header length is 5 bytes, maximum DATA is 247 bytes, maximum frame is 254 bytes.
- CRC is Modbus CRC-16 (`0xA001`, initial `0xFFFF`) over header and DATA.
- `ADDR=0x00` is broadcast, `0x01..0xEF` is assigned, and `0xF8` is the
  unassigned identity.
- Parser failures are local `GS_PARSE_*` results. They are never encoded as
  AutoLink exception bytes.

The generated function, error, timing and registry values in
`generated-spec-tables.md` are part of this current contract. Hand-copied numeric
tables are intentionally absent here.

## 2. One request path

Every parsed request, including DISCOVER, RECONCILE and RECOVER, enters exactly
one node execution function. The pipeline order is:

1. envelope and payload validation;
2. administrative and power eligibility;
3. address, UID and network eligibility;
4. generic recent-transaction or baud-TID qualification;
5. construction of one transaction plan containing all durable operations,
   runtime transitions, response and delayed effects;
6. atomic persistence of the complete durable snapshot;
7. RAM finalization and response/effect eligibility;
8. transport arbitration and delivery feedback.

Transport code MUST NOT call lifecycle handlers or mutate node state. It may only
arbitrate and emit `IGNORE`, `STATE_ONLY`, `ERROR`, `IMMEDIATE_RESPONSE`, or
`SCHEDULED_RESPONSE` actions.

## 3. Commission confirmation

ASSIGN (`0x11`) and READDRESS (`0x1D`) persist an identity as `PENDING` and emit
REG_CONFIRM (`0x12`). Thereafter, **any valid transaction addressed to the
candidate address**, except a new ASSIGN/READDRESS, atomically changes commission
to `COMMITTED` before that same transaction continues. No function-specific
confirmation list exists.

The request is first classified against the pre-request snapshot. Candidate
confirmation is then applied speculatively to one private planned state before
the operation executes, so no helper observes an invalid intermediate product
state. The confirmation and its durable bit are retained only for a normal
operation result; `ERROR` and `IGNORE` discard the speculative confirmation
while preserving any separately defined operation-owned runtime semantics. The
invariant is checked on the complete planned state before it is published once.

The durable operations are composable members of one transaction plan:

- `IDENTITY_PENDING`: address and NETWORK_ID are stored with PENDING marker;
- `COMMISSION_COMMITTED`: only the commission marker advances;
- `IDENTITY_CLEAR`: address, NETWORK_ID and commission are cleared together.

A PENDING-node `WRITE_CFG`, for example, owns both `CONFIG` and
`COMMISSION_COMMITTED` in the same plan and the same A/B journal commit. A
failed commit exposes neither a success response nor a delayed hardware effect.
Schema decoration joins that same outer request plan; it MUST NOT perform an
independent nested journal commit.

## 4. Collection requests

- DISCOVER DATA is `SEED4 | SLOTS1 | NETWORK_ID8`; eligible unassigned nodes emit
  REGISTER reports in deterministic Murmur3 slots.
- RECONCILE has the same DATA layout; attached service-eligible nodes emit
  RECONCILE reports.
- RECOVER DATA remains the frozen recovery layout defined by the generated
  request schema and produces RECOVER reports.
- `SLOTS` is one of 8, 16, 32, 64. Invalid broadcast payloads are silent and
  counted locally.
- RETIRED and SLEEPING nodes are ineligible before collect handlers run.

## 5. Network migration

A committed node ignores foreign DISCOVER during the 15 s own-network grace
period. After grace, one foreign transaction records a candidate. Only a second,
distinct transaction for the same foreign NETWORK_ID releases identity. Release
uses the single `IDENTITY_CLEAR` transition, setting address to `0xF8`, NETWORK_ID
to zero/no-net, and commission to NONE atomically. Any valid own-network activity
cancels the candidate.

## 6. Quarantine

`QUARANTINED_REG` and `QUARANTINED_SERVICE` are different service qualifiers.
REG expiry restores discovery eligibility. SERVICE expiry does **not** manufacture
READY: it only enables one RECONCILE retry. Successful scheduled-report delivery
restores NORMAL; collision or failed delivery returns to backoff.

## 7. Baud transaction and idempotency

Supported and recovery-scan baud registries are identical and generated from the
same list: 9600, 19200, 57600, 115200. `38400` is not persistable.

Generic side-effect idempotency is keyed by `(SEQ, FUNC, request fingerprint)`.
The recent cache is global across the generic idempotent family: within
`DUP_CACHE_TTL`, a repeated SEQ with a different FUNC or fingerprint is
`ERR_TXN_CONFLICT`; an exact duplicate replays the finalized response/effect
plan without restarting deadlines. Expiry is measured from completion time.
The `0x1C` baud transaction has a separate TID store. QUERY with no active PREPARE
returns normal REJECT/NO_PREPARE; QUERY with an active mismatched TID returns
`ERR_TXN_CONFLICT`. COMMIT must bind TID, prepared baud and the frozen 200 ms
switch delay.

The Master baud orchestrator owns PREPARE broadcast, QUERY of every eligible
online node, all-READY qualification, three same-SEQ COMMIT copies with the
generated copy gap, MASTER_SILENCE, local switch, READ_ADDR verification and
registry-ordered recovery scan. The platform adapter only transmits returned
frames and applies returned hardware events.

## 8. Boot, timers and sleep

Persistent snapshots contain durable facts, not transient projections. Boot
reconstruction follows the generated precedence `RETIRED > PENDING >
QUARANTINED > COMMITTED > NONE`; COMMITTED restarts in PROBATION, while caches,
baud transactions and SLEEP_ARMED are cleared conservatively. Durable
`schema_rev` and descriptor signature are restored.

The Core owns cache TTL, registration, assignment confirmation, PREPARE,
sleep-arm, quarantine, grace/migration, baud-switch and completed-COMMIT timing.
A nonzero SLEEP retains its duration, ACKs duplicates without moving the first
arm deadline, emits `ENTER_SLEEP(duration)`, and wakes into the appropriate
attached/probation lifecycle. The Master records an uncertain lease at physical
TX completion and confirms/persists it only after ACK.
On a PENDING node, commission confirmation and sleep arming are one planned
transition and one durable commit. Persistence failure restores both dimensions,
suppresses ACK/effect/cache publication, and permits the exact retry to execute.
A visible request-level persistence failure is legal only when that request owns
the failed durable operation. Durability coalesced from an unrelated timer expiry
must not fabricate a request completion or inspect payload fields that request
validation did not establish.

### Runtime time model

The Node adapter supplies one wrapping 32-bit millisecond projection: the lower
32 bits of `esp_timer_get_time()` microseconds divided by 1000. Request receive,
delivery completion and periodic tick paths all use that single conversion
owner. Every normative Node timer is an explicit record `{active, start_ms,
interval_ms}`. Activity is
never encoded by a timestamp value: zero is a legal start or wrapped due tick.
Intervals are nonzero and no greater than `INT32_MAX`; expiry is defined only as
`active && uint32(now_ms - start_ms) >= interval_ms`. A wrapped `start+interval`
value may be exposed for diagnostics, but it is not used for ordering.
Raw observations must be monotonic modulo `2^32` (no stale/out-of-order adapter
timestamps), and an active timer must be serviced by a tick, request or delivery
before one complete 32-bit millisecond epoch elapses. These are explicit
platform liveness conditions; all generated protocol intervals are at most 90 s
and statically remain inside the declared half-epoch domain.

A request timestamp is itself a time observation. The Node Core time-advance
stage applies every timer whose `age >= interval` before classifying that
request. State-only transitions join the same request transaction plan. A time
advance exposes at most one platform effect; any later due effect remains active
for another advance at the same timestamp. Effects drain deterministically as
`SET_BAUD` then `ENTER_SLEEP`. A Node Core post-state predicate is the sole
publication authority that distinguishes a committed due platform effect from
future or still-uncommitted effect metadata. The request path repeats NodeEngine
execution at one request timestamp only when that predicate is true; SET_BAUD
applies and retries, while
ENTER_SLEEP terminates that runtime. Future metadata is never applied early.
The periodic no-request path drains NodeEngine advance. Core execution advances
time first and returns a due effect without classifying the request. NodeEngine
persists a durable timeout transition before publishing it and restores the
complete prior state on persistence failure.

The eight logical Node timer records are registration, assignment confirmation,
baud PREPARE, sleep arm, quarantine, baud switch, duplicate cache and completed
COMMIT retention. Their start/restart/cancel/expiry/reboot owners are generated
from `SPEC['runtime_time_model']['node_timers']`. Own-network migration silence
uses the same modular elapsed primitive as an always-valid timestamp anchor, not
an active/inactive timer. Lifecycle invariants require each active state and its
timer in both directions.

Master Core consumes one 64-bit monotonic millisecond epoch. The production
ESP32 adapter derives it from `esp_timer_get_time()`; all Master transaction,
baud, work-scheduler, recovery-probe and lease epochs use that type. A legal
nonzero `uint32_t sleep_ms` is represented without narrowing:
`expected_wake = first_tx + SLEEP_ARMED_WINDOW + sleep_ms`, and lease release is
not eligible before `expected_wake + GRACE_PERIOD`. Absolute uptime is transient
and never serialized. Reboot reconstructs a durable active lease as
`RECOVERY_VERIFY` against the new boot epoch, preserving the conservative lease
instead of restoring an old absolute deadline.

This millisecond lifecycle model does not alter collect-slot timing. The latter
continues to use its 64-bit physical-microsecond CRC-stop to VER-start contract.

## 9. Trust and descriptors

Frame validity does not imply semantic trust. Sample channels whose descriptor
width or registry meaning conflicts are retained for diagnostics but marked
untrusted. The descriptor value is six bytes:

`CH_ID | QUANTITY_ID | DATA_TYPE | UNIT | EXP | FLAGS`

## 10. Conformance

An implementation is conforming only when all gates in `CONFORMANCE.md` pass.
Python simulation alone, C compilation alone, or documentation exact-diff alone
is insufficient. Hardware timing and real multi-drop collision behavior remain
separate device-level evidence.

## 11. Complete machine contract

This specification and the following current, generated artifacts are the only
implementation inputs required; RC history is not required:

- `generated-spec-tables.md`: request length/address/transaction matrix,
  response contracts, state eligibility, enum/reserved ranges, error
  applicability, idempotency, baud registry/TID policy and timing references;
- `generated/autolink_semantics.json`: lossless declarative semantic model;
- `generated/wire_lock.json`: exact frozen allocation surface;
- `tools/vectors.json`: current golden frames and timing derivations;
- `CONFORMANCE.md`: executable acceptance gates.

Byte layouts for frame, sample, INFO base/descriptor, reports and QUERY response
are owned by `SPEC['codec']` and rendered into the generated artifacts. Unknown
or reserved values follow the generated enum/error direction rules; an adapter
may not assign or reinterpret them.
## SC2 Root-Cause Closure semantic owners

This candidate remains NON-NORMATIVE / NO-FREEZE. The frozen collect target is `RX request CRC-stop + T_IFG + slot × T_SLOT(operation)`, measured to the response VER start bit. Slot 0 therefore waits T_IFG. The required absolute bound is ±500 µs and the two-node relative bound is ≤1000 µs; the ESP32 adapter in this package does not provide verified physical evidence for those bounds.

NodeEngine owns canonical descriptor identity and SCHEMA_REV. Application callbacks provide descriptor facts only. A changed canonical encoded TLV signature advances revision once; `{revision, signature}` is persisted before the new descriptor/sample meaning is published.

Node Core owns time observation. Every request advances due timers before
classification; NodeEngine owns durable publication/rollback, and firmware only
drains the ordered platform effects returned by the engine.

Master owns SLEEP NONE/UNCERTAIN/CONFIRMED/RECOVERY_VERIFY and the immutable first physical TX epoch of one logical request. A reboot without stable time reconstructs RECOVERY_VERIFY, not a numeric zero deadline. Master Core also owns each verification probe's eligibility, in-flight/result state, generated bounded backoff, and round-robin work-class/node cursors. A terminal verification timeout retains the conservative lease without making the node OFFLINE; ASSIGN, DISCOVER and READ_SAMPLE remain serviceable between probes.

Master also owns durable current bus baud. Local UART switching follows successful persistence. Missing/corrupt state enters a candidate-major generated baud-registry scan before ordinary traffic. At each candidate, every own-network known-address node without a sleep lease is eligible even if its old `online` projection is false. A contract-valid normal READ_ADDR or contract-valid Wire exception proves the physical candidate baud, which becomes runtime-known and is staged durably; recovery completes only after persistence succeeds. Persistence failure or a proofless full pass leaves the baud unknown and restarts the generated scan.
