# AutoLink v2.2.0 Public Integration API

Status: **NON-NORMATIVE / NO-FREEZE / NO-SC3**

Wire change: **false**

Planned compatibility scope: controlled source-level integration for AutoLink v2.2.x and the pinned GrainSilo baseline. No binary ABI promise is made.

Accepted Pre-Freeze audit baseline: `ef32fcdb39a47414778ddc4d7375f66e3aee36ed`.

The machine-readable classification owner is `GrainSilo-AutoLink-v2.2.0-API-Allowlist.json`; `generated/autolink_public_api_inventory.json` is its exact expanded projection over current public headers. Any exported `al_*` symbol not explicitly marked `PUBLIC / SUPPORTED` is `INTERNAL / PRIVATE` by default.

## Boundary rule

`al_node_engine_t` and `al_master_t` are supported static-allocation types. Their concrete members are not application APIs. Production firmware may hold these objects and pass their addresses to supported functions, but must not read or write their members.

Protocol lifecycle, timers, transaction ownership, baud orchestration, scheduling and persistence transitions remain Core-owned. The facade exposes semantic operations and facts only.

## Node integration

### Initialization and restore

1. Create an `al_storage_port_t` and an `al_node_application_port_t`.
2. Call `al_node_engine_init()` once.
3. Call `al_node_engine_restore()` before querying platform status. A false result means no valid snapshot was restored and the Engine has reset itself to initialized defaults.
4. Call `al_node_engine_set_application_port()` and, after the platform sensor is ready, `al_node_engine_sync_schema()`.
5. Call `al_node_engine_platform_status()` and start the UART with `status.active_baud`. False means invalid arguments.

### Request execution and response delivery

Build `al_request_t` from an already parsed frame. `now_ms` is the Node lifecycle projection of the observed request CRC-stop timestamp, and `request_crc_stop` carries that platform observation. Pass the request to `al_node_engine_execute()`.

The returned `al_action_t` is the only instruction consumed by the Adapter:

- response action kinds are encoded and transmitted;
- a scheduled response is followed by one `al_node_engine_on_delivery()` call with the observed delivery outcome/end time;
- no Adapter field read is needed to decide protocol state.

### Time advance and platform effects

Call `al_node_engine_advance()` with the current Node lifecycle time until it returns an action with `AL_EFFECT_NONE`. False means a durable transition was rolled back and no effect may be published.

Before applying any Engine-produced baud or sleep effect, call `al_node_engine_effect_is_due()`. False always means **do not publish the effect**. This function delegates to the accepted Core effect owner; firmware must not compare deadlines or reconstruct due state.

## Master integration

### Initialization, restore and persistence

1. Initialize the journal and call `al_master_init()` with a non-zero network identity.
2. Restore durable node records using `al_master_restore_records()`. False means the supplied record set was rejected. The function does not promise an unchanged Master object on failure: discard/reinitialize that object before any further use. The bundled firmware follows this rule by falling back to `al_master_init()`.
3. Restore the stored baud with `al_master_restore_bus_baud()`; if the stored fact is absent or invalid, call `al_master_mark_bus_baud_unknown()` so Core owns recovery.
4. For a new registry, call `al_master_stage_registry_persistence()` and complete the normal dirty-edge/write/feedback sequence.

When Core requests persistence, consume the edge with `al_master_take_registry_dirty()`, call `al_master_export_registry()`, durably write the Adapter-owned registry record, then call `al_master_on_persist_result()` exactly once with that write result.

`al_master_export_registry()` is atomic from the caller's perspective:

- `AL_MASTER_REGISTRY_EXPORT_OK` returns network identity, current bus baud, baud-known state and every durable node record;
- `AL_MASTER_REGISTRY_EXPORT_INVALID_ARGUMENT` or `AL_MASTER_REGISTRY_EXPORT_INSUFFICIENT_CAPACITY` leaves the snapshot and record outputs unchanged;
- no transaction state, work cursor or boot-local deadline is exported.

Persistence must occur after RS485 driver-enable is released. Restored SLEEP state remains relative/conservative; no monotonic runtime deadline is serialized.

### RX, tick and events

Call `al_master_receive_at_timestamp()` for a parsed response with its RX CRC-stop timestamp. Call `al_master_tick_us()` from the serialized event loop before asking for new work. Apply its event without interpreting Core state fields:

- `AL_MASTER_EVENT_RETRANSMIT`: transmit `event.tx_frame` through the normal authorization/completion path;
- `AL_MASTER_EVENT_PERSIST_REQUIRED` or `event.persist_registry`: perform the persistence sequence above;
- `AL_MASTER_EVENT_SET_BAUD`: apply `event.effect_value` to the UART;
- `AL_MASTER_EVENT_BAUD_RECOVERY`: apply the candidate baud and then call `al_master_baud_recovery_applied()`;
- accepted sample payloads may be handed to the GrainSilo presentation layer using the event trust flag.

### Core-owned scheduling and TX ownership

After tick/event handling, call `al_master_next_transmission()` with the same monotonic millisecond epoch, discovery entropy and slot count:

- `AL_MASTER_NEXT_TX_INVALID`: invalid pointer arguments;
- `AL_MASTER_NEXT_TX_NONE`: no transmission is currently authorized;
- `AL_MASTER_NEXT_TX_READY`: `request` contains an already-started Core transaction selected across baud management and normal fair scheduling.

Do not call `al_master_baud_next()` or `al_master_next_work()` from production firmware. They remain internal implementation surfaces.

Immediately before physical TX, `al_master_tx_authorized()` must return true for the exact frame. It validates the pending Core transaction and complete frame identity; false means **do not transmit**. After UART flush/CRC-stop observation, call `al_master_on_tx_complete_timestamp()` before later RX/tick progression.

### Active baud

After initialization/restore and whenever platform startup needs the fact, call `al_master_platform_status()` and use `status.active_baud`. Firmware does not need to know where Core stores the restored or recovered baud.

## Call-order and concurrency contract

NodeEngine and Master objects are serialized, non-reentrant state machines. One Adapter event loop owns each object. Do not call the same object concurrently from ISR/task contexts without external serialization.

Important orderings are:

```text
Node: init -> restore -> platform status -> UART start
Node: execute/advance -> effect-is-due -> platform effect
Node: scheduled TX -> delivery feedback

Master: init -> restore -> platform status -> UART start
Master: RX/tick -> event effects/persistence -> next transmission
Master: Core-produced frame -> TX authorization -> physical TX -> TX-complete timestamp
Master: durable write -> persist-result feedback
```

## Version boundary

Only symbols in the GrainSilo allowlist are supported for the planned v2.2.0 dependency. Future AutoLink versions are not adopted automatically. Any allowlist change requires explicit compatibility review and a newly pinned candidate.
