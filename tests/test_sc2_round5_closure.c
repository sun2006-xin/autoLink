#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <autolink_core.h>
#include <autolink_master.h>
#include "generated/autolink_semantics.h"

static const uint8_t UID[GS_UID_LEN] = {1u, 2u, 3u, 4u, 5u, 6u};
static const uint64_t NETWORK_ID = UINT64_C(0x0102030405060708);

static void put_u64be(uint8_t *out, uint64_t value)
{
    int i;
    for (i = 7; i >= 0; --i) {
        out[i] = (uint8_t)value;
        value >>= 8;
    }
}

static al_request_t make_request(uint8_t addr, uint8_t seq, uint8_t func,
                                 uint8_t len, const uint8_t *data,
                                 uint32_t now_ms)
{
    al_request_t request = {addr, seq, func, len, data, now_ms,
        {AL_BUS_EVENT_RX_CRC_STOP, (al_time_us_t)now_ms * UINT64_C(1000),
         0u, true}};
    return request;
}

static void make_committed(al_node_t *node)
{
    al_node_init(node, UID);
    node->addr = 5u;
    node->network_id = NETWORK_ID;
    node->commission = AL_COMMISSION_COMMITTED;
    node->network = AL_NETWORK_ATTACHED;
    assert(al_node_invariants(node));
}

static void make_pending(al_node_t *node, uint32_t now_ms)
{
    al_node_init(node, UID);
    node->addr = 5u;
    node->network_id = NETWORK_ID;
    node->commission = AL_COMMISSION_PENDING;
    node->network = AL_NETWORK_ATTACHED;
    assert(al_node_timer_start(&node->assign_confirm_timer, now_ms,
                               GS_TIMING_ASSIGN_CONFIRM_TIMEOUT_MS));
    assert(al_node_invariants(node));
}

static al_request_t make_net_request(uint8_t data[16], uint8_t op,
                                     uint8_t seq, uint8_t tid, uint32_t baud,
                                     uint32_t now_ms)
{
    uint8_t length = op == GS_NET_OP_PREPARE ? 14u :
                     (op == GS_NET_OP_QUERY ? 10u : 16u);
    memset(data, 0, 16u);
    put_u64be(data, NETWORK_ID);
    data[8] = op;
    data[9] = tid;
    if (length >= 14u) gs_put_u32be(data + 10u, baud);
    if (length == 16u)
        gs_put_u16be(data + 14u, GS_TIMING_SWITCH_AFTER_MS);
    return make_request(op == GS_NET_OP_QUERY ? 5u : GS_ADDR_BROADCAST,
                        seq, GS_FUNC_NET_MGMT, length, data, now_ms);
}

static void master_with_node(al_master_t *master)
{
    al_master_init(master, NETWORK_ID);
    master->node_count = 1u;
    memcpy(master->nodes[0].uid, UID, GS_UID_LEN);
    master->nodes[0].addr = 5u;
    master->nodes[0].network_id = NETWORK_ID;
    master->nodes[0].online = true;
}

static void confirm_sleep(al_master_t *master, uint32_t duration,
                          al_master_time_ms_t tx_end)
{
    gs_frame_t request;
    gs_frame_t response;
    assert(al_master_make_sleep(master, 5u, duration, &request));
    assert(al_master_begin(master, &request, 5u,
                           GS_TIMING_RESP_TIMEOUT_MS));
    assert(al_master_on_tx_complete(master, tx_end));
    assert(al_master_on_persist_result(master, true));
    memset(&response, 0, sizeof(response));
    response.addr = 5u;
    response.seq = request.seq;
    response.func = GS_FUNC_SLEEP;
    response.len = 1u;
    assert(al_master_receive_at(master, &response, tx_end + 1u).kind ==
           AL_MASTER_EVENT_ACCEPTED);
    assert(al_master_on_persist_result(master, true));
    assert(master->nodes[0].sleep_state == AL_MASTER_SLEEP_CONFIRMED);
}

static void r5_c1_sleep_early_expiry(void)
{
    const uint8_t data[4] = {0u, 0u, 0u, 1u};
    const uint32_t start = UINT32_C(4294967000);
    al_node_t node;
    make_committed(&node);
    (void)al_node_execute(&node,
        &(al_request_t){5u, 77u, GS_FUNC_SLEEP, 4u, data, start,
            {AL_BUS_EVENT_RX_CRC_STOP, UINT64_C(4294967000000), 0u, true}});
    assert(node.power == AL_POWER_SLEEP_ARMED && node.sleep_timer.active);
    assert(al_node_tick(&node, start + 1u).effect == AL_EFFECT_NONE);
    assert(node.power == AL_POWER_SLEEP_ARMED);
    assert(al_node_tick(&node,
        (uint32_t)(start + GS_TIMING_SLEEP_ARMED_WINDOW_MS - 1u)).effect ==
        AL_EFFECT_NONE);
    assert(al_node_tick(&node,
        (uint32_t)(start + GS_TIMING_SLEEP_ARMED_WINDOW_MS)).effect ==
        AL_EFFECT_ENTER_SLEEP);
    puts("R5-C1 PASS");
}

static void r5_c2_sleep_exact_zero(void)
{
    const uint8_t data[4] = {0u, 0u, 0u, 1u};
    const uint32_t start = (uint32_t)(0u - GS_TIMING_SLEEP_ARMED_WINDOW_MS);
    al_node_t node;
    al_request_t request;
    make_pending(&node, start - 1u);
    request = make_request(5u, 78u, GS_FUNC_SLEEP, 4u, data, start);
    assert(al_node_execute(&node, &request).kind ==
           AL_ACTION_IMMEDIATE_RESPONSE);
    assert(node.commission == AL_COMMISSION_COMMITTED);
    assert(node.power == AL_POWER_SLEEP_ARMED);
    assert(node.sleep_timer.active &&
           al_node_timer_due_tick(&node.sleep_timer) == 0u);
    assert(al_node_invariants(&node));
    assert(al_node_tick(&node, UINT32_MAX).effect == AL_EFFECT_NONE);
    assert(al_node_tick(&node, 0u).effect == AL_EFFECT_ENTER_SLEEP);
    puts("R5-C2 PASS");
}

static void r5_c3_assign_exact_zero(void)
{
    uint8_t data[15] = {1u, 2u, 3u, 4u, 5u, 6u, 5u};
    const uint32_t start =
        (uint32_t)(0u - GS_TIMING_ASSIGN_CONFIRM_TIMEOUT_MS);
    al_node_t node;
    al_request_t request;
    put_u64be(data + 7u, NETWORK_ID);
    al_node_init(&node, UID);
    request = make_request(GS_ADDR_UNASSIGNED, 3u, GS_FUNC_ASSIGN_ADDR,
                           15u, data, start);
    assert(al_node_execute(&node, &request).kind ==
           AL_ACTION_IMMEDIATE_RESPONSE);
    assert(node.assign_confirm_timer.active &&
           al_node_timer_due_tick(&node.assign_confirm_timer) == 0u);
    assert(al_node_tick(&node, UINT32_MAX).kind == AL_ACTION_IGNORE);
    assert(node.commission == AL_COMMISSION_PENDING);
    assert(al_node_tick(&node, 0u).kind == AL_ACTION_STATE_ONLY);
    assert(node.commission == AL_COMMISSION_NONE && node.network_id == 0u);
    puts("R5-C3 PASS");
}

static void r5_c4_prepare_wrap(void)
{
    uint8_t data[16];
    const uint32_t start = UINT32_C(4294967000);
    al_node_t node;
    al_request_t request;
    make_committed(&node);
    request = make_net_request(data, GS_NET_OP_PREPARE, 4u, 9u, 19200u,
                               start);
    assert(al_node_execute(&node, &request).kind == AL_ACTION_STATE_ONLY);
    assert(node.baud == AL_BAUD_PREPARED);
    assert(al_node_tick(&node, start + 1u).kind == AL_ACTION_IGNORE);
    assert(node.baud == AL_BAUD_PREPARED);
    assert(al_node_tick(&node,
        (uint32_t)(start + GS_TIMING_PREPARE_TIMEOUT_MS)).kind ==
        AL_ACTION_STATE_ONLY);
    assert(node.baud == AL_BAUD_IDLE);
    puts("R5-C4 PASS");
}

static void r5_c5_registration_exact_zero(void)
{
    const uint32_t start = (uint32_t)(0u - GS_TIMING_REG_TIMEOUT_MS);
    al_node_t node;
    al_action_t report;
    al_node_init(&node, UID);
    memset(&report, 0, sizeof(report));
    report.kind = AL_ACTION_SCHEDULED_RESPONSE;
    report.purpose = AL_PURPOSE_REGISTRATION_REPORT;
    (void)al_node_on_delivery_at(&node, &report, AL_DELIVERY_DELIVERED,
                                 start);
    assert(node.commission == AL_COMMISSION_REGISTERING);
    assert(node.registration_timer.active &&
           al_node_timer_due_tick(&node.registration_timer) == 0u);
    assert(al_node_tick(&node, UINT32_MAX).kind == AL_ACTION_IGNORE);
    assert(al_node_tick(&node, 0u).kind == AL_ACTION_STATE_ONLY);
    assert(node.service == AL_SERVICE_QUARANTINED_REG);
    puts("R5-C5 PASS");
}

static void r5_c6_quarantine_exact_zero(void)
{
    const uint32_t start = (uint32_t)(0u - GS_Q_BACKOFF_1_MS);
    al_node_t node;
    al_action_t report;
    al_action_t transition;
    al_node_init(&node, UID);
    al_enter_quarantine_at(&node, AL_SERVICE_QUARANTINED_REG, start);
    assert(node.quarantine_timer.active &&
           al_node_timer_due_tick(&node.quarantine_timer) == 0u);
    assert(al_node_tick(&node, UINT32_MAX).kind == AL_ACTION_IGNORE);
    assert(al_node_tick(&node, 0u).kind == AL_ACTION_STATE_ONLY);
    assert(node.service == AL_SERVICE_NORMAL);

    al_enter_quarantine_at(&node, AL_SERVICE_QUARANTINED_REG, 1u);
    memset(&report, 0, sizeof(report));
    report.kind = AL_ACTION_SCHEDULED_RESPONSE;
    report.purpose = AL_PURPOSE_REGISTRATION_REPORT;
    transition = al_node_on_delivery_at(&node, &report,
                                        AL_DELIVERY_DELIVERED, 2u);
    assert(transition.kind == AL_ACTION_STATE_ONLY);
    assert((transition.plan.durable_ops & AL_DURABLE_QUARANTINE) != 0u);
    assert(node.service == AL_SERVICE_NORMAL);
    assert(node.commission == AL_COMMISSION_REGISTERING);
    assert(!node.quarantine_timer.active);
    assert(node.registration_timer.active);
    assert(al_node_invariants(&node));
    puts("R5-C6 PASS");
}

static void r5_c7_master_full_range(void)
{
    al_master_t master;
    al_master_time_ms_t expiry;
    master_with_node(&master);
    confirm_sleep(&master, UINT32_MAX, 1000u);
    assert(master.nodes[0].expected_wake_ms == UINT64_C(4294970295));
    expiry = al_master_time_after(master.nodes[0].expected_wake_ms,
                                  GS_TIMING_GRACE_PERIOD_MS);
    assert(al_master_tick(&master, expiry - 1u).kind == AL_MASTER_EVENT_NONE);
    assert(master.nodes[0].sleep_lease);
    assert(al_master_tick(&master, expiry).kind ==
           AL_MASTER_EVENT_PERSIST_REQUIRED);
    assert(!master.nodes[0].sleep_lease);
    puts("R5-C7 PASS");
}

static void r5_c8_duration_classes(void)
{
    static const uint32_t durations[] = {
        1u,
        GS_TIMING_SLEEP_ARMED_WINDOW_MS - 1u,
        GS_TIMING_SLEEP_ARMED_WINDOW_MS,
        GS_TIMING_SLEEP_ARMED_WINDOW_MS + 1u,
        (uint32_t)INT32_MAX - 1u,
        (uint32_t)INT32_MAX,
        (uint32_t)INT32_MAX + 1u,
        UINT32_MAX - GS_TIMING_SLEEP_ARMED_WINDOW_MS,
        UINT32_MAX - 1u,
        UINT32_MAX
    };
    size_t i;
    for (i = 0u; i < sizeof(durations) / sizeof(durations[0]); ++i) {
        al_master_t master;
        al_master_time_ms_t expected;
        master_with_node(&master);
        confirm_sleep(&master, durations[i], 1000u);
        expected = UINT64_C(1000) + GS_TIMING_SLEEP_ARMED_WINDOW_MS +
                   durations[i];
        assert(master.nodes[0].expected_wake_ms == expected);
        assert(al_master_tick(&master, expected +
            GS_TIMING_GRACE_PERIOD_MS - 1u).kind == AL_MASTER_EVENT_NONE);
        assert(master.nodes[0].sleep_lease);
    }
    puts("R5-C8 PASS");
}

static void r5_c9_multiple_wraps(void)
{
    const al_master_time_ms_t epoch = UINT64_C(4294967195);
    al_master_t master;
    al_master_time_ms_t expiry;
    master_with_node(&master);
    confirm_sleep(&master, UINT32_MAX, epoch);
    assert(master.nodes[0].expected_wake_ms ==
           epoch + GS_TIMING_SLEEP_ARMED_WINDOW_MS + UINT32_MAX);
    assert(master.nodes[0].expected_wake_ms > UINT64_C(8589934592));
    assert(al_master_tick(&master, UINT64_C(4294967295)).kind ==
           AL_MASTER_EVENT_NONE);
    assert(al_master_tick(&master, UINT64_C(4294967296)).kind ==
           AL_MASTER_EVENT_NONE);
    assert(al_master_tick(&master, UINT64_C(8589934591)).kind ==
           AL_MASTER_EVENT_NONE);
    expiry = master.nodes[0].expected_wake_ms + GS_TIMING_GRACE_PERIOD_MS;
    assert(al_master_tick(&master, expiry).kind ==
           AL_MASTER_EVENT_PERSIST_REQUIRED);
    puts("R5-C9 PASS");
}

static void r5_c10_reboot_long_lease(void)
{
    al_master_t master;
    al_master_t rebooted;
    al_master_node_record_t record;
    gs_frame_t request;
    master_with_node(&master);
    confirm_sleep(&master, UINT32_MAX, 1000u);
    assert(al_master_export_records(&master, &record, 1u) == 1u);
    al_master_init(&rebooted, NETWORK_ID);
    assert(al_master_restore_records(&rebooted, &record, 1u));
    assert(rebooted.nodes[0].sleep_state ==
           AL_MASTER_SLEEP_RECOVERY_VERIFY);
    assert(rebooted.nodes[0].sleep_lease &&
           rebooted.nodes[0].expected_wake_ms == 0u);
    rebooted.work_next_discover_ms = UINT64_C(1000000);
    assert(al_master_next_work(&rebooted, 0u, 1u, 8u, &request) ==
           AL_MASTER_WORK_NONE);
    assert(al_master_next_work(&rebooted, AL_MASTER_SLEEP_VERIFY_BASE_MS,
                               2u, 8u, &request) ==
           AL_MASTER_WORK_SLEEP_VERIFY);
    assert(rebooted.nodes[0].sleep_lease);
    puts("R5-C10 PASS");
}

static void r5_c11_duplicate_cache_wrap(void)
{
    const uint32_t start = (uint32_t)(0u - GS_TIMING_DUP_CACHE_TTL_MS);
    const uint8_t data[5] = {1u, 0u, 0u, 0u, 10u};
    al_node_t node;
    al_request_t request;
    al_action_t action;
    make_committed(&node);
    request = make_request(5u, 11u, GS_FUNC_WRITE_CFG, 5u, data, start);
    action = al_node_execute(&node, &request);
    assert(action.kind == AL_ACTION_IMMEDIATE_RESPONSE);
    assert(node.effects.retention_timer.active &&
           al_node_timer_due_tick(&node.effects.retention_timer) == 0u);
    request.now_ms = UINT32_MAX;
    assert(al_node_execute(&node, &request).replayed);
    request.now_ms = 0u;
    assert(!al_node_execute(&node, &request).replayed);
    assert(node.effects.retention_timer.active &&
           node.effects.retention_timer.start_ms == 0u);
    puts("R5-C11 PASS");
}

static void r5_c12_completed_commit_wrap(void)
{
    const uint32_t commit_start =
        (uint32_t)(0u - GS_TIMING_COMMIT_REPLAY_WINDOW_MS);
    uint8_t prepare_data[16];
    uint8_t commit_data[16];
    al_node_t node;
    al_request_t prepare;
    al_request_t commit;
    make_committed(&node);
    prepare = make_net_request(prepare_data, GS_NET_OP_PREPARE, 12u, 7u,
                               19200u, commit_start - 1u);
    assert(al_node_execute(&node, &prepare).kind == AL_ACTION_STATE_ONLY);
    commit = make_net_request(commit_data, GS_NET_OP_COMMIT, 13u, 7u,
                              19200u, commit_start);
    assert(al_node_execute(&node, &commit).kind == AL_ACTION_STATE_ONLY);
    assert(node.baud_transaction.completed_timer.active &&
           al_node_timer_due_tick(&node.baud_transaction.completed_timer) ==
           0u);
    (void)al_node_tick(&node, UINT32_MAX);
    commit.now_ms = UINT32_MAX;
    assert(al_node_execute(&node, &commit).kind == AL_ACTION_IGNORE);
    assert(node.baud_transaction.completed);
    (void)al_node_tick(&node, 0u);
    assert(!node.baud_transaction.completed);
    puts("R5-C12 PASS");
}

static void r5_c13_baud_switch_wrap(void)
{
    const uint32_t commit_start =
        (uint32_t)(0u - GS_TIMING_SWITCH_AFTER_MS);
    uint8_t prepare_data[16];
    uint8_t commit_data[16];
    al_node_t node;
    al_request_t prepare;
    al_request_t commit;
    make_committed(&node);
    prepare = make_net_request(prepare_data, GS_NET_OP_PREPARE, 14u, 8u,
                               19200u, commit_start - 1u);
    (void)al_node_execute(&node, &prepare);
    commit = make_net_request(commit_data, GS_NET_OP_COMMIT, 15u, 8u,
                              19200u, commit_start);
    (void)al_node_execute(&node, &commit);
    assert(node.baud_transaction.switch_timer.active &&
           al_node_timer_due_tick(&node.baud_transaction.switch_timer) == 0u);
    assert(al_node_tick(&node, UINT32_MAX).effect == AL_EFFECT_NONE);
    assert(node.baud == AL_BAUD_SWITCH_WAIT);
    assert(al_node_tick(&node, 0u).effect == AL_EFFECT_SET_BAUD);
    assert(node.current_baud == 19200u);
    puts("R5-C13 PASS");
}

static void r5_c14_candidate_sleep_atomic_wrap(void)
{
    const uint8_t data[4] = {0u, 0u, 0u, 9u};
    const uint32_t start = UINT32_MAX - 100u;
    al_node_t node;
    al_request_t request;
    al_action_t first;
    uint32_t timer_start;
    make_pending(&node, start - 1u);
    request = make_request(5u, 16u, GS_FUNC_SLEEP, 4u, data, start);
    first = al_node_execute(&node, &request);
    assert((first.plan.durable_ops & AL_DURABLE_COMMISSION_COMMITTED) != 0u);
    assert(node.commission == AL_COMMISSION_COMMITTED &&
           node.power == AL_POWER_SLEEP_ARMED);
    timer_start = node.sleep_timer.start_ms;
    request.now_ms = start + 1u;
    assert(al_node_execute(&node, &request).replayed);
    assert(node.sleep_timer.start_ms == timer_start);
    puts("R5-C14 PASS");
}

static void r5_c15_active_numeric_zero(void)
{
    static const uint32_t intervals[] = {
        GS_TIMING_REG_TIMEOUT_MS,
        GS_TIMING_ASSIGN_CONFIRM_TIMEOUT_MS,
        GS_TIMING_PREPARE_TIMEOUT_MS,
        GS_TIMING_SLEEP_ARMED_WINDOW_MS,
        GS_Q_BACKOFF_1_MS,
        GS_Q_BACKOFF_2_MS,
        GS_Q_BACKOFF_3_MS,
        GS_Q_BACKOFF_MAX_MS,
        GS_TIMING_SWITCH_AFTER_MS,
        GS_TIMING_DUP_CACHE_TTL_MS,
        GS_TIMING_COMMIT_REPLAY_WINDOW_MS
    };
    size_t i;
    for (i = 0u; i < sizeof(intervals) / sizeof(intervals[0]); ++i) {
        al_node_timer_t timer = {false, 0u, 0u};
        assert(al_node_timer_start(&timer, (uint32_t)(0u - intervals[i]),
                                   intervals[i]));
        assert(timer.active && al_node_timer_due_tick(&timer) == 0u);
        assert(!al_node_timer_expired(&timer, UINT32_MAX));
        assert(al_node_timer_expired(&timer, 0u));
    }
    puts("R5-C15 PASS");
}

int main(void)
{
    r5_c1_sleep_early_expiry();
    r5_c2_sleep_exact_zero();
    r5_c3_assign_exact_zero();
    r5_c4_prepare_wrap();
    r5_c5_registration_exact_zero();
    r5_c6_quarantine_exact_zero();
    r5_c7_master_full_range();
    r5_c8_duration_classes();
    r5_c9_multiple_wraps();
    r5_c10_reboot_long_lease();
    r5_c11_duplicate_cache_wrap();
    r5_c12_completed_commit_wrap();
    r5_c13_baud_switch_wrap();
    r5_c14_candidate_sleep_atomic_wrap();
    r5_c15_active_numeric_zero();
    puts("SC2 RCC Round 5: R5-C1..R5-C15 PASS");
    return 0;
}
