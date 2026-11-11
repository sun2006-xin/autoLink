# AutoLink V2.2 Conformance

The public checkout provides the host C suites through `python tools/run_c_tests.py`. The full structural gate and differential harness described below document the original freeze procedure. Their internal runners are outside this checkout.

Status: **NON-NORMATIVE / NO-FREEZE**. No release or tag is authorized by a
passing self-check.

| Gate | Requirement | Primary executable evidence |
| --- | --- | --- |
| S1 Wire Lock | exact candidate Wire; no new FUNC/field/enum/bit/error | generated `wire_lock.json`, reserved-token negatives |
| S2 Full Semantic SSOT | one declarative owner; generated Python/C/docs | generator freshness, vector/doc exact-diff, semantic-origin dependency checks |
| S3 Production Node + Master Core | one Node pipeline and complete portable Master orchestration | 17 completion counterexamples, all nine suites for every discovered strict host compiler and advertised sanitizer mode, R5-C1..C15, R7-C1..C4, 50-scenario/78-transition NodeEngine candidate matrix, Round 2 adversarial histories, adapter structural scan |
| S4 Transactional Persistence + Timer Engine | detectable A/B records; persist-before-finalize; explicit timer records, request-boundary time advance and 64-bit Master epoch | torn/corrupt recovery, combined expiry/request rollback, candidate commission+sleep rollback/retry, single-commit schema composition, 990 named/880 unique epoch inputs, ordered effect drain, long-SLEEP reboot |
| S5 Full V2.2 Codec | one frame/sample layout and separate parser/Wire errors | golden vectors, C codec round trips and malformed negatives |
| S6 Generated Differential Conformance | Node and Master Python↔C plus generated properties | 57 Node cases/94 transitions including R5-C1..C6 6/28 and five Round 7 observation histories, 5 schema transitions, 50 Master full-state transitions, 16/317 scheduler, 13/89 long-SLEEP, 440/476 deterministic structural Node, 512 random Node properties |
| S7 Portable Release Gates | complete environment-derived compiler matrix, complete privacy-safe package | R6-C1..C9, R7 pre-fix capture, compiler-matrix ownership audit, Arduino builds, manifest/hash/provenance extracted-tree verification |

Run from the repository root:

```text
python tools/structural_gates.py
```

Host C discovery supports MSVC, GCC and Clang. Warnings-as-errors are mandatory;
ASan/UBSan run when the chosen host compiler supports them. The portable
invariant is exact required/executed matrix equality over discovered capabilities,
not a fixed compiler or build count. No developer-machine absolute compiler path
is a requirement.

Platform compilation is additional source/toolchain evidence:

```text
arduino-cli compile --fqbn esp32:esp32:esp32c3 --libraries libraries node
arduino-cli compile --fqbn esp32:esp32:esp32s3 --libraries libraries master
```

Neither host nor Arduino compilation proves physical RS485 timing, collision
behavior, flash behavior during power interruption, deep sleep, sensor accuracy,
or network availability. Those belong to the next hardware-conformance phase.
## Root-Cause Closure conformance histories

Required named histories are implemented in `tests/test_sc2_root_cause_closure.c`, `tests/test_sc2_round2_closure.c`, `tests/test_sc2_round4_closure.c`, `tests/test_sc2_round5_closure.c`, `tests/test_sc2_timer_observation_closure.c`, `tests/autolink_schema_diff_runner.c`, `tests/autolink_master_diff_runner.c`, `tests/autolink_timer_matrix_runner.c`, and `tests/autolink_node_candidate_runner.c`. The timer-observation suite proves exact-boundary advance-before-classification, ordered multi-effect drain, future-vs-due effect publication and persistence rollback; the five corresponding histories are generated from SSOT and compared by the normal Node Python/C runner. `tools/candidate_confirmation_conformance.py` derives every invariant-valid node profile and legal request/critical payload class from generated contracts; `tools/node_candidate_conformance.py` derives every candidate-confirming operation plus generic idempotency intersections and checks persistence success/failure, reboot, duplicate/conflict/TTL, and effect retry. `tools/timer_epoch_matrix.py` derives the complete Node timer interval/epoch/tick cross-product from the generated semantic contract rather than a handwritten timer list. Observable comparison includes timer active/start/interval state, commission/service/power state, response/action/effect/cache, the Core due-effect predicate, journal writes, scheduled microsecond start/accuracy, schema revision/signature, sleep probe eligibility/deadline/attempt/in-flight/result, all Master work cursors, durable bus baud/known/recovery-persistence state, and persistence outcome. The Master runner also generates deterministic multi-node scheduler cases and full-range long-SLEEP histories. Physical ±500 µs timing remains UNVERIFIED until board-level instrumentation supplies bounded RX CRC-stop, TX VER-start and TX CRC-stop evidence.
