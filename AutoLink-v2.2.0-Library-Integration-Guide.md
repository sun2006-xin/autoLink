# AutoLink v2.2.0 Library Integration Guide

## Dependency and boundary

Resolve AutoLink from the pinned `v2.2.0` tag using `dependency/AutoLink-v2.2.0.lock`. Do not vendor the full AutoLink development tree into GrainSilo and do not track AutoLink `main`/HEAD. The integration adapter owns platform callbacks only.

## API classifications

| Classification | Meaning | GrainSilo use |
|---|---|---|
| `PUBLIC / SUPPORTED` | Explicitly documented, allowlisted compatibility surface | Allowed |
| `INTERNAL / PRIVATE` | Core layout, transition owner or implementation detail | Forbidden |
| `TEST-ONLY` | Verification fixture or test runner surface | Forbidden in production |
| `EXPERIMENTAL` | Not covered by the v2.2.0 compatibility promise | Forbidden unless a new review authorizes it |

The canonical symbol set is in `GrainSilo-AutoLink-v2.2.0-API-Allowlist.json` (52 symbols). It is the machine source of truth; the Markdown file is the human index.

## Supported groups

| Group/owner | Purpose | Inputs/outputs | Error and persistence contract | GrainSilo use |
|---|---|---|---|---|
| Node Engine | initialize/restore, execute requests, advance time, publish due effects, report delivery | initialized static engine, validated request/time/application port; Core action/effect result | false means operation not accepted; journal/rollback remain Core-owned | Node adapter |
| Master runtime | tick/events, choose authorized transmission, report TX/RX observations, baud feedback | initialized Master, monotonic timestamp, Core event/frame | `INVALID`/`NONE`/`READY` retain their documented meaning; adapter transmits only `READY` | Master adapter |
| Master durable registry | export/restore records and baud/network durable facts | initialized Master and bounded record buffers | explicit result/capacity errors; no boot-local deadlines are persisted | Master storage adapter |
| Journal port | two-slot atomic load/commit | bounded storage callback port | success is explicit; torn records are rejected by journal owner | platform storage |
| Transport timestamp | carry physical RX/TX observations | monotonic timestamp, event kind, verification flag | event mismatch is rejected by consumer; no protocol decision in adapter | RS-485 glue |

For every symbol, the JSON allowlist records owner/module, purpose, input/output contract, lifecycle, error behavior, persistence and time assumptions. Do not infer a new API from a visible header.

## Required call order

Initialize → optionally restore → install application/storage ports → execute/tick/advance → transmit only Core-authorized output → report the exact physical delivery/timestamp result. Never read private members, write Core state, duplicate a timer, or reconstruct a Wire frame in GrainSilo code.

## Build assumptions

The adapter is compiled for ESP32-C3 Node and ESP32-S3 Master with Arduino libraries and the pinned AutoLink library path. Hardware compile success is not field validation. See the project plan for the staged validation sequence.
