#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <autolink_node_engine.h>
#include "generated/autolink_semantics.h"

static const uint8_t UID[GS_UID_LEN] = {1u, 2u, 3u, 4u, 5u, 6u};
static const uint64_t NETWORK_ID = UINT64_C(0x0102030405060708);

typedef struct {
    al_persistent_record_t slots[2];
    unsigned writes;
    bool fail_write;
} memory_store_t;

static bool read_slot(void *context, uint8_t slot, void *data, size_t len)
{
    memory_store_t *store = (memory_store_t *)context;
    memcpy(data, &store->slots[slot], len);
    return true;
}

static bool write_slot(void *context, uint8_t slot, const void *data,
                       size_t len)
{
    memory_store_t *store = (memory_store_t *)context;
    ++store->writes;
    if (store->fail_write) return false;
    memcpy(&store->slots[slot], data, len);
    return true;
}

static al_storage_port_t storage(memory_store_t *store)
{
    al_storage_port_t port = {store, read_slot, write_slot};
    return port;
}

static void put_u64be(uint8_t *out, uint64_t value)
{
    int i;
    for (i = 7; i >= 0; --i) {
        out[i] = (uint8_t)value;
        value >>= 8;
    }
}

static al_request_t request(uint8_t addr, uint8_t seq, uint8_t func,
                            const uint8_t *data, uint8_t len,
                            uint32_t now_ms)
{
    al_request_t value = {addr, seq, func, len, data, now_ms,
        {AL_BUS_EVENT_RX_CRC_STOP, (al_time_us_t)now_ms * UINT64_C(1000),
         0u, true}};
    return value;
}

static al_request_t net_request(uint8_t data[16], uint8_t op, uint8_t seq,
                                uint8_t tid, uint32_t baud,
                                uint32_t now_ms)
{
    uint8_t len = op == GS_NET_OP_PREPARE ? 14u :
                  (op == GS_NET_OP_QUERY ? 10u : 16u);
    memset(data, 0, 16u);
    put_u64be(data, NETWORK_ID);
    data[8] = op;
    data[9] = tid;
    if (len >= 14u) gs_put_u32be(data + 10u, baud);
    if (len == 16u)
        gs_put_u16be(data + 14u, GS_TIMING_SWITCH_AFTER_MS);
    return request(op == GS_NET_OP_QUERY ? 5u : GS_ADDR_BROADCAST,
                   seq, GS_FUNC_NET_MGMT, data, len, now_ms);
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

static void request_observation_expires_prepare_before_query(void)
{
    uint8_t data[16];
    al_node_t node;
    al_request_t value;
    al_action_t action;

    make_committed(&node);
    value = net_request(data, GS_NET_OP_PREPARE, 1u, 3u, 19200u, 0u);
    assert(al_node_execute(&node, &value).kind == AL_ACTION_STATE_ONLY);
    assert(node.baud == AL_BAUD_PREPARED);

    value = net_request(data, GS_NET_OP_QUERY, 2u, 3u, 0u,
                        GS_TIMING_PREPARE_TIMEOUT_MS);
    action = al_node_execute(&node, &value);
    assert(node.baud == AL_BAUD_IDLE);
    assert(action.kind == AL_ACTION_IMMEDIATE_RESPONSE);
    assert(action.response_len == 2u);
    assert(action.response_data[0] == GS_QUERY_REJECT);
    assert(action.response_data[1] == GS_QUERY_REASON_NO_PREPARE);
    puts("timer-observation prepare-before-query PASS");
}

static void request_observation_expires_candidate_before_confirmation(void)
{
    al_node_t node;
    al_request_t value;
    al_action_t action;

    al_node_init(&node, UID);
    node.addr = 5u;
    node.network_id = NETWORK_ID;
    node.commission = AL_COMMISSION_PENDING;
    node.network = AL_NETWORK_ATTACHED;
    assert(al_node_timer_start(&node.assign_confirm_timer, 0u,
                               GS_TIMING_ASSIGN_CONFIRM_TIMEOUT_MS));
    assert(al_node_invariants(&node));

    value = request(5u, 3u, GS_FUNC_READ_ADDR, NULL, 0u,
                    GS_TIMING_ASSIGN_CONFIRM_TIMEOUT_MS);
    action = al_node_execute(&node, &value);
    assert(node.commission == AL_COMMISSION_NONE);
    assert(node.network_id == 0u);
    assert(action.kind == AL_ACTION_STATE_ONLY);
    assert((action.plan.durable_ops & AL_DURABLE_IDENTITY_CLEAR) != 0u);
    puts("timer-observation candidate-expiry-before-confirmation PASS");
}

static void delayed_tick_drains_each_due_external_effect(void)
{
    static const uint8_t sleep_data[4] = {0u, 0u, 3u, 0xE8u};
    uint8_t data[16];
    al_node_t node;
    al_request_t value;
    al_action_t commit;
    al_action_t sleep;
    al_action_t first;
    al_action_t second;

    make_committed(&node);
    value = net_request(data, GS_NET_OP_PREPARE, 4u, 3u, 19200u, 0u);
    (void)al_node_execute(&node, &value);
    value = net_request(data, GS_NET_OP_COMMIT, 5u, 3u, 19200u, 0u);
    commit = al_node_execute(&node, &value);
    assert(commit.kind == AL_ACTION_STATE_ONLY);
    assert(commit.effect == AL_EFFECT_SET_BAUD);
    assert(!al_node_action_effect_is_due(&node, &commit));
    assert(node.current_baud != 19200u);
    value = request(5u, 6u, GS_FUNC_SLEEP, sleep_data, 4u, 1u);
    sleep = al_node_execute(&node, &value);
    assert(sleep.kind == AL_ACTION_IMMEDIATE_RESPONSE);
    assert(sleep.effect == AL_EFFECT_ENTER_SLEEP);
    assert(!al_node_action_effect_is_due(&node, &sleep));
    assert(node.baud == AL_BAUD_SWITCH_WAIT);
    assert(node.power == AL_POWER_SLEEP_ARMED);

    first = al_node_tick(&node, 3000u);
    assert(first.effect == AL_EFFECT_SET_BAUD);
    assert(al_node_action_effect_is_due(&node, &first));
    assert(first.effect_value == 19200u);
    assert(node.current_baud == 19200u);
    assert(node.power == AL_POWER_SLEEP_ARMED);

    second = al_node_tick(&node, 3000u);
    assert(second.effect == AL_EFFECT_ENTER_SLEEP);
    assert(al_node_action_effect_is_due(&node, &second));
    assert(second.effect_value == 1000u);
    assert(node.power == AL_POWER_SLEEPING);
    puts("timer-observation multi-effect-drain PASS");
}

static void engine_advance_rolls_back_failed_timer_persistence(void)
{
    memory_store_t store;
    memory_store_t malformed_store;
    memory_store_t request_store;
    al_node_engine_t engine;
    al_node_engine_t malformed_engine;
    al_node_engine_t request_engine;
    al_action_t action;
    al_action_t raw_due;
    al_action_t preview_action;
    al_node_t preview;
    al_request_t value;
    al_request_t malformed;
    uint8_t assign_data[15] = {1u, 2u, 3u, 4u, 5u, 6u, 6u};
    const uint64_t new_network_id = NETWORK_ID + UINT64_C(1);

    memset(&store, 0, sizeof(store));
    al_node_engine_init(&engine, UID, storage(&store));
    engine.node.addr = 5u;
    engine.node.network_id = NETWORK_ID;
    engine.node.commission = AL_COMMISSION_PENDING;
    engine.node.network = AL_NETWORK_ATTACHED;
    assert(al_node_timer_start(&engine.node.assign_confirm_timer, 0u,
                               GS_TIMING_ASSIGN_CONFIRM_TIMEOUT_MS));
    engine.node.baud = AL_BAUD_SWITCH_WAIT;
    engine.node.baud_transaction.pending_baud = 19200u;
    assert(al_node_timer_start(&engine.node.baud_transaction.switch_timer,
                               GS_TIMING_ASSIGN_CONFIRM_TIMEOUT_MS -
                                   GS_TIMING_SWITCH_AFTER_MS,
                               GS_TIMING_SWITCH_AFTER_MS));
    assert(al_node_invariants(&engine.node));

    preview = engine.node;
    raw_due = al_node_tick(&preview, GS_TIMING_ASSIGN_CONFIRM_TIMEOUT_MS);
    assert(raw_due.effect == AL_EFFECT_SET_BAUD);
    assert((raw_due.plan.durable_ops & AL_DURABLE_IDENTITY_CLEAR) != 0u);
    assert(!al_node_action_effect_is_due(&preview, &raw_due));

    store.fail_write = true;
    assert(!al_node_engine_advance(&engine,
                                   GS_TIMING_ASSIGN_CONFIRM_TIMEOUT_MS,
                                   &action));
    assert(engine.node.commission == AL_COMMISSION_PENDING);
    assert(engine.node.assign_confirm_timer.active);
    assert(engine.node.baud == AL_BAUD_SWITCH_WAIT);
    assert(engine.node.current_baud != 19200u);
    assert(action.kind == AL_ACTION_IGNORE);
    assert(action.effect == AL_EFFECT_NONE);

    store.fail_write = false;
    assert(al_node_engine_advance(&engine,
                                  GS_TIMING_ASSIGN_CONFIRM_TIMEOUT_MS,
                                  &action));
    assert(action.kind == AL_ACTION_STATE_ONLY);
    assert(action.plan.durable_ops == 0u);
    assert(action.effect == AL_EFFECT_SET_BAUD);
    assert(al_node_action_effect_is_due(&engine.node, &action));
    assert(engine.node.commission == AL_COMMISSION_NONE);
    assert(engine.node.network_id == 0u);
    assert(engine.node.current_baud == 19200u);
    assert(store.writes == 2u);
    assert(al_node_invariants(&engine.node));

    /* An unrelated elapsed durable transition must not make an invalid
       request look like that request's own persistence failure. */
    memset(&malformed_store, 0, sizeof(malformed_store));
    al_node_engine_init(&malformed_engine, UID, storage(&malformed_store));
    malformed_engine.node.addr = 5u;
    malformed_engine.node.network_id = NETWORK_ID;
    malformed_engine.node.commission = AL_COMMISSION_PENDING;
    malformed_engine.node.network = AL_NETWORK_ATTACHED;
    assert(al_node_timer_start(&malformed_engine.node.assign_confirm_timer,
                               0u, GS_TIMING_ASSIGN_CONFIRM_TIMEOUT_MS));
    malformed = request(5u, 7u, GS_FUNC_WRITE_CFG, NULL, 0u,
                        GS_TIMING_ASSIGN_CONFIRM_TIMEOUT_MS);
    malformed_store.fail_write = true;
    action = al_node_engine_execute(&malformed_engine, &malformed);
    assert(action.kind == AL_ACTION_IGNORE);
    assert(action.effect == AL_EFFECT_NONE);
    assert(!malformed_engine.node.effects.valid);
    assert(malformed_engine.node.commission == AL_COMMISSION_PENDING);
    assert(malformed_engine.node.assign_confirm_timer.active);
    assert(malformed_store.writes == 1u);
    assert(al_node_invariants(&malformed_engine.node));

    /* At a request boundary, the expired identity clear and replacement
       ASSIGN are one request plan and therefore one successful snapshot. */
    memset(&request_store, 0, sizeof(request_store));
    al_node_engine_init(&request_engine, UID, storage(&request_store));
    request_engine.node.addr = 5u;
    request_engine.node.network_id = NETWORK_ID;
    request_engine.node.commission = AL_COMMISSION_PENDING;
    request_engine.node.network = AL_NETWORK_ATTACHED;
    assert(al_node_timer_start(&request_engine.node.assign_confirm_timer, 0u,
                               GS_TIMING_ASSIGN_CONFIRM_TIMEOUT_MS));
    put_u64be(assign_data + 7u, new_network_id);
    value = request(GS_ADDR_UNASSIGNED, 8u, GS_FUNC_ASSIGN_ADDR,
                    assign_data, 15u,
                    GS_TIMING_ASSIGN_CONFIRM_TIMEOUT_MS);

    preview = request_engine.node;
    preview_action = al_node_prepare(&preview, &value);
    assert((preview_action.plan.durable_ops & AL_DURABLE_IDENTITY_CLEAR) != 0u);
    assert((preview_action.plan.durable_ops & AL_DURABLE_IDENTITY_PENDING) != 0u);
    assert(preview.commission == AL_COMMISSION_PENDING);
    assert(preview.addr == 6u && preview.network_id == new_network_id);

    request_store.fail_write = true;
    action = al_node_engine_execute(&request_engine, &value);
    assert(action.kind == AL_ACTION_ERROR);
    assert(action.wire_error == GS_WIRE_ERR_PERSIST_FAILED);
    assert(request_engine.node.commission == AL_COMMISSION_PENDING);
    assert(request_engine.node.addr == 5u);
    assert(request_engine.node.network_id == NETWORK_ID);
    assert(request_engine.node.assign_confirm_timer.active);
    assert(request_store.writes == 1u);

    request_store.fail_write = false;
    value.seq = 9u;
    action = al_node_engine_execute(&request_engine, &value);
    assert(action.kind == AL_ACTION_IMMEDIATE_RESPONSE);
    assert(!action.replayed);
    assert(request_engine.node.commission == AL_COMMISSION_PENDING);
    assert(request_engine.node.addr == 6u);
    assert(request_engine.node.network_id == new_network_id);
    assert(request_engine.node.assign_confirm_timer.active);
    assert(request_store.writes == 2u);
    assert(al_node_invariants(&request_engine.node));
    puts("timer-observation engine-persistence-rollback PASS");
}

int main(void)
{
    request_observation_expires_prepare_before_query();
    request_observation_expires_candidate_before_confirmation();
    delayed_tick_drains_each_due_external_effect();
    engine_advance_rolls_back_failed_timer_persistence();
    puts("SC2 timer observation closure tests passed");
    return 0;
}
