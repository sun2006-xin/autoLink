#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <autolink_master.h>
#include <autolink_node_engine.h>

typedef struct {
    al_persistent_record_t slots[2];
} memory_store_t;

static bool read_slot(void *context, uint8_t slot, void *data, size_t len)
{
    memory_store_t *store = (memory_store_t *)context;
    memcpy(data, &store->slots[slot], len);
    return true;
}

static bool write_slot(void *context, uint8_t slot, const void *data, size_t len)
{
    memory_store_t *store = (memory_store_t *)context;
    memcpy(&store->slots[slot], data, len);
    return true;
}

static void put_u64be(uint8_t *p, uint64_t value)
{
    int i;
    for (i = 7; i >= 0; --i) {
        p[i] = (uint8_t)value;
        value >>= 8;
    }
}

static al_request_t make_request(uint8_t addr, uint8_t seq, uint8_t func,
                                 uint8_t len, const uint8_t *data,
                                 uint32_t now_ms)
{
    al_request_t request = {.addr = addr, .seq = seq, .func = func,
                            .len = len, .data = data, .now_ms = now_ms,
                            .request_crc_stop =
                                {AL_BUS_EVENT_RX_CRC_STOP,
                                 (al_time_us_t)now_ms * UINT64_C(1000),
                                 0u, true}};
    return request;
}

static void make_committed(al_node_t *node, const uint8_t uid[GS_UID_LEN])
{
    al_node_init(node, uid);
    node->addr = 5u;
    node->network_id = UINT64_C(0x0102030405060708);
    node->commission = AL_COMMISSION_COMMITTED;
    node->network = AL_NETWORK_ATTACHED;
}

int main(void)
{
    const uint8_t uid[GS_UID_LEN] = {1u, 2u, 3u, 4u, 5u, 6u};
    gs_sample_t sample;
    gs_chdesc_t descriptor;
    uint8_t encoded[32];
    al_node_t node;
    al_action_t action;

    memset(&sample, 0, sizeof(sample));
    sample.sensor_type = GS_SENSOR_SHT31;
    sample.channel_count = 2u;
    sample.channels[0].id = GS_CH_TEMP;
    sample.channels[0].value_len = 2u;
    sample.channels[1].id = GS_CH_TEMP;
    sample.channels[1].value_len = 2u;
    assert(gs_build_sample_v22(encoded, &sample) == 0u);
    sample.channels[1].id = GS_CH_RH;
    sample.sensor_type = 0x7Fu;
    assert(gs_build_sample_v22(encoded, &sample) == 0u);

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.ch_id = GS_CH_TEMP;
    descriptor.quantity_id = 0u;
    descriptor.data_type = GS_DESC_DT_INT16;
    descriptor.unit = GS_DESC_UNIT_CELSIUS;
    descriptor.flags = GS_DESC_FLAG_RO;
    assert(gs_build_chdesc(encoded, &descriptor) == 0u);

    {
        uint8_t discover[13] = {0u, 0u, 0u, 1u, 8u};
        al_request_t request;
        put_u64be(discover + 5u, UINT64_C(0x0102030405060708));
        al_node_init(&node, uid);
        request = make_request(GS_ADDR_BROADCAST, 1u, GS_FUNC_DISCOVER,
                               sizeof(discover), discover, 10u);
        request.request_crc_stop.at_us = UINT64_C(10050);
        action = al_node_execute(&node, &request);
        assert(action.kind == AL_ACTION_SCHEDULED_RESPONSE);
        assert(action.scheduled_start_bit_us == UINT64_C(10050) +
               GS_TIMING_T_IFG_MS * UINT64_C(1000) +
               (al_time_us_t)action.slot *
               GS_TIMING_T_SLOT_DISCOVER_MS * UINT64_C(1000));
        (void)al_node_on_delivery_at(&node, &action,
                                     AL_DELIVERY_DELIVERED, 123u);
        assert(al_node_timer_due_tick(&node.registration_timer) ==
               123u + GS_TIMING_REG_TIMEOUT_MS);
        assert(node.commission == AL_COMMISSION_REGISTERING);
        action = al_node_tick(&node,
                              al_node_timer_due_tick(&node.registration_timer));
        assert(action.kind == AL_ACTION_STATE_ONLY);
        assert(node.commission == AL_COMMISSION_NONE);
        assert(node.service == AL_SERVICE_QUARANTINED_REG);
    }

    make_committed(&node, uid);
    al_enter_quarantine_at(&node, AL_SERVICE_QUARANTINED_SERVICE, 0u);
    action = al_node_tick(&node, GS_Q_BACKOFF_1_MS);
    assert(node.service_retry_due);
    memset(&action, 0, sizeof(action));
    action.purpose = AL_PURPOSE_SERVICE_RECOVERY_REPORT;
    (void)al_node_on_delivery_at(&node, &action,
                                 AL_DELIVERY_FAILED, 6000u);
    assert(node.service == AL_SERVICE_QUARANTINED_SERVICE);
    assert(!node.service_retry_due && node.quarantine_attempt == 1u);
    assert(al_node_timer_due_tick(&node.quarantine_timer) ==
           6000u + GS_Q_BACKOFF_2_MS);

    {
        memory_store_t store;
        al_storage_port_t port;
        al_node_engine_t engine;
        al_node_engine_t restored;
        memset(&store, 0, sizeof(store));
        port.context = &store;
        port.read_slot = read_slot;
        port.write_slot = write_slot;
        al_node_engine_init(&engine, uid, port);
        engine.node.addr = 5u;
        engine.node.network_id = UINT64_C(0x0102030405060708);
        engine.node.commission = AL_COMMISSION_PENDING;
        engine.node.network = AL_NETWORK_ATTACHED;
        assert(al_node_timer_start(
            &engine.node.assign_confirm_timer,
            (uint32_t)(100u - GS_TIMING_ASSIGN_CONFIRM_TIMEOUT_MS),
            GS_TIMING_ASSIGN_CONFIRM_TIMEOUT_MS));
        action = al_node_engine_tick(&engine, 100u);
        assert(action.kind == AL_ACTION_STATE_ONLY);
        assert(engine.node.commission == AL_COMMISSION_NONE);
        al_node_engine_init(&restored, uid, port);
        assert(al_node_engine_restore(&restored));
        assert(restored.node.commission == AL_COMMISSION_NONE);
    }

    make_committed(&node, uid);
    node.power = AL_POWER_SLEEPING;
    node.sleep_duration_ms = 1u;
    al_node_wake(&node, 500u);
    assert(node.power == AL_POWER_AWAKE);
    assert(node.network == AL_NETWORK_ATTACHED);
    assert(node.last_own_network_ms == 500u);

    {
        al_master_t master;
        gs_frame_t request;
        al_master_event_t master_event;
        al_master_init(&master, UINT64_C(0x0102030405060708));
        assert(al_master_baud_start(&master, 0u, 19200u));
        master.baud.state = AL_MASTER_BAUD_IDLE;
        master.node_count = 1u;
        memcpy(master.nodes[0].uid, uid, GS_UID_LEN);
        master.nodes[0].addr = 5u;
        master.nodes[0].online = true;
        assert(al_master_make_read_sample(&master, 5u, &request));
        assert(al_master_begin(&master, &request, 5u, 300u));
        assert(al_master_on_tx_complete(&master, 100u));
        master_event = al_master_tick(&master, 400u);
        assert(master_event.kind == AL_MASTER_EVENT_RETRANSMIT);
        assert(master_event.tx_frame.seq == request.seq &&
               master_event.tx_frame.func == request.func);
        assert(al_master_on_tx_complete(&master, 400u));
        master_event = al_master_tick(&master, 700u);
        assert(master_event.kind == AL_MASTER_EVENT_RETRANSMIT);
        assert(master_event.tx_frame.seq == request.seq &&
               master_event.tx_frame.func == request.func);
        assert(al_master_on_tx_complete(&master, 700u));
        assert(al_master_tick(&master, 999u).kind == AL_MASTER_EVENT_NONE);
        master_event = al_master_tick(&master, 1000u);
        assert(master_event.kind == AL_MASTER_EVENT_TIMEOUT);

        assert(al_master_make_sleep(&master, 5u, 1000u, &request));
        assert(al_master_begin(&master, &request, 5u, 300u));
        assert(al_master_on_tx_complete(&master, 600u));
        assert(al_master_take_registry_dirty(&master));
        assert(al_master_on_persist_result(&master, true));
        assert(master.nodes[0].sleep_lease);
        assert(!master.nodes[0].sleep_lease_confirmed);
        assert(master.nodes[0].sleep_state == AL_MASTER_SLEEP_UNCERTAIN);
        assert(master.nodes[0].expected_wake_ms ==
               600u + GS_TIMING_SLEEP_ARMED_WINDOW_MS + 1000u);
        master_event = al_master_tick(&master, 900u);
        assert(master_event.kind == AL_MASTER_EVENT_RETRANSMIT);
        assert(al_master_on_tx_complete(&master, 900u));
        assert(master.nodes[0].expected_wake_ms ==
               600u + GS_TIMING_SLEEP_ARMED_WINDOW_MS + 1000u);
        memset(&request, 0, sizeof(request));
        request.addr = 5u;
        request.seq = master.transaction.request.seq;
        request.func = GS_FUNC_SLEEP;
        request.len = 1u;
        request.data[0] = 0u;
        master_event = al_master_receive_at(&master, &request,
                                            master.transaction.deadline_ms - 1u);
        assert(master_event.kind == AL_MASTER_EVENT_ACCEPTED);
        assert(master_event.persist_registry);
        assert(al_master_on_persist_result(&master, true));
        assert(master.nodes[0].sleep_lease_confirmed);
        assert(master.nodes[0].expected_wake_ms ==
               600u + GS_TIMING_SLEEP_ARMED_WINDOW_MS + 1000u);
        {
            al_master_node_record_t record;
            al_master_t restored_master;
            assert(al_master_export_records(&master, &record, 1u) == 1u);
            al_master_init(&restored_master, master.network_id);
            assert(al_master_restore_records(&restored_master, &record, 1u));
            assert(restored_master.nodes[0].sleep_lease);
            assert(!restored_master.nodes[0].sleep_lease_confirmed);
            assert(restored_master.nodes[0].sleep_state ==
                   AL_MASTER_SLEEP_RECOVERY_VERIFY);
            assert(restored_master.nodes[0].expected_wake_ms == 0u);
            assert(restored_master.nodes[0].sleep_duration_ms == 1000u);
            assert(al_master_tick(&restored_master,
                   GS_TIMING_GRACE_PERIOD_MS).kind == AL_MASTER_EVENT_NONE);
            assert(restored_master.nodes[0].sleep_lease);
        }
    }

    {
        al_master_t master;
        gs_frame_t request;
        gs_frame_t response;
        uint8_t commit_seq;
        al_master_event_t master_event;
        al_master_init(&master, UINT64_C(0x0102030405060708));
        master.node_count = 1u;
        memcpy(master.nodes[0].uid, uid, GS_UID_LEN);
        master.nodes[0].addr = 5u;
        master.nodes[0].network_id = master.network_id;
        master.nodes[0].online = true;

        assert(al_master_baud_start(&master, 3u, 19200u));
        assert(al_master_baud_next(&master, 0u, &request));
        assert(request.func == GS_FUNC_NET_MGMT &&
               request.data[8] == GS_NET_OP_PREPARE);
        assert(al_master_on_tx_complete(&master, 100u));

        assert(al_master_baud_next(&master, 100u, &request));
        assert(request.data[8] == GS_NET_OP_QUERY && request.addr == 5u);
        assert(al_master_on_tx_complete(&master, 110u));
        memset(&response, 0, sizeof(response));
        response.addr = 5u;
        response.seq = request.seq;
        response.func = GS_FUNC_NET_MGMT;
        response.len = 2u;
        response.data[0] = GS_QUERY_READY;
        response.data[1] = 0u;
        assert(al_master_receive_at(&master, &response,
                   master.transaction.deadline_ms - 1u).kind ==
               AL_MASTER_EVENT_ACCEPTED);

        assert(al_master_baud_next(&master, 110u, &request));
        assert(request.data[8] == GS_NET_OP_COMMIT);
        commit_seq = request.seq;
        assert(al_master_on_tx_complete(&master, 120u));
        assert(!al_master_baud_next(&master, 139u, &request));
        assert(al_master_baud_next(&master, 140u, &request));
        assert(request.seq == commit_seq);
        assert(al_master_on_tx_complete(&master, 140u));
        assert(al_master_baud_next(&master, 160u, &request));
        assert(request.seq == commit_seq);
        assert(al_master_on_tx_complete(&master, 160u));
        assert(al_master_tick(&master, 484u).kind == AL_MASTER_EVENT_NONE);
        master_event = al_master_tick(&master, 485u);
        assert(master_event.kind == AL_MASTER_EVENT_PERSIST_REQUIRED);
        assert(master.current_bus_baud == 19200u);
        assert(al_master_on_persist_result(&master, true));
        master_event = al_master_tick(&master, 485u);
        assert(master_event.kind == AL_MASTER_EVENT_SET_BAUD);
        assert(master_event.effect_value == 19200u);

        assert(al_master_baud_next(&master, 485u, &request));
        assert(request.func == GS_FUNC_READ_ADDR);
        assert(al_master_on_tx_complete(&master, 490u));
        memset(&response, 0, sizeof(response));
        response.addr = 5u;
        response.seq = request.seq;
        response.func = GS_FUNC_READ_ADDR;
        response.len = 1u;
        response.data[0] = 5u;
        assert(al_master_receive_at(&master, &response,
                   master.transaction.deadline_ms - 1u).kind ==
               AL_MASTER_EVENT_ACCEPTED);
        assert(!al_master_baud_next(&master, 491u, &request));
        master_event = al_master_tick(&master, 491u);
        assert(master_event.kind == AL_MASTER_EVENT_BAUD_COMPLETE);
    }

    {
        al_master_t master;
        gs_frame_t request;
        al_master_event_t master_event;
        uint8_t missed_before_recovery;
        al_master_init(&master, UINT64_C(0x0102030405060708));
        master.node_count = 1u;
        memcpy(master.nodes[0].uid, uid, GS_UID_LEN);
        master.nodes[0].addr = 5u;
        master.nodes[0].network_id = master.network_id;
        master.nodes[0].online = true;
        master.baud.state = AL_MASTER_BAUD_VERIFY_WAIT;
        master.baud.target_baud = 19200u;
        master.transaction.state = AL_MASTER_WAIT_RESPONSE;
        master.transaction.request.addr = 5u;
        master.transaction.deadline_ms = 100u;
        master.transaction.retry_count = GS_TRANSACTION_RETRY_MAX;
        master_event = al_master_tick(&master, 100u);
        assert(master_event.kind == AL_MASTER_EVENT_BAUD_RECOVERY);
        assert(master_event.effect_value == 9600u);
        missed_before_recovery = master.nodes[0].missed;
        al_master_baud_recovery_applied(&master);
        assert(master.baud.state == AL_MASTER_BAUD_VERIFY_READY);
        assert(al_master_baud_next(&master, 101u, &request));
        assert(request.func == GS_FUNC_READ_ADDR);
        assert(al_master_on_tx_complete(&master, 110u));
        master_event = al_master_tick(&master, 410u);
        assert(master_event.kind == AL_MASTER_EVENT_RETRANSMIT);
        assert(master_event.tx_frame.seq == request.seq);
        assert(al_master_on_tx_complete(&master, 410u));
        master_event = al_master_tick(&master, 710u);
        assert(master_event.kind == AL_MASTER_EVENT_RETRANSMIT);
        assert(master_event.tx_frame.seq == request.seq);
        assert(al_master_on_tx_complete(&master, 710u));
        master_event = al_master_tick(&master, 1010u);
        assert(master_event.kind == AL_MASTER_EVENT_TIMEOUT);
        assert(master.nodes[0].missed == missed_before_recovery &&
               master.nodes[0].online);
        assert(!al_master_baud_next(&master, 1010u, &request));
        master_event = al_master_tick(&master, 1010u);
        assert(master_event.kind == AL_MASTER_EVENT_BAUD_RECOVERY);
        assert(master_event.effect_value == 19200u);
    }

    {
        al_master_t master;
        gs_frame_t request;
        gs_frame_t response;
        gs_chdesc_t trust_descriptor = {1u, GS_QTY_TEMPERATURE,
                                        GS_DESC_DT_INT16,
                                        GS_DESC_UNIT_CELSIUS,
                                        -2, GS_DESC_FLAG_RO};
        gs_sample_t trust_sample;
        al_master_event_t master_event;
        al_master_init(&master, UINT64_C(0x0102030405060708));
        master.node_count = 1u;
        memcpy(master.nodes[0].uid, uid, GS_UID_LEN);
        master.nodes[0].addr = 5u;
        master.nodes[0].network_id = master.network_id;
        master.nodes[0].online = true;

        assert(al_master_make_read_info(&master, 5u, GS_INFO_SUB_BASE,
                                        &request));
        assert(al_master_begin(&master, &request, 5u, 300u));
        assert(al_master_on_tx_complete(&master, 0u));
        memset(&response, 0, sizeof(response));
        response.addr = 5u; response.seq = request.seq;
        response.func = GS_FUNC_READ_INFO; response.len = 15u;
        memcpy(response.data, uid, GS_UID_LEN);
        gs_put_u16be(response.data + 6u, 1u);
        response.data[8] = GS_SENSOR_SHT31;
        response.data[9] = 5u;
        response.data[10] = GS_CAP_CONFIGURABLE;
        gs_put_u16be(response.data + 11u, GS_PROTO_VER);
        gs_put_u16be(response.data + 13u, 7u);
        master_event = al_master_receive_at(&master, &response, 1u);
        assert(master_event.kind == AL_MASTER_EVENT_ACCEPTED);
        assert(master.nodes[0].peer_proto_ver == GS_PROTO_VER);
        assert(master_event.persist_registry);
        assert(al_master_on_persist_result(&master, true));

        assert(al_master_make_read_info(&master, 5u, GS_INFO_SUB_CHDESC,
                                        &request));
        assert(al_master_begin(&master, &request, 5u, 300u));
        assert(al_master_on_tx_complete(&master, 10u));
        memset(&response, 0, sizeof(response));
        response.addr = 5u; response.seq = request.seq;
        response.func = GS_FUNC_READ_INFO;
        response.data[0] = GS_SENSOR_SHT31;
        response.data[1] = 1u;
        gs_put_u16be(response.data + 2u, 7u);
        response.len = (uint8_t)(4u + gs_build_chdesc(response.data + 4u,
                                                      &trust_descriptor));
        master_event = al_master_receive_at(&master, &response, 11u);
        assert(master_event.kind == AL_MASTER_EVENT_ACCEPTED);
        assert(master.nodes[0].descriptor_count == 1u);
        assert(master_event.persist_registry);
        assert(al_master_on_persist_result(&master, true));

        assert(al_master_make_read_sample(&master, 5u, &request));
        assert(al_master_begin(&master, &request, 5u, 300u));
        assert(al_master_on_tx_complete(&master, 20u));
        memset(&trust_sample, 0, sizeof(trust_sample));
        trust_sample.sensor_type = GS_SENSOR_SHT31;
        trust_sample.channel_count = 1u;
        trust_sample.schema_rev = 7u;
        trust_sample.channels[0].id = 1u;
        trust_sample.channels[0].value_len = 2u;
        trust_sample.channels[0].value[0] = 0u;
        trust_sample.channels[0].value[1] = 1u;
        memset(&response, 0, sizeof(response));
        response.addr = 5u; response.seq = request.seq;
        response.func = GS_FUNC_READ_SAMPLE;
        response.len = gs_build_sample_v22(response.data, &trust_sample);
        master_event = al_master_receive_at(&master, &response, 21u);
        assert(master_event.kind == AL_MASTER_EVENT_ACCEPTED);
        assert(master_event.payload_trusted && master.nodes[0].sample_trusted);
    }

    puts("SC2 completion architecture: codec/timers/retry/lease/baud PASS");
    return 0;
}
