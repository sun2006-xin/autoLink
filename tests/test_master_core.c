#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <autolink_master.h>

int main(void)
{
    const uint8_t uid[GS_UID_LEN] = {1, 2, 3, 4, 5, 6};
    al_master_t master;
    gs_frame_t request;
    gs_frame_t response;
    al_master_event_t event;
    al_master_init(&master, 0x0102030405060708ULL);

    assert(al_master_make_assign(&master, uid, 7u, &request));
    assert(al_master_begin(&master, &request, GS_ADDR_UNASSIGNED, 300u));
    assert(al_master_on_tx_complete(&master, 0u));
    memset(&response, 0, sizeof(response));
    response.addr = GS_ADDR_UNASSIGNED;
    response.seq = request.seq;
    response.func = GS_FUNC_REG_CONFIRM;
    response.len = request.len;
    memcpy(response.data, request.data, request.len);
    event = al_master_receive_at(&master, &response,
                                 master.transaction.deadline_ms - 1u);
    assert(event.kind == AL_MASTER_EVENT_REGISTRY_CHANGED);
    assert(event.persist_registry && master.node_count == 1u);
    assert(master.nodes[0].addr == 7u);
    assert(al_master_on_persist_result(&master, true));

    assert(al_master_make_read_sample(&master, 7u, &request));
    assert(al_master_begin(&master, &request, 7u, 300u));
    assert(al_master_on_tx_complete(&master, 100u));
    memset(&response, 0, sizeof(response));
    response.addr = 8u;
    response.seq = request.seq;
    response.func = request.func;
    assert(al_master_receive_at(&master, &response,
               master.transaction.deadline_ms - 1u).kind == AL_MASTER_EVENT_REJECTED);
    response.addr = 7u;
    response.len = 4u; /* truncated V2.2 sample: must not terminate transaction */
    assert(al_master_receive_at(&master, &response,
               master.transaction.deadline_ms - 1u).kind == AL_MASTER_EVENT_REJECTED);
    assert(master.transaction.state == AL_MASTER_WAIT_RESPONSE);
    response.data[0] = GS_SENSOR_SHT31;
    response.data[1] = 0u;
    response.data[2] = 0u;
    response.data[3] = 1u;
    response.data[4] = 0u;
    response.len = 5u;
    assert(al_master_receive_at(&master, &response,
               master.transaction.deadline_ms - 1u).kind == AL_MASTER_EVENT_ACCEPTED);

    assert(al_master_make_net_management(&master, 7u, GS_NET_OP_QUERY,
                                         3u, 0u, &request));
    assert(al_master_begin(&master, &request, 7u, 300u));
    assert(al_master_on_tx_complete(&master, 500u));
    response.addr = 7u;
    response.seq = request.seq;
    response.func = (uint8_t)(GS_FUNC_ERR_BASE | GS_FUNC_NET_MGMT);
    response.len = 1u;
    response.data[0] = GS_WIRE_ERR_TXN_CONFLICT;
    event = al_master_receive_at(&master, &response,
                                 master.transaction.deadline_ms - 1u);
    assert(event.kind == AL_MASTER_EVENT_ACCEPTED);
    assert(event.wire_error == GS_WIRE_ERR_TXN_CONFLICT);

    assert(al_master_make_read_sample(&master, 7u, &request));
    assert(al_master_begin(&master, &request, 7u, 300u));
    assert(al_master_on_tx_complete(&master, 1000u));
    event = al_master_tick(&master, 1300u);
    assert(event.kind == AL_MASTER_EVENT_RETRANSMIT);
    assert(event.tx_frame.seq == request.seq);
    assert(al_master_on_tx_complete(&master, 1300u));
    event = al_master_tick(&master, 1600u);
    assert(event.kind == AL_MASTER_EVENT_RETRANSMIT);
    assert(event.tx_frame.seq == request.seq);
    assert(al_master_on_tx_complete(&master, 1600u));
    event = al_master_tick(&master, 1900u);
    assert(event.kind == AL_MASTER_EVENT_TIMEOUT);
    assert(master.nodes[0].missed == 1u);

    assert(al_master_make_read_sample(&master, 7u, &request));
    assert(al_master_begin(&master, &request, 7u, 300u));
    assert(al_master_on_tx_complete(&master, 2000u));
    memset(&response, 0, sizeof(response));
    response.addr = 7u;
    response.seq = request.seq;
    response.func = request.func;
    response.len = 5u;
    response.data[0] = GS_SENSOR_SHT31;
    response.data[1] = 0u;
    response.data[2] = 0u;
    response.data[3] = 1u;
    response.data[4] = 0u;
    event = al_master_receive_at(&master, &response, 2300u);
    assert(event.kind == AL_MASTER_EVENT_RETRANSMIT);
    assert(master.transaction.awaiting_tx_complete);
    assert(master.nodes[0].missed == 1u);
    puts("master core: request/response/registry/timeout/rx-deadline PASS");
    return 0;
}
