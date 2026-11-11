# AutoLink v2.2.0 Protocol Overview

## Frozen scope

AutoLink is the reusable protocol/core layer for GrainSilo. It runs a Master/Node model over RS-485. The accepted semantic baseline is commit `76db7e55b42d80f48b9d01c48be5bf72f896e929`, tag `v2.2.0`; Wire and protocol semantics are frozen. This document explains the frozen behavior for integrators; it does not redefine it.

## Roles and transport

The Master schedules discovery, commissioning, addressed transactions, retry/duplicate handling, collection and durable registry work. A Node owns its protocol lifecycle, request validation, timers, persistence transitions and platform effects. RS-485 is the physical half-duplex transport; the adapter reports observed timestamps and delivery outcomes to AutoLink rather than deciding protocol state.

## Commissioning and addressing

Unassigned Nodes answer the Master’s discovery/registration flow, receive a logical address, persist the durable identity, and confirm it. On reboot, the Node reconstructs durable identity through its storage port. The Master restores its registry and verifies identity before treating a peer as available. Address, network and commissioning facts remain Core-owned transaction state.

## Transaction identity and retry

Requests carry the frozen transaction identity and request-specific duplicate/idempotency policy. Retries are evaluated by the Core against the recorded fingerprint and lifecycle state. Adapters must not invent a second duplicate cache, bypass confirmation, or turn a late response into a new transaction.

## Persistence, reboot, baud and SLEEP

AutoLink owns durable plans, journal generation/slot semantics, rollback and reboot reconstruction. The platform supplies storage callbacks and reports the result. Baud changes are proof → canonical state → durable feedback → recovery/reboot behavior. SLEEP is an explicit Core effect with relative lease/deadline handling; the platform performs the primitive only after the Core marks the effect due.

## Timing

Node elapsed checks are wrap-safe in the accepted time domain and never use zero as an activity sentinel. Master orchestration uses the accepted wide monotonic epoch and transport observations. GrainSilo passes observations; it does not calculate protocol expiry or slot eligibility.

## Public integration boundary

Only symbols classified `PUBLIC / SUPPORTED` in the canonical allowlist are stable for GrainSilo. `al_node_engine_t` and `al_master_t` are pass-by-address/static-allocation API objects; their members remain private. See [AutoLink-v2.2.0-Library-Integration-Guide.md](AutoLink-v2.2.0-Library-Integration-Guide.md) and [AutoLink-GrainSilo-Ownership-Boundary.md](AutoLink-GrainSilo-Ownership-Boundary.md).

## Hardware boundary and future versions

The software Freeze does not prove RS-485 collision behavior, termination/bias, cable length, power integrity, brownout, real NVS power-cut endurance, deep sleep/wake, sensor failure recovery, Wi-Fi recovery or long-duration stability. These remain GrainSilo-stage evidence. A future AutoLink version requires an explicit compatibility review; GrainSilo must not follow repository `main`/HEAD implicitly.
