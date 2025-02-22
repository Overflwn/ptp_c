#include "ptp_control.h"
#include "ptp_message.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Headers for hton / ntoh
#ifdef _WIN32
#include <winsock.h>
#define htobe64(x) htonll(x)
#define htobe32(x) htonl(x)
#define be64toh(x) ntohll(x)
#define be32toh(x) ntohl(x)
#elif defined(__linux__)
// NOTE: My guess is that most microcontroller SDKs have some sort of support
// for these headers aswell
// TODO: Try compiling this for ESP
#include <arpa/inet.h>
#include <endian.h>
#elif defined(__APPLE__)
#include <arpa/inet.h>
#include <machine/endian.h>
#define htobe64(x) htonll(x)
#define htobe32(x) htonl(x)
#define be64toh(x) ntohll(x)
#define be32toh(x) ntohl(x)
#endif

/// @brief Searches for an existing delay_info entry for the given id
/// @param[in] instance The PTP clock instance
/// @param[in] id The ID to search for
/// @return Pointer to the delay_info, NULL if not found
static ptp_delay_info_entry_t *
search_delay_info(const timesync_clock_t *instance, const uint64_t id) {
  ptp_delay_info_entry_t *temp = instance->delay_infos;
  while (temp != NULL) {
    if (temp->delay_info.peer_id == id) {
      break;
    } else {
      temp = temp->next;
    }
  }
  return temp;
}

/// @brief Find delay info for the given client or create a new instance and
///        append it to the list
/// @param[in] instance The PTP clock instance
/// @param[in] The ID to look for or create a new delay_info entry for
/// @return Pointer to the new or existing delay_info entry
static ptp_delay_info_entry_t *get_or_new_delay_info(timesync_clock_t *instance,
                                                     const uint64_t id) {
  ptp_delay_info_entry_t *temp = search_delay_info(instance, id);
  if (temp == NULL) {
    temp = calloc(1, sizeof(ptp_delay_info_entry_t));
    temp->delay_info.peer_id = id;
    ptp_delay_info_entry_t *last = instance->delay_infos;
    if (last == NULL) {
      instance->delay_infos = temp;
    } else {
      while (last->next != NULL) {
        last = last->next;
      }
      last->next = temp;
      temp->previous = last;
    }
  }
  return temp;
}

/// @brief Searches for a delay_info entry with the given ID and removes it from
///        the PTP clock instance
/// @param[in] instance The PTP clock instance
/// @param[in] id The ID to look for and delete
static void remove_delay_info(timesync_clock_t *instance, const uint64_t id) {
  ptp_delay_info_entry_t *entry = search_delay_info(instance, id);
  if (entry != NULL) {
    entry->previous->next = entry->next;
    entry->next->previous = entry->previous;
    free(entry);
  }
}

/// @brief Simple function that builds a key for our delay_info list
/// @param[in] port_ident Port identity of peer
/// @return The calculated ID or 0 on error
static uint64_t
port_identity_to_id(const ptp_message_port_identity_t *port_ident) {
  uint64_t id = 0;
  if (NULL == port_ident) {
    return id;
  }
  id |= port_ident->clock_identity[0];
  id |= (uint64_t)port_ident->clock_identity[1] << 8;
  id |= (uint64_t)port_ident->clock_identity[2] << 16;
  id |= (uint64_t)port_ident->clock_identity[3] << 24;
  id |= (uint64_t)port_ident->clock_identity[4] << 32;
  id |= (uint64_t)port_ident->clock_identity[5] << 40;
  id |= ((uint64_t)port_ident->port_number & 0x00FF) << 48;
  id |= ((uint64_t)port_ident->port_number & 0xFF00) << 56;
  return id;
}

/// @brief Helper function to convert a ptp_message_timestamp_t to 64bit
///        nanoseconds value
/// TODO: Move this function (and probably others aswell) to a utils file
/// @param[in] ts The timestamp to convert
/// @return The converted timestamp or 0 on error
static uint64_t ts_to_ns(const ptp_message_timestamp_t *ts) {
  if (NULL == ts) {
    return 0;
  }
  uint64_t temp = 0;
  uint64_t temp_secs_be = 0;
  memcpy(&((uint8_t *)(&temp_secs_be))[2], ts->seconds, sizeof(ts->seconds));
  temp_secs_be = be64toh(temp_secs_be);
  temp = temp_secs_be * 1000000000;
  temp += be32toh(ts->nanoseconds);
  return temp;
}

void ptp_req_thread_func(timesync_clock_t *instance) {
  uint8_t tx_buf[2048];
  // We shouldn't care about the transport protocol
  // tx_buf[0] = 0x01;
  // tx_buf[1] = 0x1B;
  // tx_buf[2] = 0x19;
  // tx_buf[3] = 0x00;
  // tx_buf[4] = 0x00;
  // tx_buf[5] = 0x00;
  //
  // tx_buf[6] = 0x01;
  // tx_buf[7] = 0x02;
  // tx_buf[8] = 0x03;
  // tx_buf[9] = 0x04;
  // tx_buf[10] = 0x05;
  // tx_buf[11] = 0x06;

  // tx_buf[12] = 0x88;
  // tx_buf[13] = 0xF7;
  ptp_message_pdelay_req_t req = {{0}};
  req.header.message_type = PTP_MESSAGE_TYPE_PDELAY_REQ;
  req.header.transport_specific = 0;
  req.header.version = 2;
  req.header.reserved_1 = 0;
  req.header.message_length = sizeof(ptp_message_pdelay_req_t);
  // TODO: Make domain number adjustable
  req.header.domain_num = 0;
  req.header.reserved_2 = 0;
  req.header.flags.raw_val = 0;
  req.header.flags.utc_offset_valid = 1;
  // TODO: Look into what the correction field does
  req.header.correction_field = 0;
  req.header.reserved_3 = 0;
  // TODO: Make clock identity and port num adjustable
  req.header.source_port_identity.port_number = 0x0001;
  req.header.source_port_identity.clock_identity[0] = 0x01;
  req.header.source_port_identity.clock_identity[1] = 0x02;
  req.header.source_port_identity.clock_identity[2] = 0x03;
  req.header.source_port_identity.clock_identity[3] = 0x04;
  req.header.source_port_identity.clock_identity[4] = 0x05;
  req.header.source_port_identity.clock_identity[5] = 0x06;
  uint16_t sequence_id = 0;
  req.header.sequence_id = htons(sequence_id);
  req.header.control_field = 0x05;
  req.header.log_message_interval = 0x7F;
  // origintimestamp shall be 0 (not needed for PDELAY_REQ)

  memcpy(tx_buf, (void *)&req, sizeof(ptp_message_pdelay_req_t));
  while (!instance->stop) {
    instance->mutex_lock(instance->mutex);
    int sent = instance->send(tx_buf, sizeof(ptp_message_pdelay_req_t));
    instance->latest_t3 = instance->get_time_ns_tx();
    // TODO: Handle error, incomplete send, etc.
    sequence_id++;
    req.header.sequence_id = htons(sequence_id);
    instance->mutex_unlock(instance->mutex);

    instance->sleep_ms(instance->pdelay_req_interval_ms);
  }
}

void ptp_thread_func(timesync_clock_t *instance) {
  uint8_t rx_buf[2048];
  uint8_t tx_buf[2048];
  while (!instance->stop) {
    int amount_received = instance->receive(rx_buf, sizeof(rx_buf));
    if (amount_received > 0) {
      uint64_t received_ts = instance->get_time_ns_rx();
      ptp_message_header_t *header = (ptp_message_header_t *)rx_buf;
      instance->mutex_lock(instance->mutex);
      switch (header->message_type) {
      case PTP_MESSAGE_TYPE_SYNC: {
        ptp_message_sync_t *msg = (ptp_message_sync_t *)rx_buf;
        ptp_delay_info_entry_t *delay_info = get_or_new_delay_info(
            instance, port_identity_to_id(&msg->header.source_port_identity));
        if (delay_info != NULL) {
          // Don't adjust clock
          delay_info->delay_info.t2 = received_ts;
        }
        // TODO: Handle
        break;
      }
      case PTP_MESSAGE_TYPE_DELAY_REQ: {
        ptp_message_delay_req_t *msg = (ptp_message_delay_req_t *)rx_buf;
        // TODO: Handle E2E delay req
        break;
      }
      case PTP_MESSAGE_TYPE_FOLLOW_UP: {
        ptp_message_follow_up_t *msg = (ptp_message_follow_up_t *)rx_buf;
        ptp_delay_info_entry_t *delay_info = search_delay_info(
            instance, port_identity_to_id(&msg->header.source_port_identity));
        if (delay_info != NULL) {
          delay_info->delay_info.t1 = ts_to_ns(&msg->precise_origin_timestamp);
          if (delay_info->delay_info.last_calculated_delay > 0) {
            // TODO: int64 theoretically reduces the possible
            //       time range, what do?
            int64_t offset =
                ((int64_t)delay_info->delay_info.t2 -
                 (int64_t)delay_info->delay_info.t1) -
                (int64_t)delay_info->delay_info.last_calculated_delay;
            // TODO: Check returnval
            instance->set_time_offset_ns(offset);
          } else {
            // TODO: Notify user?
          }
        }
        break;
      }
      case PTP_MESSAGE_TYPE_DELAY_RESP: {
        ptp_message_delay_resp_t *msg = (ptp_message_delay_resp_t *)rx_buf;
        // TODO: Handle E2E delay calculation
        break;
      }
      case PTP_MESSAGE_TYPE_PDELAY_REQ: {
        ptp_message_pdelay_req_t *msg = (ptp_message_pdelay_req_t *)rx_buf;
        // TODO: Handle PDELAY_REQ requests coming from other peers
        uint64_t secs = htobe64(received_ts / 1000000000);
        // Get the remaining ns
        received_ts = received_ts % 1000000000;
        ptp_message_pdelay_resp_t *resp = (ptp_message_pdelay_resp_t *)tx_buf;
        resp->header = ptp_message_create_header(PTP_MESSAGE_TYPE_PDELAY_RESP);
        resp->header.sequence_id = msg->header.sequence_id;
        resp->requesting_port_identity = msg->header.source_port_identity;
        resp->request_receipt_timestamp.nanoseconds =
            htobe32((uint32_t)received_ts);
        memcpy(resp->request_receipt_timestamp.seconds, &((uint8_t *)secs)[2],
               6);

        int sent =
            instance->send((uint8_t *)resp, sizeof(ptp_message_pdelay_resp_t));
        // TODO: Check sent amount
        uint64_t sent_ts = instance->get_time_ns_tx();
        ptp_message_pdelay_resp_follow_up_t *fup =
            (ptp_message_pdelay_resp_follow_up_t *)tx_buf;

        fup->header =
            ptp_message_create_header(PTP_MESSAGE_TYPE_PDELAY_RESP_FOLLOW_UP);
        fup->header.sequence_id = msg->header.sequence_id;
        fup->requesting_port_identity = msg->header.source_port_identity;
        secs = htobe64(sent_ts / 1000000000);
        uint32_t sent_ns = htobe32(sent_ts % 1000000000);
        fup->response_origin_timestamp.nanoseconds = sent_ns;
        memcpy(fup->response_origin_timestamp.seconds, &((uint8_t *)secs)[2],
               6);

        sent = instance->send((uint8_t *)fup,
                              sizeof(ptp_message_pdelay_resp_follow_up_t));
        // TODO: Check sent amount

        break;
      }
      case PTP_MESSAGE_TYPE_PDELAY_RESP: {
        ptp_message_pdelay_resp_t *msg = (ptp_message_pdelay_resp_t *)rx_buf;
        uint64_t id = port_identity_to_id(&msg->header.source_port_identity);
        ptp_delay_info_entry_t *delay_info =
            get_or_new_delay_info(instance, id);
        if (delay_info != NULL) {
          // TODO: Check sequence_id to be safe
          uint64_t ts = ts_to_ns(&msg->request_receipt_timestamp);
          delay_info->delay_info.t3 = instance->latest_t3;
          delay_info->delay_info.t4 = ts;
          delay_info->delay_info.t6 = received_ts;
        } else {
          // TODO: Handle erroneous state
        }
        break;
      }
      case PTP_MESSAGE_TYPE_PDELAY_RESP_FOLLOW_UP: {
        ptp_message_pdelay_resp_follow_up_t *msg =
            (ptp_message_pdelay_resp_follow_up_t *)rx_buf;
        uint64_t id = port_identity_to_id(&msg->header.source_port_identity);
        ptp_delay_info_entry_t *delay_info = search_delay_info(instance, id);
        if (delay_info != NULL) {
          uint64_t ts = ts_to_ns(&msg->response_origin_timestamp);
          delay_info->delay_info.t5 = ts;
          uint64_t t3 = delay_info->delay_info.t3;
          uint64_t t4 = delay_info->delay_info.t4;
          uint64_t t5 = delay_info->delay_info.t5;
          uint64_t t6 = delay_info->delay_info.t6;
          if (t3 != 0 && t4 != 0 && t5 != 0 && t6 != 0) {
            uint64_t delay = ((t6 - t3) - (t5 - t4)) / 2;
            delay_info->delay_info.last_calculated_delay = delay;
          } else {
            // TODO: Handle weird state
          }
        }
        break;
      }
      }
      instance->mutex_unlock(instance->mutex);
    }
  }
}
