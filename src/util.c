#ifdef __cplusplus__
extern "C" {
#endif

#include "util.h"

ptp_message_header_t
ptp_message_create_header(timesync_clock_t *instance,
                          ptp_message_type_t message_type) {
  ptp_message_header_t header = {
      .message_type = message_type,
      .major_sdo_id = instance->major_sdo_id,
      .version = 2,
      // TODO: Adjust minor version later
      .minor_version = 0,
      .message_length = 0,
      .domain_num = instance->domain_id,
      .minor_sdo_id = instance->minor_sdo_id,
      // Flags see below
      .correction_field = 0,
      .message_type_specific = 0,
      .source_port_identity = instance->source_port_identity,
  };
  header.flags.raw_val = 0;
  header.flags.utc_offset_valid = 1;
  switch (message_type) {
  case PTP_MESSAGE_TYPE_SYNC:
    header.message_length = htobe16(sizeof(ptp_message_sync_t));
    header.control_field = 0x00;
    header.flags.two_step = 1;
    break;
  case PTP_MESSAGE_TYPE_DELAY_REQ:
    header.message_length = htobe16(sizeof(ptp_message_delay_req_t));
    header.control_field = 0x01;
    break;
  case PTP_MESSAGE_TYPE_FOLLOW_UP:
    header.message_length = htobe16(sizeof(ptp_message_follow_up_t));
    header.control_field = 0x02;
    break;
  case PTP_MESSAGE_TYPE_DELAY_RESP:
    header.message_length = htobe16(sizeof(ptp_message_delay_resp_t));
    header.control_field = 0x03;
    break;
  // TODO: Management
  case PTP_MESSAGE_TYPE_PDELAY_REQ:
    header.message_length = htobe16(sizeof(ptp_message_pdelay_req_t));
    header.control_field = 0x05;
    break;
  case PTP_MESSAGE_TYPE_PDELAY_RESP:
    header.message_length = htobe16(sizeof(ptp_message_pdelay_resp_t));
    header.control_field = 0x05;
    header.flags.two_step = 1;
    break;
  case PTP_MESSAGE_TYPE_PDELAY_RESP_FOLLOW_UP:
    header.message_length =
        htobe16(sizeof(ptp_message_pdelay_resp_follow_up_t));
    header.control_field = 0x05;
    break;
  case PTP_MESSAGE_TYPE_ANNOUNCE:
    header.message_length = htobe16(sizeof(ptp_message_announce_t));
    header.control_field = 0x05;
    break;
  default:
    // TODO: Other message types
    break;
  }
  return header;
}

#ifdef __cplusplus__
}
#endif
