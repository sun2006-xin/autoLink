# AutoLink V2.2 Generated Current Semantic Tables

> Source: `spec/autolink_spec.py`; generated without hand-maintained protocol policy.
> NON-NORMATIVE / NO-FREEZE / NO v2.2.0 TAG.

## Wire lock

| category | name | value |
|---|---|---|
| addresses | BROADCAST | `0x00` |
| addresses | MIN | `0x01` |
| addresses | MAX | `0xEF` |
| addresses | SYSTEM | `0xF0` |
| addresses | UNASSIGNED | `0xF8` |
| addresses | RESERVED | `0xF9` |
| addresses | INVALID | `0xFF` |
| functions | READ_SAMPLE | `0x03` |
| functions | READ_INFO | `0x04` |
| functions | DISCOVER | `0x05` |
| functions | WRITE_CFG | `0x06` |
| functions | RECONCILE | `0x07` |
| functions | DIAG | `0x08` |
| functions | REPORT | `0x10` |
| functions | ASSIGN | `0x11` |
| functions | REG_CONFIRM | `0x12` |
| functions | READ_ADDR | `0x13` |
| functions | SLEEP | `0x17` |
| functions | EVENT_RESERVED | `0x1A` |
| functions | NET_MGMT | `0x1C` |
| functions | READDRESS | `0x1D` |
| functions | RECOVER | `0x1E` |
| functions | ERR_BASE | `0x80` |
| wire_errors | ILLEGAL_FUNC | `0x01` |
| wire_errors | BAD_PAYLOAD_LEN | `0x02` |
| wire_errors | INVALID_VALUE | `0x03` |
| wire_errors | SENSOR | `0x04` |
| wire_errors | BUSY | `0x05` |
| wire_errors | PROTO_FEATURE | `0x07` |
| wire_errors | NET_ID | `0x08` |
| wire_errors | TXN_CONFLICT | `0x09` |
| wire_errors | PERSIST_FAILED | `0x0A` |

## Request contracts

| operation | FUNC | address rule | length/schema | transaction | retry | idempotent | durable facts |
|---|---:|---|---|---|---:|---:|---|
| READ_SAMPLE | `0x03` | current_addr | 0 | UNICAST | True | True | none |
| READ_INFO | `0x04` | current_addr | 1 | UNICAST | True | True | none |
| WRITE_CFG | `0x06` | current_addr | typed | UNICAST | True | True | CONFIG |
| DIAG | `0x08` | current_addr | 0..247 | UNICAST | True | True | none |
| SLEEP | `0x17` | current_addr | 4 | UNICAST | True | True | MASTER_SLEEP_LEASE |
| EVENT_RESERVED | `0x1A` | current_addr | 0 | UNICAST | False | True | none |
| ASSIGN | `0x11` | uid_at_unassigned | 15 | UID_DIRECTED | True | True | IDENTITY |
| READ_ADDR | `0x13` | current_addr | 0 | UNICAST | True | True | none |
| NM_PREPARE | `0x1C` | broadcast | 14 | BROADCAST_NOACK | False | True | none |
| NM_QUERY | `0x1C` | current_addr | 10 | UNICAST | True | True | none |
| NM_COMMIT | `0x1C` | broadcast | 16 | BROADCAST_NOACK | False | True | BAUD |
| DISCOVER | `0x05` | broadcast | 13 | COLLECT | True | False | none |
| RECONCILE | `0x07` | broadcast | 13 | COLLECT | True | False | none |
| RECOVER | `0x1E` | broadcast | 14 | COLLECT | True | False | none |
| READDRESS | `0x1D` | uid_at_broadcast | 15 | UID_DIRECTED | True | True | IDENTITY |

## Response contracts

| operation | normal FUNC | exception | response address rule |
|---|---|---:|---|
| READ_SAMPLE | READ_SAMPLE | True | request |
| READ_INFO | READ_INFO | True | request |
| WRITE_CFG | WRITE_CFG | True | request |
| DIAG | DIAG | True | request |
| SLEEP | SLEEP | True | request |
| EVENT_RESERVED | none | True | request |
| ASSIGN | REG_CONFIRM | True | unassigned |
| READ_ADDR | READ_ADDR | True | request |
| NM_PREPARE | none | False | none |
| NM_QUERY | NET_MGMT | True | request |
| NM_COMMIT | none | False | none |
| DISCOVER | REPORT | False | unassigned |
| RECONCILE | REPORT | False | current |
| RECOVER | REPORT | False | current_or_unassigned |
| READDRESS | REG_CONFIRM | True | new_normal_old_exception |

## State eligibility

| operation class | commission | service |
|---|---|---|
| ordinary | PENDING, COMMITTED | NORMAL, DEGRADED |
| ASSIGN | NONE, REGISTERING, PENDING | NORMAL, QUARANTINED_REG |
| READDRESS | PENDING, COMMITTED | NORMAL, DEGRADED, QUARANTINED_SERVICE |
| DISCOVER | NONE, REGISTERING, PENDING, COMMITTED | NORMAL, DEGRADED, QUARANTINED_REG, QUARANTINED_SERVICE |
| RECONCILE | COMMITTED | NORMAL, DEGRADED, QUARANTINED_SERVICE |
| RECOVER | NONE, REGISTERING, PENDING, COMMITTED | NORMAL, DEGRADED, QUARANTINED_REG, QUARANTINED_SERVICE |

## Enum, reserved-value and bitmask registry

| registry | kind | assigned | reserved | allowed mask |
|---|---|---|---|---:|
| REPORT_TYPE | ENUM | `[1, 2, 3]` | 00,04-FF | `n/a` |
| NM_OP | ENUM | `[1, 2, 3]` | 00,04-FF | `n/a` |
| QUERY_STATUS | ENUM | `[0, 1]` | 02-FF | `n/a` |
| QUERY_REASON | ENUM | `[0, 1, 2, 3]` | 04-FF | `n/a` |
| RECOVER_MODE | ENUM | `[1]` | 00,02-FF | `n/a` |
| CONFIG_ID | ENUM | `[1, 2, 3, 4]` | 05-FF | `n/a` |
| SUB_0x04 | ENUM | `[0, 1]` | 02-FF | `n/a` |
| DATA_TYPE | EXTENSIBLE_ENUM | `[1, 2, 3]` | 04-FF | `n/a` |
| UNIT_ID | EXTENSIBLE_ENUM | `[1, 2, 3, 4, 5, 6, 7]` | 08-FF | `n/a` |
| QUANTITY_ID | EXTENSIBLE_ENUM | `[1, 2, 3, 4, 5, 6, 7, 8]` | 09-FF | `n/a` |
| FLAGS_BIT | BITMASK | `[1]` | bit1-7 | `1` |
| STATUS_BIT | BITMASK | `[1, 2, 4]` | bit3-7 | `7` |
| SLOTS | ENUM | `[8, 16, 32, 64]` | other | `n/a` |
| STYPE | ENUM | `[0, 1]` | 02-FF | `n/a` |
| RESULT_0x06 | ENUM | `[0, 1, 2, 3]` | 04-FF | `n/a` |
| SLEEP_STATUS | ENUM | `[0]` | 01-FF | `n/a` |
| CH_ID | ENUM | `[1, 255]` | 00 | `n/a` |
| ERROR_CODE | ENUM | `[1, 2, 3, 4, 5, 7, 8, 9, 10]` | 00,06,0B-FF | `n/a` |
| CAP_BITS | BITMASK | `[1, 4, 32]` | bit1,3-4,6-7 | `37` |

## Error applicability

| code | name | Wire | class | applicable request FUNC |
|---:|---|---:|---|---|
| `0x00` | ERR_RESERVED | False | INTERNAL | none |
| `0x01` | ERR_ILLEGAL_FUNC | True | UNICAST/UID_DIRECTED | * |
| `0x02` | ERR_PAYLOAD_LEN | True | UNICAST/UID_DIRECTED | READ_SAMPLE, READ_INFO, WRITE_CFG, DIAG, ASSIGN, READ_ADDR, SLEEP, NET_MGMT, READDRESS |
| `0x03` | ERR_INVALID_VALUE | True | UNICAST/UID_DIRECTED | ASSIGN, NET_MGMT, READDRESS |
| `0x04` | ERR_SENSOR | True | UNICAST | READ_SAMPLE, READ_INFO |
| `0x05` | ERR_BUSY | True | UNICAST/UID_DIRECTED | READ_SAMPLE, READ_INFO, WRITE_CFG, DIAG, ASSIGN, READ_ADDR, SLEEP, NET_MGMT, READDRESS |
| `0x06` | ERR_INTERNAL_ONLY | False | INTERNAL | none |
| `0x07` | ERR_PROTO_FEATURE | True | UNICAST/UID_DIRECTED | READ_INFO, EVENT_RESERVED, NET_MGMT |
| `0x08` | ERR_NET_ID | True | UNICAST/UID_DIRECTED | ASSIGN, NET_MGMT, READDRESS |
| `0x09` | ERR_TXN_CONFLICT | True | UNICAST/UID_DIRECTED | WRITE_CFG, ASSIGN, SLEEP, NET_MGMT, READDRESS |
| `0x0A` | ERR_PERSIST_FAILED | True | UID_DIRECTED | ASSIGN, READDRESS |

## Generic idempotency

| FUNC | cache key | identity | duplicate | conflict |
|---|---|---|---|---|
| WRITE_CFG | func+seq | full_data | replay | TXN_CONFLICT |
| SLEEP | func+seq | full_data | replay_no_rearm | TXN_CONFLICT |
| ASSIGN | func+seq | full_data | replay | TXN_CONFLICT |
| READDRESS | func+seq | full_data | replay | TXN_CONFLICT |

## Canonical structured policies

### NET and participant rules

```json
{
  "none_assign_readdress": "unicast_invalid_value_no_mutation",
  "none_query": "unicast_invalid_value",
  "none_broadcast": "silent_count",
  "recover_participant": "all_awake_non_retired_regardless_current_net"
}
```

### Baud TID FSM

```json
{
  "owner": "baud_fsm",
  "generic_idempotency": false,
  "PREPARE": {
    "same_tid_same_baud": "refresh",
    "same_tid_diff_baud": "silent_count",
    "different_tid": "supersede"
  },
  "QUERY": {
    "no_active": "reject_no_prepare",
    "match": "ready_or_contextual_reject",
    "mismatch": "txn_conflict"
  },
  "COMMIT": {
    "match": "persist_pending_effect_once",
    "duplicate": "silent_noop",
    "conflict": "silent_count"
  },
  "switch_after_must": 200,
  "completed_retention": {
    "duration_ms": 300,
    "derivation": "ceil above SWITCH_AFTER + 2*(worst 23-byte COMMIT TX at 9600,-2% + COPY_GAP) + T_IFG + T_JITTER",
    "start": "first accepted COMMIT request CRC stop bit",
    "within": "exact seq+tid+baud+switch duplicate silent noop; conflicting copy silent count",
    "expiry": "age >= duration clears completed identity before classification",
    "seq_wrap": "expired history cannot classify a later transaction"
  }
}
```

### Timing reference points

```json
{
  "RESP_TIMEOUT": {
    "start": "request_crc_stop_bit_on_bus",
    "end": "response_crc_stop_bit_received"
  },
  "T_PROCESS_MAX": {
    "start": "request_crc_stop_bit_on_bus",
    "end": "response_first_start_bit"
  },
  "T_TURNAROUND_MAX": {
    "start": "response_ready",
    "end": "response_first_start_bit"
  },
  "RETRY_WINDOW_MAX": {
    "start": "first_request_start_bit",
    "end": "final_response_deadline"
  }
}
```

### Persistence transaction policy

```json
{
  "record_format": "version+generation+payload+crc+valid_marker",
  "atomicity": "single atomic record or detectable A/B commit; corrupt/torn is absent",
  "transaction_order": [
    "validate",
    "stage",
    "persist",
    "effect",
    "success"
  ],
  "failure_attribution": "a visible request persistence failure requires that request own the failed durable operation; unrelated coalesced timer durability must not fabricate a request completion",
  "commission": {
    "fields": [
      "addr",
      "net_id",
      "commission"
    ],
    "safe_absent": "UNASSIGNED"
  },
  "config": {
    "order": [
      "validate",
      "persist",
      "apply",
      "success"
    ],
    "failure": "old_ram_and_storage"
  },
  "baud": {
    "fields": [
      "current_bus_baud",
      "bus_baud_known"
    ],
    "persist_before_effect": true,
    "failure": "no SET_BAUD; enter registry-ordered recovery",
    "recovery_owner": "Master baud and persistence state machine",
    "recovery_sequence": [
      "registry scan",
      "known-node READ_ADDR proof",
      "canonical recovered baud",
      "persist",
      "known idle"
    ],
    "recovery_probe_order": "candidate-major; every eligible known node before next generated baud",
    "recovery_eligibility": "known normal address and own network and no sleep lease; stale online flag ignored",
    "recovery_proof": "contract-valid normal READ_ADDR or contract-valid Wire exception at candidate baud",
    "recovery_no_proof": "remain unknown; report failed pass and restart first generated candidate",
    "recovery_success": "runtime baud known before persistence request; complete only after persistence success",
    "recovery_persist_failure": "rollback runtime known state and restart registry scan"
  },
  "schema": {
    "fields": [
      "schema_rev",
      "descriptor_signature"
    ],
    "owner": "NodeEngine",
    "identity": "Murmur3 canonical encoded descriptor TLVs",
    "unchanged": "retain schema_rev",
    "changed": "increment schema_rev exactly once",
    "order": [
      "normalize",
      "signature",
      "compare",
      "stage",
      "persist",
      "publish"
    ],
    "boot": "compare application descriptor identity with durable signature",
    "failure": "retain prior rev/signature/canonical descriptor"
  },
  "quarantine": {
    "fields": [
      "reason"
    ],
    "durable": [
      "QUARANTINED_REG",
      "QUARANTINED_SERVICE"
    ],
    "clear_is_durable": true,
    "backoff_reconstruction": "attempt and deadline are transient; reboot restarts conservative first backoff"
  },
  "sleep_lease": {
    "fields": [
      "uid",
      "lease_state",
      "requested_duration"
    ],
    "owner": "Master transaction and lease state machine",
    "states": [
      "NONE",
      "UNCERTAIN",
      "CONFIRMED",
      "RECOVERY_VERIFY"
    ],
    "first_tx_epoch": "immutable per logical SLEEP transaction",
    "retry": "same SEQ; does not move epoch or expected wake",
    "forbidden_runtime_projection": "monotonic expected_wake timestamp",
    "no_stable_clock_boot": "RECOVERY_VERIFY with no live deadline",
    "recovery_verify": {
      "owner": "Master work scheduler and lease timer engine",
      "initial_probe": "arm generated base delay from the current boot monotonic time",
      "failure": "retain lease without OFFLINE; update transient result/attempt; arm bounded backoff",
      "reboot": "re-arm a full generated backoff from new boot monotonic time",
      "fairness": "round-robin work classes prevent sleep verification from starving eligible normal work"
    }
  }
}
```

### Boot reconstruction

```json
{
  "precedence": [
    "RETIRED",
    "PENDING",
    "QUARANTINED",
    "COMMITTED",
    "NONE"
  ],
  "RETIRED": {
    "administrative": "RETIRED",
    "runtime": "derive durable identity then enforce administrative lock",
    "power": "AWAKE"
  },
  "PENDING": {
    "commission": "PENDING",
    "network": "ATTACHED",
    "service": "NORMAL",
    "power": "AWAKE"
  },
  "QUARANTINED_REG": {
    "commission": "NONE",
    "network": "NO_NET",
    "service": "QUARANTINED_REG",
    "power": "AWAKE"
  },
  "QUARANTINED_SERVICE": {
    "commission": "COMMITTED",
    "network": "PROBATION",
    "service": "QUARANTINED_SERVICE",
    "power": "AWAKE"
  },
  "COMMITTED": {
    "commission": "COMMITTED",
    "network": "PROBATION",
    "service": "NORMAL",
    "power": "AWAKE"
  },
  "NONE": {
    "commission": "NONE",
    "network": "NO_NET",
    "service": "NORMAL",
    "power": "AWAKE"
  },
  "transient_reset": [
    "generic_cache",
    "baud_transaction",
    "sleep_armed",
    "registration_timer",
    "migration_candidate"
  ],
  "schema": "restore durable schema_rev and descriptor_signature"
}
```

### Timer ownership

```json
{
  "DUP_CACHE_TTL": [
    "recent_transaction_engine",
    "completion_time",
    "expire cache"
  ],
  "REG_TIMEOUT": [
    "registration_engine",
    "REGISTER report TX-complete",
    "QUARANTINED_REG with backoff"
  ],
  "ASSIGN_CONFIRM_TIMEOUT": [
    "commission_engine",
    "PENDING entry",
    "atomic identity clear"
  ],
  "PREPARE_TIMEOUT": [
    "baud_fsm",
    "accepted PREPARE",
    "clear RAM TID"
  ],
  "SLEEP_ARMED_WINDOW": [
    "sleep_engine",
    "first accepted SLEEP",
    "ENTER_SLEEP(duration)"
  ],
  "SLEEP_WAKE": [
    "platform timer plus node wake transition",
    "deep-sleep entry",
    "AWAKE plus ATTACHED/PROBATION"
  ],
  "QUARANTINE_BACKOFF": [
    "service engine",
    "quarantine entry",
    "scheduled retry event"
  ],
  "GRACE_PERIOD": [
    "migration/sleep lease engines",
    "candidate/expected wake",
    "conservative recovery"
  ],
  "BAUD_SWITCH": [
    "baud_fsm",
    "valid durable COMMIT",
    "SET_BAUD"
  ],
  "COMMIT_REPLAY_WINDOW": [
    "baud_fsm",
    "completed COMMIT",
    "300ms from accepted COMMIT RX end; duplicate noop/conflict count; age>=300ms expires"
  ]
}
```

### Runtime time model

```json
{
  "node_clock": {
    "platform_tick_bits": 32,
    "arithmetic": "uint32 modular elapsed = now - start",
    "timer_record": [
      "active",
      "start_ms",
      "interval_ms"
    ],
    "inactive": "explicit active=false; numeric epoch zero is valid",
    "max_interval_ms": 2147483647,
    "input_order": "raw tick observations are monotonic modulo 2^32; stale/out-of-order timestamps are forbidden",
    "service_horizon": "while any timer is active, Core observes a tick/request/delivery before one complete 2^32 ms hardware epoch elapses",
    "adapter_source": "one Node adapter projection: lower 32 bits of esp_timer_get_time microseconds divided by 1000"
  },
  "node_observation_order": {
    "owner": "Node Core time-advance stage",
    "request_boundary": "all timers with age >= interval advance before request classification at the same observed timestamp",
    "state_only_merge": "elapsed state-only transitions join the request transaction plan before publish",
    "external_effect_drain": "one due external effect per advance; later due effects remain active for the next same-time advance",
    "external_effect_order": [
      "SET_BAUD",
      "ENTER_SLEEP"
    ],
    "effect_publication": "Node Core post-state predicate is the only authority that distinguishes committed due platform effects from future or still-uncommitted effect metadata",
    "adapter_contract": "request path repeats NodeEngine execute at one request timestamp only when the Core effect-publication predicate says the timer effect is due; future effect metadata is not applied early; SET_BAUD applies then retries, ENTER_SLEEP terminates runtime; periodic no-request path drains NodeEngine advance",
    "direct_execute_fallback": "execute advances time first and returns the first due external effect without classifying the request"
  },
  "node_timers": [
    {
      "name": "registration",
      "owner": "registration_engine",
      "field": "node.registration_timer",
      "interval_source": "timing_ms.REG_TIMEOUT",
      "start_event": "delivered REGISTER report",
      "restart": "new registration lifecycle only",
      "cancel": "registration expiry or commission clear",
      "expiry": "enter QUARANTINED_REG",
      "durable_relation": "quarantine reason is durable; timer epoch is not",
      "reboot": "REGISTERING is transient and timer is not restored",
      "coverage": [
        "R5-C5",
        "timer-epoch-matrix"
      ]
    },
    {
      "name": "assignment_confirmation",
      "owner": "commission_engine",
      "field": "node.assign_confirm_timer",
      "interval_source": "timing_ms.ASSIGN_CONFIRM_TIMEOUT",
      "start_event": "atomic PENDING entry",
      "restart": "accepted replacement ASSIGN/READDRESS transaction",
      "cancel": "candidate confirmation, timeout, or commission clear",
      "expiry": "atomic identity clear",
      "durable_relation": "PENDING identity is durable; timer epoch is not",
      "reboot": "PENDING restore starts a full interval at boot epoch zero",
      "coverage": [
        "R5-C3",
        "R5-C14",
        "timer-epoch-matrix"
      ]
    },
    {
      "name": "prepare",
      "owner": "baud_fsm",
      "field": "node.baud_transaction.prepare_timer",
      "interval_source": "timing_ms.PREPARE_TIMEOUT",
      "start_event": "accepted PREPARE",
      "restart": "same TID/same baud refresh or different TID supersede",
      "cancel": "valid COMMIT, expiry, or reboot",
      "expiry": "clear RAM PREPARE identity and return IDLE",
      "durable_relation": "PREPARE and timer are transient",
      "reboot": "not restored",
      "coverage": [
        "R5-C4",
        "timer-epoch-matrix"
      ]
    },
    {
      "name": "sleep_arm",
      "owner": "sleep_engine",
      "field": "node.sleep_timer",
      "interval_source": "timing_ms.SLEEP_ARMED_WINDOW",
      "start_event": "first accepted nonzero SLEEP",
      "restart": "duplicate replay never re-arms",
      "cancel": "arm expiry, wake, or commission clear",
      "expiry": "emit ENTER_SLEEP with requested duration",
      "durable_relation": "Node arm timer is transient; Master lease is durable-relative",
      "reboot": "not restored as an absolute epoch",
      "coverage": [
        "R5-C1",
        "R5-C2",
        "R5-C14",
        "timer-epoch-matrix"
      ]
    },
    {
      "name": "quarantine",
      "owner": "service_engine",
      "field": "node.quarantine_timer",
      "interval_source": "quarantine_backoff_ms.*",
      "start_event": "quarantine entry or failed recovery delivery",
      "restart": "bounded generated backoff progression",
      "cancel": "delivered registration/recovery, expiry-to-retry, or commission clear",
      "expiry": "REG clears quarantine; SERVICE marks retry due",
      "durable_relation": "quarantine reason is durable; attempt/epoch are transient",
      "reboot": "durable reason restarts conservative first backoff at epoch zero",
      "coverage": [
        "R5-C6",
        "timer-epoch-matrix"
      ]
    },
    {
      "name": "baud_switch",
      "owner": "baud_fsm",
      "field": "node.baud_transaction.switch_timer",
      "interval_source": "timing_ms.SWITCH_AFTER",
      "start_event": "valid COMMIT transaction plan",
      "restart": "duplicate COMMIT does not restart",
      "cancel": "switch effect or transaction rollback",
      "expiry": "emit SET_BAUD and return baud FSM to IDLE",
      "durable_relation": "baud persists before switch effect becomes final",
      "reboot": "current durable baud restores; switch epoch is not restored",
      "coverage": [
        "R5-C13",
        "timer-epoch-matrix"
      ]
    },
    {
      "name": "duplicate_cache",
      "owner": "recent_transaction_engine",
      "field": "node.effects.retention_timer",
      "interval_source": "timing_ms.DUP_CACHE_TTL",
      "start_event": "request completion after required durable success",
      "restart": "new completed idempotent request replaces cache",
      "cancel": "TTL expiry, boot, or commission clear",
      "expiry": "drop exact request/response replay identity",
      "durable_relation": "cache and timer are transient",
      "reboot": "not restored",
      "coverage": [
        "R5-C11",
        "timer-epoch-matrix"
      ]
    },
    {
      "name": "completed_commit",
      "owner": "baud_fsm",
      "field": "node.baud_transaction.completed_timer",
      "interval_source": "timing_ms.COMMIT_REPLAY_WINDOW",
      "start_event": "first accepted COMMIT request CRC stop",
      "restart": "duplicate/conflict does not refresh",
      "cancel": "retention expiry or reboot",
      "expiry": "drop completed COMMIT identity before classification",
      "durable_relation": "completed identity is transient; baud is durable",
      "reboot": "not restored",
      "coverage": [
        "R5-C12",
        "timer-epoch-matrix"
      ]
    }
  ],
  "node_elapsed_anchors": [
    {
      "name": "own_network_silence",
      "owner": "migration_engine",
      "interval_source": "timing_ms.GRACE_PERIOD",
      "representation": "timestamp anchor; never an inactive sentinel"
    }
  ],
  "scheduled_response": {
    "normative_owner": "64-bit physical microsecond slot scheduler",
    "millisecond_due": "wrapped observable projection only"
  },
  "master_clock": {
    "runtime_bits": 64,
    "adapter_source": "esp_timer_get_time microseconds projected to milliseconds",
    "ordering": "ordinary unsigned monotonic ordering",
    "reboot": "epoch resets; restore relative lease state as RECOVERY_VERIFY",
    "persistence": "never serialize runtime absolute epoch",
    "sleep_range": "SLEEP_ARMED_WINDOW + any nonzero uint32 sleep_ms + GRACE_PERIOD"
  }
}
```

### Master orchestration

```json
{
  "retry": {
    "max": "transaction.retry_max",
    "same_seq": true,
    "terminal": [
      "valid_normal",
      "valid_exception",
      "final_timeout"
    ]
  },
  "collect": {
    "start": "physical TX complete",
    "tx_silent": true,
    "accept_until": "full collect deadline"
  },
  "baud": {
    "sequence": [
      "PREPARE broadcast",
      "QUERY each eligible online node",
      "all READY",
      "3 same-SEQ COMMIT copies",
      "copy gap",
      "MASTER_SILENCE",
      "persist current bus baud",
      "local switch",
      "READ_ADDR verification"
    ],
    "durable_owner": "Master baud and persistence state machine",
    "boot_known": "restore current bus baud before ordinary traffic",
    "boot_unknown": "candidate-major registry scan; every eligible known node gets READ_ADDR proof opportunity; proofless pass restarts unknown",
    "discover_dependency": false
  },
  "work_scheduler": {
    "owner": "Master Core",
    "classes": [
      "SLEEP_RECOVERY_VERIFY",
      "ASSIGN",
      "DISCOVER",
      "READ_SAMPLE"
    ],
    "selection": "round-robin across eligible work classes and nodes",
    "fairness": "after a selected class finishes, the next scan begins at the following class",
    "assign_fairness": "transient node cursor advances when ASSIGN is issued, including terminal timeout",
    "poll_interval_ms": 1000,
    "sleep_verify_backoff_base_ms": 1000,
    "sleep_verify_backoff_max_ms": 60000,
    "firmware_role": "supply monotonic time/random seed and execute returned request"
  },
  "sleep_lease": [
    "uncertain at SLEEP TX-complete",
    "confirmed on ACK",
    "persist relative state/duration, never raw monotonic deadline",
    "early wake clears lease",
    "no-stable-clock reboot enters lease verification"
  ],
  "descriptor_trust": [
    "validate generated registry",
    "cache schema_rev",
    "cache descriptor_signature"
  ],
  "offline": {
    "threshold": "transaction.offline_threshold",
    "sleep_lease_exempt": true
  }
}
```

### Codec layouts

```json
{
  "sample": "STYPE1+COUNT1+SCHEMA_REV2+(CH_ID1+VALUE_LEN1+VALUE)*COUNT+STATUS1",
  "info_base": "UID6+FW2+STYPE1+ADDR1+CAP1+PROTO_VER2+SCHEMA_REV2",
  "info_desc": "STYPE1+COUNT1+SCHEMA_REV2+TLV*",
  "report_register": "TYPE1+UID6",
  "report_reconcile": "TYPE1+UID6+ADDR1",
  "report_recover": "TYPE1+UID6+ADDR1+NET_ID8",
  "query_response": "STATUS1+REASON1"
}
```

## Semantic owners

| rule family | owner |
|---|---|
| request envelope | `request_contracts` |
| response validation | `response_contracts` |
| state qualification | `state_policy` |
| generic duplicate handling | `idempotency` |
| 0x1C transaction | `baud_tid_policy` |
| persistence | `persistence` |
| delayed effects | `effects` |
| boot reconstruction | `boot_policy` |
| timer lifecycle | `timer_policy` |
| runtime clock and timer representation | `runtime_time_model` |
| Master orchestration | `master_policy` |
