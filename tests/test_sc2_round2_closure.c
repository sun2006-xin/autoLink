#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <autolink_master.h>
#include "generated/autolink_semantics.h"

static const uint64_t NET = UINT64_C(0x0102030405060708);

static void add_node(al_master_t *master, uint8_t index, uint8_t addr,
                     uint8_t uid_seed)
{
    uint8_t i;
    assert(index < AL_MASTER_NODE_CAPACITY);
    if (master->node_count <= index) master->node_count = (uint8_t)(index + 1u);
    memset(&master->nodes[index], 0, sizeof(master->nodes[index]));
    for (i = 0u; i < GS_UID_LEN; ++i)
        master->nodes[index].uid[i] = (uint8_t)(uid_seed + i);
    master->nodes[index].addr = addr;
    master->nodes[index].network_id = NET;
    master->nodes[index].online = true;
}

static al_master_event_t finish_timeout(al_master_t *master, al_master_time_ms_t *now_ms)
{
    al_master_event_t event;
    uint8_t retry;
    for (retry = 0u; retry < GS_TRANSACTION_RETRY_MAX; ++retry) {
        *now_ms = master->transaction.deadline_ms;
        event = al_master_tick(master, *now_ms);
        assert(event.kind == AL_MASTER_EVENT_RETRANSMIT);
        assert(al_master_on_tx_complete(master, *now_ms));
    }
    *now_ms = master->transaction.deadline_ms;
    return al_master_tick(master, *now_ms);
}

static al_master_event_t try_recovery_baud(al_master_t *master,
                                           uint32_t expected_baud,
                                           bool respond,
                                           al_master_time_ms_t *now_ms)
{
    al_master_event_t event = al_master_tick(master, *now_ms);
    gs_frame_t request;
    gs_frame_t response;
    assert(event.kind == AL_MASTER_EVENT_BAUD_RECOVERY);
    assert(event.effect_value == expected_baud);
    al_master_baud_recovery_applied(master);
    assert(al_master_baud_next(master, *now_ms, &request));
    assert(request.func == GS_FUNC_READ_ADDR);
    assert(al_master_on_tx_complete(master, *now_ms));
    if (!respond) {
        event = finish_timeout(master, now_ms);
        assert(event.kind == AL_MASTER_EVENT_TIMEOUT);
        assert(!al_master_baud_next(master, *now_ms, &request));
        return al_master_tick(master, *now_ms);
    }
    memset(&response, 0, sizeof(response));
    response.addr = request.addr;
    response.seq = request.seq;
    response.func = GS_FUNC_READ_ADDR;
    response.len = 1u;
    response.data[0] = request.addr;
    ++*now_ms;
    return al_master_receive_at(master, &response, *now_ms);
}

static void test_recovered_baud_is_persisted_and_restored(void)
{
    const uint32_t targets[] = {57600u, 115200u};
    size_t target_index;
    for (target_index = 0u;
         target_index < sizeof(targets) / sizeof(targets[0]); ++target_index) {
        al_master_t master;
        al_master_t rebooted;
        al_master_t before_persist_reboot;
        al_master_event_t event = {0};
        al_master_time_ms_t now_ms = 0u;
        size_t cursor;
        uint32_t durable_baud;

        al_master_init(&master, NET);
        add_node(&master, 0u, 5u, 1u);
        al_master_mark_bus_baud_unknown(&master);
        for (cursor = 0u; cursor < al_supported_baud_count(); ++cursor) {
            uint32_t candidate = al_supported_baud_at(cursor);
            event = try_recovery_baud(&master, candidate,
                                      candidate == targets[target_index],
                                      &now_ms);
            if (candidate == targets[target_index]) break;
            assert(event.kind == AL_MASTER_EVENT_BAUD_RECOVERY ||
                   event.kind == AL_MASTER_EVENT_BAUD_ABORTED);
        }
        assert(cursor < al_supported_baud_count());
        assert(event.kind == AL_MASTER_EVENT_PERSIST_REQUIRED);
        assert(event.persist_registry);
        assert(master.current_bus_baud == targets[target_index]);
        assert(master.bus_baud_known);
        assert(master.persist_kind == AL_MASTER_PERSIST_BAUD);

        /* A reboot before feedback still sees the old unknown durable fact. */
        al_master_init(&before_persist_reboot, NET);
        al_master_mark_bus_baud_unknown(&before_persist_reboot);
        assert(!before_persist_reboot.bus_baud_known);
        assert(before_persist_reboot.baud.state == AL_MASTER_BAUD_RECOVERY);

        assert(al_master_on_persist_result(&master, true));
        event = al_master_tick(&master, now_ms);
        assert(event.kind == AL_MASTER_EVENT_BAUD_COMPLETE);
        assert(master.baud.state == AL_MASTER_BAUD_IDLE);
        durable_baud = master.current_bus_baud;

        al_master_init(&rebooted, NET);
        assert(al_master_restore_bus_baud(&rebooted, durable_baud));
        assert(rebooted.current_bus_baud == targets[target_index]);
        assert(rebooted.bus_baud_known);
        assert(rebooted.baud.state == AL_MASTER_BAUD_IDLE);
        assert(al_master_tick(&rebooted, 0u).kind == AL_MASTER_EVENT_NONE);
    }
}

static void test_recovered_baud_persistence_failure_reenters_scan(void)
{
    al_master_t master;
    al_master_event_t event;
    al_master_time_ms_t now_ms = 0u;
    size_t target_index;
    al_master_init(&master, NET);
    add_node(&master, 0u, 5u, 1u);
    al_master_mark_bus_baud_unknown(&master);
    for (target_index = 0u; target_index < al_supported_baud_count();
         ++target_index)
        if (al_supported_baud_at(target_index) == 57600u) break;
    assert(target_index < al_supported_baud_count());
    master.baud.recovery_cursor = (uint8_t)target_index;
    event = try_recovery_baud(&master, 57600u, true, &now_ms);
    assert(event.kind == AL_MASTER_EVENT_PERSIST_REQUIRED);
    assert(al_master_on_persist_result(&master, false));
    assert(!master.bus_baud_known);
    assert(master.baud.state == AL_MASTER_BAUD_RECOVERY);
    event = al_master_tick(&master, now_ms);
    assert(event.kind == AL_MASTER_EVENT_BAUD_RECOVERY);
    assert(event.effect_value == al_supported_baud_at(0u));

    assert(!al_master_restore_bus_baud(&master, 38400u));
    assert(!master.bus_baud_known);
    assert(master.baud.state == AL_MASTER_BAUD_RECOVERY);
    assert(al_master_restore_bus_baud(&master, 115200u));
    assert(master.bus_baud_known && master.current_bus_baud == 115200u);
    assert(master.baud.state == AL_MASTER_BAUD_IDLE);
}

static void test_recovery_requires_proof_and_scans_each_known_node(void)
{
    al_master_t master;
    al_master_event_t event;
    gs_frame_t request;
    gs_frame_t response;
    al_master_time_ms_t now_ms = 0u;
    size_t candidate;

    al_master_init(&master, NET);
    add_node(&master, 0u, 5u, 1u);
    add_node(&master, 1u, 6u, 10u);
    al_master_mark_bus_baud_unknown(&master);
    event = al_master_tick(&master, now_ms);
    assert(event.kind == AL_MASTER_EVENT_BAUD_RECOVERY);
    al_master_baud_recovery_applied(&master);

    /* A timeout at node A does not skip the current candidate. */
    assert(al_master_baud_next(&master, now_ms, &request));
    assert(request.addr == 5u);
    assert(al_master_on_tx_complete(&master, now_ms));
    event = finish_timeout(&master, &now_ms);
    assert(event.kind == AL_MASTER_EVENT_TIMEOUT);
    assert(master.nodes[0].online && master.nodes[0].missed == 0u);
    assert(al_master_baud_next(&master, now_ms, &request));
    assert(request.addr == 6u);

    memset(&response, 0, sizeof(response));
    response.addr = request.addr;
    response.seq = request.seq;
    response.func = GS_FUNC_READ_ADDR;
    response.len = 1u;
    response.data[0] = request.addr;
    assert(al_master_on_tx_complete(&master, now_ms));
    event = al_master_receive_at(&master, &response, ++now_ms);
    assert(event.kind == AL_MASTER_EVENT_PERSIST_REQUIRED);
    assert(master.baud.state == AL_MASTER_BAUD_RECOVERY_PERSIST);

    /* With no proof at any candidate, the engine never claims COMPLETE and
     * restarts an unknown-baud pass instead of leaking into ordinary work. */
    al_master_init(&master, NET);
    add_node(&master, 0u, 5u, 1u);
    al_master_mark_bus_baud_unknown(&master);
    now_ms = 0u;
    for (candidate = 0u; candidate < al_supported_baud_count(); ++candidate) {
        event = al_master_tick(&master, now_ms);
        assert(event.kind == AL_MASTER_EVENT_BAUD_RECOVERY);
        assert(event.effect_value == al_supported_baud_at(candidate));
        al_master_baud_recovery_applied(&master);
        assert(al_master_baud_next(&master, now_ms, &request));
        assert(al_master_on_tx_complete(&master, now_ms));
        event = finish_timeout(&master, &now_ms);
        assert(event.kind == AL_MASTER_EVENT_TIMEOUT);
        assert(!al_master_baud_next(&master, now_ms, &request));
    }
    event = al_master_tick(&master, now_ms);
    assert(event.kind == AL_MASTER_EVENT_BAUD_ABORTED);
    assert(!master.bus_baud_known);
    assert(master.baud.state == AL_MASTER_BAUD_RECOVERY);
    event = al_master_tick(&master, now_ms);
    assert(event.kind == AL_MASTER_EVENT_BAUD_RECOVERY);
    assert(event.effect_value == al_supported_baud_at(0u));
}

static void test_recovery_accepts_valid_exception_proof_and_stale_offline_node(void)
{
    al_master_t master;
    al_master_event_t event;
    gs_frame_t request;
    gs_frame_t response;
    al_master_time_ms_t now_ms = 0u;

    al_master_init(&master, NET);
    add_node(&master, 0u, 5u, 1u);
    master.nodes[0].online = false; /* stale fact learned at the wrong baud */
    master.nodes[0].missed = GS_TRANSACTION_OFFLINE_THRESHOLD;
    al_master_mark_bus_baud_unknown(&master);
    event = al_master_tick(&master, now_ms);
    assert(event.kind == AL_MASTER_EVENT_BAUD_RECOVERY);
    al_master_baud_recovery_applied(&master);
    assert(al_master_baud_next(&master, now_ms, &request));
    assert(request.addr == 5u && request.func == GS_FUNC_READ_ADDR);
    assert(al_master_on_tx_complete(&master, now_ms));

    /* A contract-valid exception proves the physical candidate baud even
     * though the requested operation itself returned BUSY. */
    memset(&response, 0, sizeof(response));
    response.addr = request.addr;
    response.seq = request.seq;
    response.func = (uint8_t)(GS_FUNC_ERR_BASE | GS_FUNC_READ_ADDR);
    response.len = 1u;
    response.data[0] = GS_WIRE_ERR_BUSY;
    response.seq = (uint8_t)(request.seq + 1u);
    event = al_master_receive_at(&master, &response, ++now_ms);
    assert(event.kind == AL_MASTER_EVENT_REJECTED);
    assert(master.persist_kind == AL_MASTER_PERSIST_NONE);
    assert(master.baud.state == AL_MASTER_BAUD_VERIFY_WAIT);
    response.seq = request.seq;
    event = al_master_receive_at(&master, &response, ++now_ms);
    assert(event.kind == AL_MASTER_EVENT_PERSIST_REQUIRED);
    assert(event.persist_registry && event.wire_error == GS_WIRE_ERR_BUSY);
    assert(master.bus_baud_known);
    assert(master.nodes[0].online && master.nodes[0].missed == 0u);
    assert(master.baud.state == AL_MASTER_BAUD_RECOVERY_PERSIST);
}

static void restore_sleeping_and_online(al_master_t *master,
                                        bool two_sleepers)
{
    al_master_node_record_t records[3];
    uint8_t count = two_sleepers ? 3u : 2u;
    memset(records, 0, sizeof(records));
    records[0].uid[0] = 1u; records[0].addr = 5u;
    records[0].network_id = NET;
    records[0].sleep_state = AL_MASTER_SLEEP_CONFIRMED;
    records[0].sleep_duration_ms = 86400000u;
    records[1].uid[0] = 2u; records[1].addr = 6u;
    records[1].network_id = NET;
    if (two_sleepers) {
        records[2].uid[0] = 3u; records[2].addr = 7u;
        records[2].network_id = NET;
        records[2].sleep_state = AL_MASTER_SLEEP_CONFIRMED;
        records[2].sleep_duration_ms = 86400000u;
    }
    al_master_init(master, NET);
    assert(al_master_restore_records(master, records, count));
    master->work_next_discover_ms = 100000u;
}

static al_master_event_t timeout_current_work(al_master_t *master,
                                              al_master_time_ms_t *now_ms)
{
    assert(al_master_on_tx_complete(master, *now_ms));
    return finish_timeout(master, now_ms);
}

static void test_sleep_recovery_is_rate_limited_and_fair(void)
{
    al_master_t master;
    gs_frame_t request;
    al_master_event_t event;
    al_master_work_kind_t kind;
    al_master_time_ms_t now_ms = 100u;

    restore_sleeping_and_online(&master, true);
    kind = al_master_next_work(&master, now_ms, 1u, GS_SLOTS, &request);
    assert(kind == AL_MASTER_WORK_SAMPLE && request.addr == 6u);
    assert(al_master_on_tx_complete(&master, now_ms));
    {
        gs_frame_t response;
        memset(&response, 0, sizeof(response));
        response.addr = 6u; response.seq = request.seq;
        response.func = GS_FUNC_READ_SAMPLE; response.len = 5u;
        response.data[0] = GS_SENSOR_GENERIC;
        assert(al_master_receive_at(&master, &response, now_ms + 1u).kind ==
               AL_MASTER_EVENT_ACCEPTED);
    }
    now_ms += AL_MASTER_SLEEP_VERIFY_BASE_MS;
    kind = al_master_next_work(&master, now_ms, 2u, GS_SLOTS, &request);
    assert(kind == AL_MASTER_WORK_SLEEP_VERIFY && request.addr == 5u);
    event = timeout_current_work(&master, &now_ms);
    assert(event.kind == AL_MASTER_EVENT_TIMEOUT && !event.persist_registry);
    assert(master.nodes[0].sleep_state == AL_MASTER_SLEEP_RECOVERY_VERIFY);
    assert(master.nodes[0].sleep_lease);
    assert(!master.nodes[0].sleep_recovery_probe_in_flight);
    assert(master.nodes[0].sleep_recovery_next_probe_ms > now_ms);

    /* The ONLINE node is serviced before another recovery probe. */
    kind = al_master_next_work(&master, now_ms + 1u, 3u, GS_SLOTS, &request);
    assert(kind == AL_MASTER_WORK_SAMPLE && request.addr == 6u);
    assert(al_master_on_tx_complete(&master, now_ms + 1u));
    {
        gs_frame_t response;
        memset(&response, 0, sizeof(response));
        response.addr = 6u; response.seq = request.seq;
        response.func = GS_FUNC_READ_SAMPLE; response.len = 5u;
        response.data[0] = GS_SENSOR_GENERIC;
        event = al_master_receive_at(&master, &response, now_ms + 2u);
        assert(event.kind == AL_MASTER_EVENT_ACCEPTED);
    }

    /* A second sleeping node may then be verified; A is still in backoff. */
    kind = al_master_next_work(&master, now_ms + 2u, 4u, GS_SLOTS, &request);
    assert(kind == AL_MASTER_WORK_SLEEP_VERIFY && request.addr == 7u);
}

static void test_sleep_recovery_backoff_survives_reboot_and_early_wake(void)
{
    al_master_t master;
    al_master_t rebooted;
    al_master_node_record_t records[2];
    gs_frame_t request;
    gs_frame_t response;
    al_master_event_t event;
    al_master_work_kind_t kind;
    al_master_time_ms_t now_ms = 10u;

    restore_sleeping_and_online(&master, false);
    assert(al_master_next_work(&master, now_ms, 1u, GS_SLOTS, &request) ==
           AL_MASTER_WORK_SAMPLE);
    assert(al_master_on_tx_complete(&master, now_ms));
    memset(&response, 0, sizeof(response));
    response.addr = 6u; response.seq = request.seq;
    response.func = GS_FUNC_READ_SAMPLE; response.len = 5u;
    response.data[0] = GS_SENSOR_GENERIC;
    assert(al_master_receive_at(&master, &response, now_ms + 1u).kind ==
           AL_MASTER_EVENT_ACCEPTED);
    now_ms += AL_MASTER_SLEEP_VERIFY_BASE_MS;
    assert(al_master_next_work(&master, now_ms, 2u, GS_SLOTS, &request) ==
           AL_MASTER_WORK_SLEEP_VERIFY);
    event = timeout_current_work(&master, &now_ms);
    assert(event.kind == AL_MASTER_EVENT_TIMEOUT && !event.persist_registry);
    assert(al_master_export_records(&master, records, 2u) == 2u);

    al_master_init(&rebooted, NET);
    assert(al_master_restore_records(&rebooted, records, 2u));
    rebooted.work_next_discover_ms = 100000u;
    kind = al_master_next_work(&rebooted, 0u, 3u, GS_SLOTS, &request);
    assert(kind == AL_MASTER_WORK_SAMPLE && request.addr == 6u);
    assert(al_master_on_tx_complete(&rebooted, 0u));
    memset(&response, 0, sizeof(response));
    response.addr = 6u; response.seq = request.seq;
    response.func = GS_FUNC_READ_SAMPLE; response.len = 5u;
    response.data[0] = GS_SENSOR_GENERIC;
    assert(al_master_receive_at(&rebooted, &response, 1u).kind ==
           AL_MASTER_EVENT_ACCEPTED);

    kind = al_master_next_work(&rebooted,
                               AL_MASTER_SLEEP_VERIFY_BASE_MS - 1u,
                               4u, GS_SLOTS, &request);
    assert(kind != AL_MASTER_WORK_SLEEP_VERIFY);
    kind = al_master_next_work(&rebooted, AL_MASTER_SLEEP_VERIFY_BASE_MS,
                               5u, GS_SLOTS, &request);
    assert(kind == AL_MASTER_WORK_SLEEP_VERIFY && request.addr == 5u);
    assert(al_master_on_tx_complete(&rebooted,
                                    AL_MASTER_SLEEP_VERIFY_BASE_MS));
    memset(&response, 0, sizeof(response));
    response.addr = 5u; response.seq = request.seq;
    response.func = GS_FUNC_READ_ADDR; response.len = 1u;
    response.data[0] = 5u;
    event = al_master_receive_at(&rebooted, &response,
                                 AL_MASTER_SLEEP_VERIFY_BASE_MS + 1u);
    assert(event.kind == AL_MASTER_EVENT_ACCEPTED && event.persist_registry);
    assert(rebooted.nodes[0].sleep_state == AL_MASTER_SLEEP_NONE);
    assert(!rebooted.nodes[0].sleep_lease);

    /* Failed durable early-wake transition rolls back to a conservative lease
     * and never restores the already-finished probe as in-flight. */
    assert(al_master_on_persist_result(&rebooted, false));
    assert(rebooted.nodes[0].sleep_state ==
           AL_MASTER_SLEEP_RECOVERY_VERIFY);
    assert(rebooted.nodes[0].sleep_lease);
    assert(!rebooted.nodes[0].sleep_recovery_probe_in_flight);
    assert(!rebooted.nodes[0].sleep_recovery_schedule_armed);
    assert(rebooted.nodes[0].sleep_recovery_last_result ==
           AL_MASTER_SLEEP_VERIFY_EARLY_WAKE);
}

static void test_sleep_recovery_does_not_starve_assign_discover_or_sample(void)
{
    al_master_t master;
    al_master_node_record_t records[3];
    al_master_work_kind_t kind;
    al_master_event_t event;
    gs_frame_t request;
    gs_frame_t response;
    al_master_time_ms_t now_ms = 0u;

    memset(records, 0, sizeof(records));
    records[0].uid[0] = 1u;
    records[0].addr = 5u;
    records[0].network_id = NET;
    records[0].sleep_state = AL_MASTER_SLEEP_CONFIRMED;
    records[0].sleep_duration_ms = 86400000u;
    records[1].uid[0] = 2u;
    records[1].addr = GS_ADDR_UNASSIGNED;
    records[1].network_id = NET;
    records[2].uid[0] = 3u;
    records[2].addr = 6u;
    records[2].network_id = NET;
    al_master_init(&master, NET);
    assert(al_master_restore_records(&master, records, 3u));

    kind = al_master_next_work(&master, now_ms, 11u, GS_SLOTS, &request);
    assert(kind == AL_MASTER_WORK_ASSIGN);
    assert(al_master_on_tx_complete(&master, now_ms));
    memset(&response, 0, sizeof(response));
    response.addr = GS_ADDR_UNASSIGNED;
    response.seq = request.seq;
    response.func = GS_FUNC_REG_CONFIRM;
    response.len = request.len;
    memcpy(response.data, request.data, request.len);
    event = al_master_receive_at(&master, &response, now_ms + 1u);
    assert(event.kind == AL_MASTER_EVENT_REGISTRY_CHANGED);
    assert(event.persist_registry);
    assert(al_master_on_persist_result(&master, true));

    kind = al_master_next_work(&master, now_ms + 1u, 12u, GS_SLOTS,
                               &request);
    assert(kind == AL_MASTER_WORK_DISCOVER);
    assert(al_master_on_tx_complete(&master, now_ms + 1u));
    now_ms = master.transaction.deadline_ms;
    event = al_master_tick(&master, now_ms);
    assert(event.kind == AL_MASTER_EVENT_TIMEOUT);

    kind = al_master_next_work(&master, now_ms, 13u, GS_SLOTS, &request);
    assert(kind == AL_MASTER_WORK_SAMPLE);
    assert(request.addr == 1u);
    assert(al_master_on_tx_complete(&master, now_ms));
    memset(&response, 0, sizeof(response));
    response.addr = request.addr;
    response.seq = request.seq;
    response.func = GS_FUNC_READ_SAMPLE;
    response.len = 5u;
    response.data[0] = GS_SENSOR_GENERIC;
    assert(al_master_receive_at(&master, &response, now_ms + 1u).kind ==
           AL_MASTER_EVENT_ACCEPTED);

    now_ms += AL_MASTER_WORK_POLL_INTERVAL_MS;
    kind = al_master_next_work(&master, now_ms, 14u, GS_SLOTS, &request);
    assert(kind == AL_MASTER_WORK_SLEEP_VERIFY);
    assert(request.addr == 5u);
    event = timeout_current_work(&master, &now_ms);
    assert(event.kind == AL_MASTER_EVENT_TIMEOUT);
    kind = al_master_next_work(&master, now_ms + 1u, 15u, GS_SLOTS,
                               &request);
    assert(kind == AL_MASTER_WORK_SAMPLE);
    assert(request.addr == 6u);
}

static void test_assign_round_robin_survives_first_node_timeouts(void)
{
    al_master_t master;
    al_master_node_record_t records[2];
    al_master_event_t event;
    gs_frame_t request;
    al_master_time_ms_t now_ms = 0u;

    memset(records, 0, sizeof(records));
    records[0].uid[0] = 0x11u;
    records[0].addr = GS_ADDR_UNASSIGNED;
    records[0].network_id = NET;
    records[1].uid[0] = 0x22u;
    records[1].addr = GS_ADDR_UNASSIGNED;
    records[1].network_id = NET;
    al_master_init(&master, NET);
    assert(al_master_restore_records(&master, records, 2u));
    master.work_next_discover_ms = 100000u;

    assert(al_master_next_work(&master, now_ms, 1u, GS_SLOTS, &request) ==
           AL_MASTER_WORK_ASSIGN);
    assert(request.data[0] == 0x11u);
    assert(al_master_on_tx_complete(&master, now_ms));
    event = finish_timeout(&master, &now_ms);
    assert(event.kind == AL_MASTER_EVENT_TIMEOUT);

    assert(al_master_next_work(&master, now_ms + 1u, 2u, GS_SLOTS,
                               &request) == AL_MASTER_WORK_ASSIGN);
    assert(request.data[0] == 0x22u);
}

int main(void)
{
    test_recovered_baud_is_persisted_and_restored();
    test_recovered_baud_persistence_failure_reenters_scan();
    test_recovery_requires_proof_and_scans_each_known_node();
    test_recovery_accepts_valid_exception_proof_and_stale_offline_node();
    test_sleep_recovery_is_rate_limited_and_fair();
    test_sleep_recovery_backoff_survives_reboot_and_early_wake();
    test_sleep_recovery_does_not_starve_assign_discover_or_sample();
    test_assign_round_robin_survives_first_node_timeouts();
    puts("SC2 RCC Round 2: durable baud recovery and fair sleep verification PASS");
    return 0;
}
