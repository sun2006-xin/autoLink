#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <autolink_master.h>
#include <autolink_node_engine.h>
#include <generated/autolink_semantics.h>

static const uint8_t UID[GS_UID_LEN] = {1u, 2u, 3u, 4u, 5u, 6u};
static const uint64_t NET = UINT64_C(0x0102030405060708);

typedef struct {
    al_persistent_record_t slots[2];
    bool fail_write;
} store_t;

typedef struct {
    al_device_info_t info;
    gs_sample_t sample;
} application_t;

static bool read_slot(void *context, uint8_t slot, void *data, size_t len)
{
    store_t *store = (store_t *)context;
    memcpy(data, &store->slots[slot], len);
    return true;
}

static bool write_slot(void *context, uint8_t slot, const void *data,
                       size_t len)
{
    store_t *store = (store_t *)context;
    if (store->fail_write) return false;
    memcpy(&store->slots[slot], data, len);
    return true;
}

static bool read_info(void *context, al_device_info_t *info)
{
    *info = ((application_t *)context)->info;
    return true;
}

static bool read_sample(void *context, gs_sample_t *sample)
{
    *sample = ((application_t *)context)->sample;
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

static al_request_t request(uint8_t addr, uint8_t seq, uint8_t func,
                            const uint8_t *data, uint8_t len,
                            uint32_t now_ms, al_time_us_t crc_stop_us)
{
    al_request_t value = {.addr = addr, .seq = seq, .func = func,
                          .len = len, .data = data, .now_ms = now_ms,
                          .request_crc_stop =
                              {AL_BUS_EVENT_RX_CRC_STOP, crc_stop_us,
                               0u, true}};
    return value;
}

static void committed(al_node_t *node)
{
    al_node_init(node, UID);
    node->addr = 5u;
    node->network_id = NET;
    node->commission = AL_COMMISSION_COMMITTED;
    node->network = AL_NETWORK_ATTACHED;
}

static al_action_t collect_action(uint8_t func, uint32_t seed, uint8_t slots,
                                  al_time_us_t anchor)
{
    uint8_t data[14] = {0u};
    uint8_t len;
    al_node_t node;
    al_request_t input;
    if (func == GS_FUNC_RECOVER) {
        put_u64be(data, NET);
        data[8] = GS_REPORT_REGISTER;
        gs_put_u32be(data + 9u, seed);
        data[13] = slots;
        len = 14u;
    } else {
        gs_put_u32be(data, seed);
        data[4] = slots;
        put_u64be(data + 5u, NET);
        len = 13u;
    }
    if (func == GS_FUNC_DISCOVER) al_node_init(&node, UID);
    else committed(&node);
    input = request(GS_ADDR_BROADCAST, 1u, func, data, len,
                    (uint32_t)(anchor / 1000u), anchor);
    return al_node_execute(&node, &input);
}

static void test_collect_equation(void)
{
    const uint8_t funcs[] = {GS_FUNC_DISCOVER, GS_FUNC_RECONCILE,
                             GS_FUNC_RECOVER};
    const uint32_t intervals[] = {GS_TIMING_T_SLOT_DISCOVER_MS,
                                  GS_TIMING_T_SLOT_RECONCILE_MS,
                                  GS_TIMING_T_SLOT_RECOVER_MS};
    const al_time_us_t anchor = UINT64_C(1234567);
    size_t f;
    for (f = 0u; f < sizeof(funcs); ++f) {
        uint32_t seed;
        bool saw_zero = false;
        bool saw_nonzero = false;
        bool saw_max = false;
        for (seed = 0u; seed < 200000u && !saw_max; ++seed) {
            al_action_t action = collect_action(funcs[f], seed, 64u, anchor);
            al_time_us_t expected;
            assert(action.kind == AL_ACTION_SCHEDULED_RESPONSE);
            expected = anchor +
                (al_time_us_t)GS_TIMING_T_IFG_MS * UINT64_C(1000) +
                (al_time_us_t)action.slot * intervals[f] * UINT64_C(1000);
            assert(action.scheduled_start_bit_us == expected);
            assert(action.scheduled_start_accuracy_us ==
                   AL_SLOT_ACCURACY_US);
            assert(action.effect_due_ms == (uint32_t)(anchor / 1000u) +
                   GS_TIMING_T_IFG_MS + action.slot * intervals[f]);
            if (action.slot == 0u) {
                assert(action.scheduled_start_bit_us - anchor ==
                       GS_TIMING_T_IFG_MS * UINT64_C(1000));
                saw_zero = true;
            } else {
                saw_nonzero = true;
            }
            if (action.slot == 63u) saw_max = true;
        }
        assert(saw_zero && saw_nonzero && saw_max);
        {
            al_action_t boundary = collect_action(funcs[f], seed, 8u, anchor);
            assert(!al_transport_target_due(
                boundary.scheduled_start_bit_us - 1u,
                boundary.scheduled_start_bit_us));
            assert(al_transport_target_due(boundary.scheduled_start_bit_us,
                                           boundary.scheduled_start_bit_us));
            assert(al_transport_target_due(
                boundary.scheduled_start_bit_us + 1u,
                boundary.scheduled_start_bit_us));
        }
    }
    assert(AL_SLOT_ACCURACY_US == 500u);
    assert(AL_TWO_NODE_RELATIVE_BOUND_US == 1000u);
    {
        al_transport_timestamp_t first = al_transport_timestamp(
            AL_BUS_EVENT_TX_VER_START, anchor, 500u, true);
        al_transport_timestamp_t second = al_transport_timestamp(
            AL_BUS_EVENT_TX_VER_START, anchor, 500u, true);
        assert(al_transport_meets_bound(&first, AL_SLOT_ACCURACY_US));
        assert(al_transport_pair_meets_relative_bound(
            &first, &second, AL_TWO_NODE_RELATIVE_BOUND_US));
        second.verified = false;
        assert(!al_transport_pair_meets_relative_bound(
            &first, &second, AL_TWO_NODE_RELATIVE_BOUND_US));
    }
}

static void make_descriptor(application_t *application, uint8_t unit)
{
    memset(application, 0, sizeof(*application));
    application->info.firmware_version = 0x0102u;
    application->info.sensor_type = GS_SENSOR_SHT31;
    application->info.capabilities = GS_CAP_CONFIGURABLE;
    application->info.descriptor_count = 1u;
    application->info.descriptors[0].ch_id = GS_CH_TEMP;
    application->info.descriptors[0].quantity_id = GS_QTY_TEMPERATURE;
    application->info.descriptors[0].data_type = GS_DESC_DT_INT16;
    application->info.descriptors[0].unit = unit;
    application->info.descriptors[0].exp = -2;
    application->info.descriptors[0].flags = GS_DESC_FLAG_RO;
    application->sample.sensor_type = GS_SENSOR_SHT31;
    application->sample.channel_count = 1u;
    application->sample.channels[0].id = GS_CH_TEMP;
    application->sample.channels[0].value_len = 2u;
}

static void test_schema_owner(void)
{
    store_t store;
    application_t application;
    al_storage_port_t storage;
    al_node_application_port_t port;
    al_node_engine_t engine;
    al_node_engine_t rebooted;
    uint16_t revision_a;
    uint32_t signature_a;
    memset(&store, 0, sizeof(store));
    storage.context = &store;
    storage.read_slot = read_slot;
    storage.write_slot = write_slot;
    make_descriptor(&application, GS_DESC_UNIT_CELSIUS);
    port.context = &application;
    port.read_sample = read_sample;
    port.read_device_info = read_info;
    al_node_engine_init(&engine, UID, storage);
    al_node_engine_set_application_port(&engine, port);
    assert(al_node_engine_sync_schema(&engine));
    revision_a = engine.node.schema_rev;
    signature_a = engine.node.descriptor_signature;
    assert(revision_a == 1u);
    assert(al_node_engine_sync_schema(&engine));
    assert(engine.node.schema_rev == revision_a);

    application.info.descriptors[0].unit = GS_DESC_UNIT_VOLT;
    store.fail_write = true;
    assert(!al_node_engine_sync_schema(&engine));
    assert(engine.node.schema_rev == revision_a);
    assert(engine.node.descriptor_signature == signature_a);
    assert(engine.schema_info.descriptors[0].unit == GS_DESC_UNIT_CELSIUS);
    store.fail_write = false;
    assert(al_node_engine_sync_schema(&engine));
    assert(engine.node.schema_rev == (uint16_t)(revision_a + 1u));
    assert(engine.node.descriptor_signature != signature_a);

    al_node_engine_init(&rebooted, UID, storage);
    assert(al_node_engine_restore(&rebooted));
    al_node_engine_set_application_port(&rebooted, port);
    assert(al_node_engine_sync_schema(&rebooted));
    assert(rebooted.node.schema_rev == engine.node.schema_rev);
    assert(rebooted.node.descriptor_signature ==
           engine.node.descriptor_signature);

    /* Powered-off replacement: durable A is restored, then boot observes B. */
    memset(&store, 0, sizeof(store));
    make_descriptor(&application, GS_DESC_UNIT_CELSIUS);
    al_node_engine_init(&engine, UID, storage);
    al_node_engine_set_application_port(&engine, port);
    assert(al_node_engine_sync_schema(&engine));
    al_node_engine_init(&rebooted, UID, storage);
    assert(al_node_engine_restore(&rebooted));
    application.info.descriptors[0].unit = GS_DESC_UNIT_VOLT;
    al_node_engine_set_application_port(&rebooted, port);
    assert(al_node_engine_sync_schema(&rebooted));
    assert(rebooted.node.schema_rev == 2u);
}

static void add_master_node(al_master_t *master)
{
    master->node_count = 1u;
    memcpy(master->nodes[0].uid, UID, GS_UID_LEN);
    master->nodes[0].addr = 5u;
    master->nodes[0].network_id = NET;
    master->nodes[0].online = true;
}

static void test_sleep_and_persistence(void)
{
    al_master_t master;
    al_master_t rebooted;
    al_master_node_record_t record;
    gs_frame_t frame;
    gs_frame_t response;
    al_master_event_t event;
    al_master_time_ms_t expected;
    al_master_init(&master, NET);
    add_master_node(&master);
    assert(al_master_make_sleep(&master, 5u, 86400000u, &frame));
    assert(al_master_begin(&master, &frame, 5u, 300u));
    assert(al_master_on_tx_complete(&master, 100u));
    expected = master.nodes[0].expected_wake_ms;
    assert(master.nodes[0].sleep_state == AL_MASTER_SLEEP_UNCERTAIN);
    assert(al_master_on_persist_result(&master, false));
    assert(master.nodes[0].sleep_state == AL_MASTER_SLEEP_UNCERTAIN);
    event = al_master_tick(&master, 400u);
    assert(event.kind == AL_MASTER_EVENT_RETRANSMIT);
    assert(event.tx_frame.seq == frame.seq);
    assert(al_master_on_tx_complete(&master, 400u));
    assert(master.nodes[0].expected_wake_ms == expected);
    assert(al_master_on_persist_result(&master, true));

    memset(&response, 0, sizeof(response));
    response.addr = 5u;
    response.seq = frame.seq;
    response.func = GS_FUNC_SLEEP;
    response.len = 1u;
    event = al_master_receive_at(&master, &response,
                                 master.transaction.deadline_ms - 1u);
    assert(event.kind == AL_MASTER_EVENT_ACCEPTED && event.persist_registry);
    assert(master.nodes[0].sleep_state == AL_MASTER_SLEEP_CONFIRMED);
    assert(master.nodes[0].expected_wake_ms == expected);
    assert(al_master_on_persist_result(&master, true));
    assert(al_master_export_records(&master, &record, 1u) == 1u);

    al_master_init(&rebooted, NET);
    assert(al_master_restore_records(&rebooted, &record, 1u));
    assert(rebooted.nodes[0].sleep_state == AL_MASTER_SLEEP_RECOVERY_VERIFY);
    assert(rebooted.nodes[0].sleep_lease);
    assert(rebooted.nodes[0].expected_wake_ms == 0u);
    assert(al_master_tick(&rebooted, 15000u).kind == AL_MASTER_EVENT_NONE);
    assert(rebooted.nodes[0].sleep_lease);
    assert(al_master_make_read_addr(&rebooted, 5u, &frame));
    assert(al_master_begin(&rebooted, &frame, 5u, 300u));
    assert(al_master_on_tx_complete(&rebooted, 15001u));
    memset(&response, 0, sizeof(response));
    response.addr = 5u; response.seq = frame.seq;
    response.func = GS_FUNC_READ_ADDR; response.len = 1u;
    response.data[0] = 5u;
    event = al_master_receive_at(&rebooted, &response, 15002u);
    assert(event.kind == AL_MASTER_EVENT_ACCEPTED && event.persist_registry);
    assert(rebooted.nodes[0].sleep_state == AL_MASTER_SLEEP_NONE);
    assert(al_master_on_persist_result(&rebooted, true));
}

static void test_baud_persistence_order(void)
{
    al_master_t master;
    al_master_event_t event;
    al_master_init(&master, NET);
    master.baud.state = AL_MASTER_BAUD_SILENCE;
    master.baud.target_baud = 57600u;
    master.baud.deadline_ms = 10u;
    event = al_master_tick(&master, 10u);
    assert(event.kind == AL_MASTER_EVENT_PERSIST_REQUIRED);
    assert(master.current_bus_baud == 57600u);
    assert(al_master_on_persist_result(&master, false));
    assert(master.current_bus_baud == GS_BAUD_DEFAULT);
    assert(!master.bus_baud_known);
    event = al_master_tick(&master, 11u);
    assert(event.kind == AL_MASTER_EVENT_BAUD_RECOVERY);
    assert(event.effect_value == al_supported_baud_at(0u));

    {
        const uint32_t targets[] = {57600u, 115200u};
        size_t i;
        for (i = 0u; i < sizeof(targets) / sizeof(targets[0]); ++i) {
            al_master_init(&master, NET);
            master.baud.state = AL_MASTER_BAUD_SILENCE;
            master.baud.target_baud = targets[i];
            master.baud.deadline_ms = 10u;
            assert(al_master_tick(&master, 10u).kind ==
                   AL_MASTER_EVENT_PERSIST_REQUIRED);
            assert(al_master_on_persist_result(&master, true));
            event = al_master_tick(&master, 10u);
            assert(event.kind == AL_MASTER_EVENT_SET_BAUD);
            assert(event.effect_value == targets[i] && master.bus_baud_known);
        }
    }
    {
        al_master_t restored;
        al_master_init(&restored, NET);
        assert(al_master_restore_bus_baud(&restored,
                                         master.current_bus_baud));
        assert(restored.current_bus_baud == 115200u);
        assert(restored.baud.state == AL_MASTER_BAUD_IDLE);
        assert(!al_master_restore_bus_baud(&restored, 38400u));
        add_master_node(&restored);
        event = al_master_tick(&restored, 0u);
        assert(event.kind == AL_MASTER_EVENT_BAUD_RECOVERY);
        assert(event.effect_value == al_supported_baud_at(0u));
        al_master_baud_recovery_applied(&restored);
        assert(al_master_baud_next(&restored, 1u, &(gs_frame_t){0}));
        assert(restored.transaction.request.func == GS_FUNC_READ_ADDR);
    }
}

static void test_registry_failure_rolls_back(void)
{
    al_master_t master;
    gs_frame_t request_frame;
    gs_frame_t response;
    al_master_event_t event;
    al_master_init(&master, NET);
    assert(al_master_make_assign(&master, UID, 5u, &request_frame));
    assert(al_master_begin(&master, &request_frame, GS_ADDR_UNASSIGNED, 300u));
    assert(al_master_on_tx_complete(&master, 0u));
    memset(&response, 0, sizeof(response));
    response.addr = GS_ADDR_UNASSIGNED;
    response.seq = request_frame.seq;
    response.func = GS_FUNC_REG_CONFIRM;
    response.len = request_frame.len;
    memcpy(response.data, request_frame.data, request_frame.len);
    event = al_master_receive_at(&master, &response, 1u);
    assert(event.kind == AL_MASTER_EVENT_REGISTRY_CHANGED);
    assert(master.node_count == 1u);
    assert(al_master_on_persist_result(&master, false));
    assert(master.node_count == 0u);
}

static void test_master_schema_refetch(void)
{
    al_master_t master;
    gs_frame_t request_frame;
    gs_frame_t response;
    gs_sample_t sample;
    gs_chdesc_t descriptor;
    al_master_event_t event;
    al_master_init(&master, NET);
    add_master_node(&master);
    master.nodes[0].peer_proto_ver = GS_PROTO_VER;
    master.nodes[0].sensor_type = GS_SENSOR_SHT31;
    master.nodes[0].schema_rev = 1u;
    master.nodes[0].descriptor_count = 1u;
    master.nodes[0].descriptors[0].ch_id = GS_CH_TEMP;
    master.nodes[0].descriptors[0].data_type = GS_DESC_DT_INT16;
    memset(&sample, 0, sizeof(sample));
    sample.sensor_type = GS_SENSOR_SHT31;
    sample.channel_count = 1u;
    sample.schema_rev = 2u;
    sample.channels[0].id = GS_CH_TEMP;
    sample.channels[0].value_len = 2u;
    assert(al_master_make_read_sample(&master, 5u, &request_frame));
    assert(al_master_begin(&master, &request_frame, 5u, 300u));
    assert(al_master_on_tx_complete(&master, 0u));
    memset(&response, 0, sizeof(response));
    response.addr = 5u; response.seq = request_frame.seq;
    response.func = GS_FUNC_READ_SAMPLE;
    response.len = gs_build_sample_v22(response.data, &sample);
    event = al_master_receive_at(&master, &response, 1u);
    assert(event.kind == AL_MASTER_EVENT_ACCEPTED);
    assert(!event.payload_trusted);

    assert(al_master_make_read_info(&master, 5u, GS_INFO_SUB_CHDESC,
                                    &request_frame));
    assert(al_master_begin(&master, &request_frame, 5u, 300u));
    assert(al_master_on_tx_complete(&master, 2u));
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.ch_id = GS_CH_TEMP;
    descriptor.quantity_id = GS_QTY_TEMPERATURE;
    descriptor.data_type = GS_DESC_DT_INT16;
    descriptor.unit = GS_DESC_UNIT_VOLT;
    descriptor.exp = -2;
    descriptor.flags = GS_DESC_FLAG_RO;
    memset(&response, 0, sizeof(response));
    response.addr = 5u; response.seq = request_frame.seq;
    response.func = GS_FUNC_READ_INFO;
    response.data[0] = GS_SENSOR_SHT31;
    response.data[1] = 1u;
    gs_put_u16be(response.data + 2u, 2u);
    response.len = (uint8_t)(4u + gs_build_chdesc(response.data + 4u,
                                                  &descriptor));
    event = al_master_receive_at(&master, &response, 3u);
    assert(event.kind == AL_MASTER_EVENT_ACCEPTED && event.persist_registry);
    assert(al_master_on_persist_result(&master, true));

    assert(al_master_make_read_sample(&master, 5u, &request_frame));
    assert(al_master_begin(&master, &request_frame, 5u, 300u));
    assert(al_master_on_tx_complete(&master, 4u));
    response.addr = 5u; response.seq = request_frame.seq;
    response.func = GS_FUNC_READ_SAMPLE;
    response.len = gs_build_sample_v22(response.data, &sample);
    event = al_master_receive_at(&master, &response, 5u);
    assert(event.kind == AL_MASTER_EVENT_ACCEPTED);
    assert(event.payload_trusted);
}

int main(void)
{
    test_collect_equation();
    test_schema_owner();
    test_sleep_and_persistence();
    test_baud_persistence_order();
    test_registry_failure_rolls_back();
    test_master_schema_refetch();
    puts("SC2 root-cause closure: RC-U1..U6 adversarial histories PASS");
    return 0;
}
