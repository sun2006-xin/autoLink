#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <autolink_core.h>

static const uint8_t UID[GS_UID_LEN] = {1, 2, 3, 4, 5, 6};

static void put_u64be(uint8_t *p, uint64_t value)
{
    int i;
    for (i = 7; i >= 0; --i) { p[i] = (uint8_t)value; value >>= 8; }
}

static al_request_t request(uint8_t addr, uint8_t seq, uint8_t func,
                            const uint8_t *data, uint8_t len, uint32_t now_ms)
{
    al_request_t r = {.addr = addr, .seq = seq, .func = func, .len = len,
                      .data = data, .now_ms = now_ms,
                      .request_crc_stop =
                          {AL_BUS_EVENT_RX_CRC_STOP,
                           (al_time_us_t)now_ms * UINT64_C(1000), 0u, true}};
    return r;
}

int main(void)
{
    al_node_t n;
    al_action_t a;
    uint8_t assign[15] = {1, 2, 3, 4, 5, 6, 7};
    uint8_t discover[13] = {0, 0, 0, 1, 32};
    al_request_t r;
    put_u64be(assign + 7, 0x0102030405060708ULL);
    put_u64be(discover + 5, 0x0102030405060708ULL);

    al_node_init(&n, UID);
    assert(al_node_invariants(&n));
    r = request(GS_ADDR_UNASSIGNED, 1, GS_FUNC_ASSIGN_ADDR, assign, 15, 0);
    a = al_node_execute(&n, &r);
    assert(a.kind == AL_ACTION_IMMEDIATE_RESPONSE);
    assert((a.plan.durable_ops & AL_DURABLE_IDENTITY_PENDING) != 0u);
    assert(n.commission == AL_COMMISSION_PENDING && n.addr == 7);

    {
        uint8_t wrong_query[10] = {0};
        wrong_query[8] = GS_NET_OP_QUERY;
        r = request(7, 2, GS_FUNC_NET_MGMT, wrong_query, 10, 1);
        a = al_node_execute(&n, &r);
        assert(a.kind == AL_ACTION_ERROR && a.wire_error == GS_WIRE_ERR_INVALID_VALUE);
        assert(n.commission == AL_COMMISSION_PENDING);
    }

    r = request(7, 2, GS_FUNC_WRITE_CFG, NULL, 0, 1);
    a = al_node_execute(&n, &r);
    assert(a.kind == AL_ACTION_ERROR);
    assert(n.commission == AL_COMMISSION_PENDING);

    r = request(7, 3, GS_FUNC_READ_ADDR, NULL, 0, 2);
    a = al_node_execute(&n, &r);
    assert(a.kind == AL_ACTION_IMMEDIATE_RESPONSE);
    assert((a.plan.durable_ops & AL_DURABLE_COMMISSION_COMMITTED) != 0u);
    assert(n.commission == AL_COMMISSION_COMMITTED);

    n.administrative = AL_ADMIN_RETIRED;
    r = request(GS_ADDR_BROADCAST, 3, GS_FUNC_DISCOVER, discover, 13, 20000);
    a = al_node_execute(&n, &r);
    assert(a.kind == AL_ACTION_IGNORE && n.commission == AL_COMMISSION_COMMITTED);

    n.administrative = AL_ADMIN_ACTIVE;
    n.power = AL_POWER_SLEEPING;
    n.sleep_duration_ms = 1u;
    a = al_node_execute(&n, &r);
    assert(a.kind == AL_ACTION_IGNORE && n.commission == AL_COMMISSION_COMMITTED);

    n.power = AL_POWER_AWAKE;
    n.sleep_duration_ms = 0u;
    al_enter_quarantine(&n, AL_SERVICE_QUARANTINED_SERVICE);
    al_quarantine_expire(&n);
    assert(n.service == AL_SERVICE_QUARANTINED_SERVICE && n.service_retry_due);
    r.func = GS_FUNC_RECONCILE;
    a = al_node_execute(&n, &r);
    assert(a.kind == AL_ACTION_SCHEDULED_RESPONSE);
    (void)al_node_on_delivery_at(&n, &a, AL_DELIVERY_DELIVERED, 1u);
    assert(n.service == AL_SERVICE_NORMAL && !n.service_retry_due);

    put_u64be(discover + 5, 0x1112131415161718ULL);
    r = request(GS_ADDR_BROADCAST, 9, GS_FUNC_DISCOVER, discover, 13, 40000);
    a = al_node_execute(&n, &r);
    assert(a.kind == AL_ACTION_STATE_ONLY && n.addr == 7);
    r.seq = 10;
    a = al_node_execute(&n, &r);
    assert(a.kind == AL_ACTION_STATE_ONLY);
    assert((a.plan.durable_ops & AL_DURABLE_IDENTITY_CLEAR) != 0u);
    assert(n.addr == GS_ADDR_UNASSIGNED && n.network_id == 0u &&
           n.commission == AL_COMMISSION_NONE);
    assert(al_node_invariants(&n));

    puts("AutoLinkCore C tests: 22 assertions passed");
    return 0;
}
