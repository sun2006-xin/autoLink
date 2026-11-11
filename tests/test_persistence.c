#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <autolink_node_engine.h>

typedef struct {
    al_persistent_record_t slots[2];
    int fail_write;
    int tear_write;
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
    if (store->fail_write) return false;
    if (store->tear_write) {
        memcpy(&store->slots[slot], data, len / 2u);
        return true;
    }
    memcpy(&store->slots[slot], data, len);
    return true;
}

static void put64(uint8_t *p, uint64_t value)
{
    int i;
    for (i = 7; i >= 0; --i) { p[i] = (uint8_t)value; value >>= 8; }
}

int main(void)
{
    const uint8_t uid[GS_UID_LEN] = {1, 2, 3, 4, 5, 6};
    memory_store_t store;
    al_storage_port_t port;
    al_node_engine_t engine;
    al_node_engine_t restored;
    al_request_t request;
    al_action_t action;
    uint8_t assign[15] = {1, 2, 3, 4, 5, 6, 7};
    memset(&store, 0, sizeof(store));
    port.context = &store;
    port.read_slot = read_slot;
    port.write_slot = write_slot;
    put64(assign + 7u, 0x0102030405060708ULL);

    al_node_engine_init(&engine, uid, port);
    request = (al_request_t){.addr = GS_ADDR_UNASSIGNED, .seq = 1u,
                             .func = GS_FUNC_ASSIGN_ADDR, .len = 15u,
                             .data = assign, .now_ms = 0u,
                             .request_crc_stop =
                                 {AL_BUS_EVENT_RX_CRC_STOP, 1u, 0u,
                                  true}};
    action = al_node_engine_execute(&engine, &request);
    assert(action.kind == AL_ACTION_IMMEDIATE_RESPONSE);
    assert(engine.node.commission == AL_COMMISSION_PENDING);

    al_node_engine_init(&restored, uid, port);
    assert(al_node_engine_restore(&restored));
    assert(restored.node.addr == 7u);
    assert(restored.node.commission == AL_COMMISSION_PENDING);

    store.tear_write = 1;
    assign[6] = 8u;
    request.seq = 2u;
    action = al_node_engine_execute(&engine, &request);
    assert(action.kind == AL_ACTION_ERROR);
    assert(action.wire_error == GS_WIRE_ERR_PERSIST_FAILED);
    assert(engine.node.addr == 7u);

    al_node_engine_init(&restored, uid, port);
    assert(al_node_engine_restore(&restored));
    assert(restored.node.addr == 7u);

    /* A complete newer generation wins, but a later CRC fault makes restore
       fall back to the previous intact generation. */
    store.tear_write = 0;
    assign[6] = 9u;
    request.seq = 3u;
    action = al_node_engine_execute(&engine, &request);
    assert(action.kind == AL_ACTION_IMMEDIATE_RESPONSE);
    assert(engine.node.addr == 9u);
    {
        uint8_t newest = store.slots[1].generation > store.slots[0].generation ? 1u : 0u;
        store.slots[newest].crc ^= 1u;
    }
    al_node_engine_init(&restored, uid, port);
    assert(al_node_engine_restore(&restored));
    assert(restored.node.addr == 7u);
    puts("persistence: atomic commit, torn-write rollback, recovery PASS");
    return 0;
}
