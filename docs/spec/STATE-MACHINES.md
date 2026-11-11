# AutoLink V2.2 Orthogonal State Machines

Status: SC2 Root-Cause Closure, **NON-NORMATIVE / NO-FREEZE / NO-SC3 / NO v2.2.0 TAG**.

The node is a product of independent state dimensions, not one growing enum.

| Dimension | Values | Sole concern |
| --- | --- | --- |
| commission | NONE, REGISTERING, PENDING, COMMITTED | durable identity progress |
| network | NO_NET, ATTACHED, PROBATION, MIGRATION_CANDIDATE | network allegiance |
| service | NORMAL, DEGRADED, QUARANTINED_REG, QUARANTINED_SERVICE | service eligibility |
| power | AWAKE, SLEEP_ARMED, SLEEPING | request availability |
| baud | IDLE, PREPARED, SWITCH_WAIT | `0x1C` transaction |
| administrative | ACTIVE, RETIRED | operator retirement |

## Transition ownership

| Transition | Owner | Persistence |
| --- | --- | --- |
| ASSIGN/READDRESS accepted | `assign_pending` / `al_assign_pending` | IDENTITY_PENDING |
| first valid candidate-address transaction with a normal operation result | request transaction planner using `commit_commission` / `al_commit_commission` | COMMISSION_COMMITTED joined to the operation plan |
| confirmed foreign-network migration | `clear_commission_atomic` / `al_clear_commission_atomic` | IDENTITY_CLEAR |
| valid own-network activity | `on_own_network_activity` | none |
| quarantine entry | `enter_quarantine` / `al_enter_quarantine` | policy-specific |
| quarantine timer expiry | `quarantine_retry` / `al_quarantine_expire` | none |
| SERVICE recovery | `on_action_delivery` / `al_on_action_delivery` | none |
| request durable finalization | `al_node_engine_execute` transaction plan | one A/B snapshot |
| boot runtime reconstruction | `al_node_engine_restore` | reads durable snapshot |
| sleep arm/wake | `al_node_tick` / `al_node_wake` | none |
| Node timer start/cancel/expiry qualification | lifecycle owner + `al_node_timer_*` elapsed primitive | timer epochs are never persisted |
| Node time observation/publication | Core `al_node_tick` before request classification; `al_node_engine_advance` before adapter effects | durable timeout transitions commit before publication |
| Master SLEEP lease release/recovery | Master lease timer + work scheduler | relative lease state/duration only |
| Master baud migration | `al_master_baud_*` orchestrator | Master registry as required |

No transport, UART, hash-slot, or response-building function owns a transition.
Schema response decoration may stage schema state, but only the outer request
transaction may persist or publish that state.

## Required invariants

- commission NONE implies address `0xF8`, no NETWORK_ID, and network NO_NET;
- commission COMMITTED implies an assigned address and nonzero NETWORK_ID;
- QUARANTINED_REG implies commission NONE;
- QUARANTINED_SERVICE implies commission COMMITTED;
- SERVICE retry-due is valid only in QUARANTINED_SERVICE;
- MIGRATION_CANDIDATE always carries a candidate tuple;
- RETIRED or non-AWAKE nodes cannot enter collect handlers.
- a valid recent cache always owns its active retention timer and matching response;
- SLEEP_ARMED always retains the original duration and an active sleep-arm timer;
- PREPARED/SWITCH_WAIT always carries a coherent TID, baud and active timer relation;
- REGISTERING always owns an active registration timer;
- PENDING owns an active assignment-confirmation timer, while no non-PENDING
  state may retain one;
- every active Node timer has a nonzero interval at most `INT32_MAX`; timestamp
  value zero is legal and never means inactive;
- every Master runtime epoch is 64-bit and no absolute uptime is durable;
- PENDING is always AWAKE outside a private request plan; a request that confirms
  commission and changes power state publishes only the complete valid result.

Both Python and C cores assert these invariants after transitions. Legacy labels
such as READY or ADDRESSED are read-only projections for historical scenarios;
they are not writable state.
## Root-Cause Closure states

- Schema: durable identity equal → retain revision; changed → stage next revision/signature → persist → publish; failure → retain old canonical schema.
- Candidate request: classify from snapshot → build private plan → speculatively
  apply commission confirmation → execute the generic operation → on a normal
  result retain confirmation → validate → persist once → publish. ERROR/IGNORE
  discard only the speculative confirmation and follow their existing
  operation-owned runtime semantics; persistence failure restores the complete
  pre-request engine snapshot and suppresses success response/effect/cache. A
  visible persistence failure is attributed only to a durable operation owned by
  that request, never to unrelated timer durability merged into its plan.
- Master sleep: NONE → UNCERTAIN at first TX complete → CONFIRMED on ACK; reboot of any active durable lease → RECOVERY_VERIFY. The Core arms a base delay, selects verification fairly, records IN_FLIGHT, and on terminal timeout records TIMEOUT plus bounded backoff while retaining the lease. A valid ordinary response records EARLY_WAKE and stages NONE; failed persistence rolls back to a conservative, non-in-flight RECOVERY_VERIFY state.
- Master work: SLEEP_RECOVERY_VERIFY / ASSIGN / DISCOVER / READ_SAMPLE are selected with a Core-owned class round-robin. Sleep, assign and poll node cursors advance when work is issued, so one sleeping or unassigned node cannot monopolize the next eligible work slot. Firmware provides time/entropy and sends the returned transaction only.
- Master baud migration: SILENCE → PERSIST_REQUIRED → (success) SET_BAUD/VERIFY or (failure) BAUD_RECOVERY.
- Master baud recovery: UNKNOWN → candidate effect → candidate-major READ_ADDR proofs across all eligible known nodes → RECOVERY_PERSIST → (success) COMPLETE/KNOWN or (failure) UNKNOWN/first candidate. A valid normal or contract-valid exception response is physical-baud proof; stale `online=false` is not a recovery exclusion. A proofless full pass reports ABORTED but re-arms UNKNOWN recovery rather than entering ordinary work.
- Master persistence: NONE → NODE/BAUD staged → success finalize or failure rollback/recovery. Adapter supplies only the write result.
- Node timer: lifecycle owner starts/cancels one explicit record; the shared
  elapsed primitive alone qualifies expiry across wrap; the lifecycle owner
  performs the resulting transition. Numeric due ticks are projections only.
  A request observes expiry before classification. State-only changes join its
  transaction; external effects drain one at a time as SET_BAUD then ENTER_SLEEP,
  with persistence rollback before any effect publication. The Core post-state
  predicate alone distinguishes a due external effect from future effect
  metadata; firmware never reinterprets that lifecycle state.
- Master long SLEEP: first physical TX fixes a 64-bit epoch; ACK confirms the
  lease without moving it; the complete `uint32_t` duration plus arm/grace is
  representable. Reboot discards uptime and reconstructs RECOVERY_VERIFY from
  durable relative state.
