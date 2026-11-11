#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "autolink_master.h"
#include "autolink_node_engine.h"

static void test_node_public_projection(void)
{
    al_node_engine_t engine;
    al_node_platform_status_t status;
    al_action_t action;
    al_storage_port_t storage = {0};
    const uint8_t uid[GS_UID_LEN] = {1u, 2u, 3u, 4u, 5u, 6u};

    al_node_engine_init(&engine, uid, storage);
    assert(al_node_engine_platform_status(&engine, &status));
    assert(status.active_baud == GS_BAUD_DEFAULT);
    assert(!al_node_engine_platform_status(NULL, &status));
    assert(!al_node_engine_platform_status(&engine, NULL));

    memset(&action, 0, sizeof(action));
    action.kind = AL_ACTION_STATE_ONLY;
    action.effect = AL_EFFECT_SET_BAUD;
    action.effect_value = GS_BAUD_DEFAULT;
    assert(al_node_engine_effect_is_due(&engine, &action));
    assert(al_node_engine_effect_is_due(&engine, &action) ==
           al_node_action_effect_is_due(&engine.node, &action));
    action.plan.durable_ops = AL_DURABLE_BAUD;
    assert(!al_node_engine_effect_is_due(&engine, &action));
    assert(!al_node_engine_effect_is_due(NULL, &action));
    assert(!al_node_engine_effect_is_due(&engine, NULL));
}

static void test_master_registry_projection(void)
{
    al_master_t master;
    al_master_registry_snapshot_t snapshot;
    al_master_registry_snapshot_t unchanged;
    al_master_node_record_t input;
    al_master_node_record_t output;
    al_master_node_record_t output_before;
    al_master_platform_status_t status;
    const uint64_t network_id = UINT64_C(0x0102030405060708);

    memset(&input, 0, sizeof(input));
    input.uid[0] = 0xA5u;
    input.addr = 7u;
    input.network_id = network_id;
    input.peer_proto_ver = GS_PROTO_VER;

    al_master_init(&master, network_id);
    assert(al_master_restore_records(&master, &input, 1u));
    assert(al_master_restore_bus_baud(&master, 19200u));

    memset(&snapshot, 0xA5, sizeof(snapshot));
    unchanged = snapshot;
    memset(&output, 0x5A, sizeof(output));
    output_before = output;
    assert(al_master_export_registry(&master, &snapshot, &output, 0u) ==
           AL_MASTER_REGISTRY_EXPORT_INSUFFICIENT_CAPACITY);
    assert(memcmp(&snapshot, &unchanged, sizeof(snapshot)) == 0);
    assert(memcmp(&output, &output_before, sizeof(output)) == 0);

    assert(al_master_export_registry(&master, &snapshot, &output, 1u) ==
           AL_MASTER_REGISTRY_EXPORT_OK);
    assert(snapshot.network_id == network_id);
    assert(snapshot.current_bus_baud == 19200u);
    assert(snapshot.bus_baud_known);
    assert(snapshot.node_count == 1u);
    assert(output.uid[0] == input.uid[0]);
    assert(output.addr == input.addr);
    assert(output.network_id == input.network_id);
    assert(al_master_export_registry(NULL, &snapshot, &output, 1u) ==
           AL_MASTER_REGISTRY_EXPORT_INVALID_ARGUMENT);
    assert(al_master_export_registry(&master, NULL, &output, 1u) ==
           AL_MASTER_REGISTRY_EXPORT_INVALID_ARGUMENT);

    assert(al_master_platform_status(&master, &status));
    assert(status.active_baud == 19200u);
    assert(!al_master_platform_status(NULL, &status));
    assert(!al_master_platform_status(&master, NULL));
}

static void test_master_core_owned_transmission(void)
{
    al_master_t master;
    gs_frame_t request;
    gs_frame_t wrong;
    al_master_next_tx_result_t result;

    al_master_init(&master, UINT64_C(0x1122334455667788));
    result = al_master_next_transmission(&master, 0u, 0x10203040u, 8u,
                                         &request);
    assert(result == AL_MASTER_NEXT_TX_READY);
    assert(request.func == GS_FUNC_DISCOVER);
    assert(al_master_tx_authorized(&master, &request));
    wrong = request;
    wrong.seq++;
    assert(!al_master_tx_authorized(&master, &wrong));
    wrong = request;
    wrong.ver++;
    assert(!al_master_tx_authorized(&master, &wrong));
    assert(al_master_next_transmission(&master, 0u, 0u, 8u, &wrong) ==
           AL_MASTER_NEXT_TX_NONE);
    assert(al_master_on_tx_complete(&master, 1u));
    assert(!al_master_tx_authorized(&master, &request));

    al_master_init(&master, UINT64_C(0x1122334455667788));
    assert(al_master_baud_start(&master, 0x31u, 19200u));
    result = al_master_next_transmission(&master, 5u, 0u, 8u, &request);
    assert(result == AL_MASTER_NEXT_TX_READY);
    assert(request.func == GS_FUNC_NET_MGMT);
    assert(request.data[8] == GS_NET_OP_PREPARE);
    assert(al_master_tx_authorized(&master, &request));

    assert(al_master_next_transmission(NULL, 0u, 0u, 8u, &request) ==
           AL_MASTER_NEXT_TX_INVALID);
    assert(al_master_next_transmission(&master, 0u, 0u, 8u, NULL) ==
           AL_MASTER_NEXT_TX_INVALID);
    assert(!al_master_tx_authorized(NULL, &request));
    assert(!al_master_tx_authorized(&master, NULL));
}

int main(void)
{
    test_node_public_projection();
    test_master_registry_projection();
    test_master_core_owned_transmission();
    puts("public integration boundary tests: PASS");
    return 0;
}
