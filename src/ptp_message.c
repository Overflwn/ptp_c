#include "ptp_message.h"

ptp_message_header_t ptp_message_create_header(ptp_message_type_t message_type)
{
    ptp_message_header_t header = {
        .message_type = message_type,
        .transport_specific = 0,
        .version = 2,
        .reserved_1 = 0,
        .message_length = 0,
    };
    switch (message_type)
    {
        case PTP_MESSAGE_TYPE_SYNC:
        header.message_length = sizeof(ptp_message_sync_t);
        break;
        case PTP_MESSAGE_TYPE_FOLLOW_UP:
        header.message_length = sizeof(ptp_message_follow_up_t);
        break;
        case PTP_MESSAGE_TYPE_PDELAY_REQ:
        header.message_length = sizeof(ptp_message_pdelay_req_t);
        break;
        case PTP_MESSAGE_TYPE_PDELAY_RESP:
        header.message_length = sizeof(ptp_message_pdelay_resp_t);
        break;
        case PTP_MESSAGE_TYPE_PDELAY_RESP_FOLLOW_UP:
        header.message_length = sizeof(ptp_message_pdelay_resp_follow_up_t);
        break;
        case PTP_MESSAGE_TYPE_ANNOUNCE:
        header.message_length = sizeof(ptp_message_announce_t);
        break;
        default:
        // TODO: Other message types
        break;
    }
    return header;
}
