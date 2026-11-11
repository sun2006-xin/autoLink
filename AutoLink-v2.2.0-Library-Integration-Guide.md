# AutoLink v2.2.0 Library Integration Guide

## Dependency and boundary

Pin the protocol source to the [v2.2.0 tag](https://github.com/sun2006-xin/autolink/tree/v2.2.0). The integration adapter owns platform callbacks.

## 中文快速上手

1. 下载 v2.2.0 源码。使用 Arduino 时，将 `libraries/AutoLinkCore` 和 `libraries/GSProto` 分别安装到 Arduino 用户库目录。使用其他 C 工具链时，把两个库的 `src` 加入 include 路径，并编译其中的 C 文件，包括生成的语义源。
2. 接入目标板的 UART 与 RS-485 收发器，并为适配层提供单调时钟和持久化回调。按目标板配置方向控制、总线接线和默认波特率。
3. **Node** 侧初始化、恢复存储、绑定应用回调并同步传感器描述。收到完整合法帧后调用 `al_node_engine_execute()`，按返回的 action 发送应答或执行适配层操作，并按 API 约定回报发送结果。
4. **Master** 侧初始化网络身份、恢复节点表和波特率，驱动时钟与收发处理。发送前调用 `al_master_tx_authorized()` 核对具体帧，发送完成后通过 `al_master_on_tx_complete_timestamp()` 报告时间。完整调用顺序见 [公开 API](AutoLink-v2.2.0-Public-Integration-API.md)。
5. 在电脑上运行 `python tools/run_c_tests.py` 检查核心。需要一套可编译的 ESP32 应用时，使用 [GrainSilo 的 firmware 目录](https://github.com/sun2006-xin/grainsilo/tree/main/firmware)及其随附的库。该项目附带用于采样与监测的集成补丁。

## API classifications

| Classification | Meaning | GrainSilo use |
|---|---|---|
| `PUBLIC / SUPPORTED` | Explicitly documented, allowlisted compatibility surface | Allowed |
| `INTERNAL / PRIVATE` | Core layout, transition owner or implementation detail | Forbidden |
| `TEST-ONLY` | Verification fixture or test runner surface | Forbidden in production |
| `EXPERIMENTAL` | Not covered by the v2.2.0 compatibility promise | Forbidden unless a new review authorizes it |

The canonical symbol set is in the [machine-readable allowlist](GrainSilo-AutoLink-v2.2.0-API-Allowlist.json). The [public API reference](AutoLink-v2.2.0-Public-Integration-API.md) describes the call order and integration contract.

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

The GrainSilo application uses Arduino ESP32-C3 as Node and ESP32-S3 as Master. Build options and adapter sources are documented in the [GrainSilo repository](https://github.com/sun2006-xin/grainsilo/tree/main/firmware). Measure physical timing and bus behavior on the target hardware.
