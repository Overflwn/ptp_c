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
    return header;
}
