#include <assert.h>
#include <string.h>

#include <autolink_node_engine.h>

typedef struct {
    al_persistent_record_t slots[2];
    unsigned writes;
    bool fail_write;
    unsigned fail_on_write;
} memory_store_t;

static const uint8_t UID[GS_UID_LEN] = {1u, 2u, 3u, 4u, 5u, 6u};

static bool read_slot(void *context, uint8_t slot, void *data, size_t len)
{
    memory_store_t *store = (memory_store_t *)context;
    memcpy(data, &store->slots[slot], len);
    return true;
}

static bool write_slot(void *context, uint8_t slot, const void *data, size_t len)
{
    memory_store_t *store = (memory_store_t *)context;
    ++store->writes;
    if (store->fail_write || store->fail_on_write == store->writes) return false;
    memcpy(&store->slots[slot], data, len);
    return true;
}

static al_storage_port_t storage_port(memory_store_t *store)
{
    al_storage_port_t port;
    port.context = store;
    port.read_slot = read_slot;
    port.write_slot = write_slot;
    return port;
}

static void make_pending(al_node_engine_t *engine, memory_store_t *store)
{
    memset(store, 0, sizeof(*store));
    al_node_engine_init(engine, UID, storage_port(store));
    engine->node.addr = 5u;
    engine->node.network_id = UINT64_C(0x0102030405060708);
    engine->node.commission = AL_COMMISSION_PENDING;
    engine->node.network = AL_NETWORK_ATTACHED;
    assert(al_node_timer_start(&engine->node.assign_confirm_timer, 0u,
                               GS_TIMING_ASSIGN_CONFIRM_TIMEOUT_MS));
    assert(al_node_invariants(&engine->node));
}

static al_request_t sleep_request(uint8_t seq, const uint8_t data[4],
                                  uint32_t now_ms)
{
    al_request_t request = {
        .addr = 5u,
        .seq = seq,
        .func = GS_FUNC_SLEEP,
        .len = 4u,
        .data = data,
        .now_ms = now_ms,
        .request_crc_stop = {
            AL_BUS_EVENT_RX_CRC_STOP,
            (al_time_us_t)now_ms * UINT64_C(1000),
            0u,
            true
        }
    };
    return request;
}

static void test_pending_sleep_commits_and_arms_atomically(void)
{
    const uint8_t duration[4] = {0u, 0u, 0u, 1u};
    memory_store_t store;
    al_node_engine_t engine;
    al_request_t request;
    al_action_t action;

    make_pending(&engine, &store);
    request = sleep_request(13u, duration, 100u);
    action = al_node_engine_execute(&engine, &request);

    assert(action.kind == AL_ACTION_IMMEDIATE_RESPONSE);
    assert(action.response_func == GS_FUNC_SLEEP);
    assert(action.response_len == 1u && action.response_data[0] == 0u);
    assert(action.effect == AL_EFFECT_ENTER_SLEEP);
    assert(action.effect_due_ms == 100u + GS_TIMING_SLEEP_ARMED_WINDOW_MS);
    assert(action.effect_value == 1u);
    assert(engine.node.commission == AL_COMMISSION_COMMITTED);
    assert(engine.node.power == AL_POWER_SLEEP_ARMED);
    assert(!engine.node.assign_confirm_timer.active);
    assert(al_node_timer_due_tick(&engine.node.sleep_timer) ==
           action.effect_due_ms);
    assert(engine.node.sleep_duration_ms == 1u);
    assert(store.writes == 1u);
    assert(al_node_invariants(&engine.node));
}

static void test_persist_failure_rolls_back_and_does_not_complete(void)
{
    const uint8_t duration[4] = {0u, 0u, 0u, 7u};
    memory_store_t store;
    al_node_engine_t engine;
    al_request_t request;
    al_action_t failed;
    al_action_t retried;

    make_pending(&engine, &store);
    store.fail_write = true;
    request = sleep_request(14u, duration, 200u);
    failed = al_node_engine_execute(&engine, &request);

    assert(failed.kind == AL_ACTION_IGNORE);
    assert(failed.effect == AL_EFFECT_NONE);
    assert(!failed.cacheable_completion);
    assert(engine.node.commission == AL_COMMISSION_PENDING);
    assert(engine.node.power == AL_POWER_AWAKE);
    assert(al_node_timer_due_tick(&engine.node.assign_confirm_timer) ==
           GS_TIMING_ASSIGN_CONFIRM_TIMEOUT_MS);
    assert(!engine.node.sleep_timer.active);
    assert(engine.node.sleep_duration_ms == 0u);
    assert(!engine.node.effects.valid);
    assert(al_node_invariants(&engine.node));

    store.fail_write = false;
    request.now_ms = 201u;
    retried = al_node_engine_execute(&engine, &request);
    assert(retried.kind == AL_ACTION_IMMEDIATE_RESPONSE);
    assert(!retried.replayed);
    assert(engine.node.commission == AL_COMMISSION_COMMITTED);
    assert(engine.node.power == AL_POWER_SLEEP_ARMED);
    assert(engine.node.effects.valid);
    assert(store.writes == 2u);
    assert(al_node_invariants(&engine.node));
}

static void test_duplicate_and_conflict_do_not_move_sleep_deadline(void)
{
    const uint8_t duration_a[4] = {0u, 0u, 0u, 9u};
    const uint8_t duration_b[4] = {0u, 0u, 0u, 10u};
    memory_store_t store;
    al_node_engine_t engine;
    al_request_t request;
    al_action_t first;
    al_action_t duplicate;
    al_action_t conflict;
    uint32_t deadline;

    make_pending(&engine, &store);
    request = sleep_request(15u, duration_a, 300u);
    first = al_node_engine_execute(&engine, &request);
    assert(first.kind == AL_ACTION_IMMEDIATE_RESPONSE);
    deadline = al_node_timer_due_tick(&engine.node.sleep_timer);

    request.now_ms = 301u;
    duplicate = al_node_engine_execute(&engine, &request);
    assert(duplicate.kind == AL_ACTION_IMMEDIATE_RESPONSE);
    assert(duplicate.replayed);
    assert(duplicate.effect == AL_EFFECT_NONE);
    assert(al_node_timer_due_tick(&engine.node.sleep_timer) == deadline);
    assert(store.writes == 1u);
    assert(al_node_invariants(&engine.node));

    request.data = duration_b;
    request.now_ms = 302u;
    conflict = al_node_engine_execute(&engine, &request);
    assert(conflict.kind == AL_ACTION_ERROR);
    assert(conflict.wire_error == GS_WIRE_ERR_TXN_CONFLICT);
    assert(conflict.effect == AL_EFFECT_NONE);
    assert(al_node_timer_due_tick(&engine.node.sleep_timer) == deadline);
    assert(store.writes == 1u);
    assert(al_node_invariants(&engine.node));
}

static void test_success_reboots_committed_without_transient_sleep(void)
{
    const uint8_t duration[4] = {0u, 0u, 0u, 11u};
    memory_store_t store;
    al_node_engine_t engine;
    al_node_engine_t rebooted;
    al_request_t request;

    make_pending(&engine, &store);
    request = sleep_request(16u, duration, 400u);
    assert(al_node_engine_execute(&engine, &request).kind ==
           AL_ACTION_IMMEDIATE_RESPONSE);
    al_node_engine_init(&rebooted, UID, storage_port(&store));
    assert(al_node_engine_restore(&rebooted));
    assert(rebooted.node.commission == AL_COMMISSION_COMMITTED);
    assert(rebooted.node.power == AL_POWER_AWAKE);
    assert(!rebooted.node.sleep_timer.active);
    assert(rebooted.node.sleep_duration_ms == 0u);
    assert(!rebooted.node.effects.valid);
    assert(al_node_invariants(&rebooted.node));
}

static void test_sleep_timer_boundaries_and_ttl_reuse(void)
{
    static const uint32_t durations[] = {1u, GS_TIMING_SLEEP_ARMED_WINDOW_MS,
                                         UINT32_MAX};
    size_t i;
    for (i = 0u; i < sizeof(durations) / sizeof(durations[0]); ++i) {
        uint8_t data[4];
        memory_store_t store;
        al_node_engine_t engine;
        al_request_t request;
        al_action_t before_deadline;
        al_action_t at_deadline;
        al_action_t reused;
        uint32_t deadline;

        gs_put_u32be(data, durations[i]);
        make_pending(&engine, &store);
        request = sleep_request((uint8_t)(20u + i), data, 1000u);
        assert(al_node_engine_execute(&engine, &request).kind ==
               AL_ACTION_IMMEDIATE_RESPONSE);
        deadline = al_node_timer_due_tick(&engine.node.sleep_timer);
        before_deadline = al_node_engine_tick(&engine, deadline - 1u);
        assert(before_deadline.kind == AL_ACTION_IGNORE);
        assert(engine.node.power == AL_POWER_SLEEP_ARMED);
        assert(al_node_invariants(&engine.node));
        at_deadline = al_node_engine_tick(&engine, deadline);
        assert(at_deadline.effect == AL_EFFECT_ENTER_SLEEP);
        assert(at_deadline.effect_value == durations[i]);
        assert(engine.node.power == AL_POWER_SLEEPING);
        assert(al_node_invariants(&engine.node));
        al_node_wake(&engine.node, deadline + durations[i]);
        assert(al_node_invariants(&engine.node));

        request.now_ms = 1000u + GS_TIMING_DUP_CACHE_TTL_MS;
        reused = al_node_engine_execute(&engine, &request);
        assert(reused.kind == AL_ACTION_IMMEDIATE_RESPONSE);
        assert(!reused.replayed);
        assert(engine.node.power == AL_POWER_SLEEP_ARMED);
        assert(al_node_invariants(&engine.node));
    }
}

static bool read_device_info(void *context, al_device_info_t *info)
{
    *info = *(const al_device_info_t *)context;
    return true;
}

static void seed_pending_snapshot(al_node_engine_t *engine,
                                  memory_store_t *store)
{
    al_node_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.schema_version = 2u;
    snapshot.addr = 5u;
    snapshot.commission = AL_COMMISSION_PENDING;
    snapshot.administrative = AL_ADMIN_ACTIVE;
    snapshot.quarantine = AL_SERVICE_NORMAL;
    snapshot.network_id = UINT64_C(0x0102030405060708);
    snapshot.baud = GS_BAUD_9600;
    assert(al_journal_commit(&engine->journal, &snapshot, sizeof(snapshot)));
    assert(store->writes == 1u);
}

static void test_schema_and_confirmation_share_one_durable_commit(void)
{
    const uint8_t base_info[1] = {GS_INFO_SUB_BASE};
    memory_store_t store;
    al_node_engine_t engine;
    al_node_engine_t rebooted;
    al_device_info_t info;
    al_node_application_port_t application;
    al_request_t request;
    al_action_t action;

    make_pending(&engine, &store);
    seed_pending_snapshot(&engine, &store);
    memset(&info, 0, sizeof(info));
    info.firmware_version = 0x0100u;
    info.sensor_type = GS_SENSOR_SHT31;
    info.capabilities = GS_CAP_CONFIGURABLE;
    application.context = &info;
    application.read_sample = NULL;
    application.read_device_info = read_device_info;
    al_node_engine_set_application_port(&engine, application);

    /* The old nested schema commit consumed write 2 and leaked COMMITTED when
       the outer transaction failed on write 3.  A single request transaction
       has exactly one post-seed durable commit, so write 3 is never reached. */
    store.fail_on_write = 3u;
    request = (al_request_t){
        .addr = 5u, .seq = 30u, .func = GS_FUNC_READ_INFO,
        .len = 1u, .data = base_info, .now_ms = 2000u,
        .request_crc_stop = {AL_BUS_EVENT_RX_CRC_STOP, UINT64_C(2000000),
                             0u, true}
    };
    action = al_node_engine_execute(&engine, &request);
    assert(action.kind == AL_ACTION_IMMEDIATE_RESPONSE);
    assert(engine.node.commission == AL_COMMISSION_COMMITTED);
    assert(engine.node.schema_rev == 1u);
    assert(store.writes == 2u);
    assert(al_node_invariants(&engine.node));

    al_node_engine_init(&rebooted, UID, storage_port(&store));
    assert(al_node_engine_restore(&rebooted));
    assert(rebooted.node.commission == AL_COMMISSION_COMMITTED);
    assert(rebooted.node.schema_rev == 1u);
    assert(al_node_invariants(&rebooted.node));
}

int main(void)
{
    test_pending_sleep_commits_and_arms_atomically();
    test_persist_failure_rolls_back_and_does_not_complete();
    test_duplicate_and_conflict_do_not_move_sleep_deadline();
    test_success_reboots_committed_without_transient_sleep();
    test_sleep_timer_boundaries_and_ttl_reuse();
    test_schema_and_confirmation_share_one_durable_commit();
    return 0;
}
