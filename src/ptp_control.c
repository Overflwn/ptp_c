#ifdef __cplusplus__
extern "C" {
#endif

#include "ptp_control.h"
#include "ptp_message.h"
#include "util.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  req.header.major_sdo_id = instance->major_sdo_id;
  req.header.version = 2;
  // TODO: After further testing, set this to 1. OR Make it adjustable for the
  // user to allow for higher compatiblity with master clocks
  req.header.minor_version = 0;
  req.header.message_length = htobe16(sizeof(ptp_message_pdelay_req_t));
  // TODO: Make domain number adjustable
  req.header.domain_num = instance->domain_id;
  // TODO: Make minor_sdo_id adjustable
  req.header.minor_sdo_id = instance->minor_sdo_id;
  req.header.flags.raw_val = 0;
  req.header.flags.two_step = 1;
  req.header.flags.utc_offset_valid = 1;
  // TODO: Look into what the correction field does
  // Update: seems to be related to transparent clocks (e.g. PTP-enabled
  // switches), ignore in this case
  req.header.correction_field = 0;
  req.header.message_type_specific = 0;
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
  // 0x05 := "All others", see struct field comment
  req.header.control_field = 0x05;
  req.header.log_message_interval = 0x7F;
  // origintimestamp shall be 0 (not needed for PDELAY_REQ)

  memcpy(tx_buf, (void *)&req, sizeof(ptp_message_pdelay_req_t));
  char log_buf[512];
  while (!instance->stop) {
    if (instance->use_p2p) {
      instance->mutex_lock(instance->mutex);
      int sent = instance->send(PTP_CONTROL_SEND_MULTICAST, NULL, tx_buf,
                                sizeof(ptp_message_pdelay_req_t));
      if (sent < sizeof(ptp_message_pdelay_req_t) && instance->debug_log) {
        snprintf(log_buf, sizeof(log_buf),
                 "Failed to send PDelay_Req. (returnval %d)", sent);
        instance->debug_log(log_buf);
      }
      instance->latest_t3 = instance->get_time_ns_tx();
      // TODO: Handle error, incomplete send, etc.
      sequence_id++;
      req.header.sequence_id = htons(sequence_id);
      instance->mutex_unlock(instance->mutex);
    }
    instance->sleep_ms(instance->pdelay_req_interval_ms);
  }
}

void ptp_thread_func(timesync_clock_t *instance) {
  uint8_t rx_buf[2048];
  uint8_t tx_buf[2048];
  char log_buf[512];
  // We get this from the user-defined receive function
  // It might be for example metadata about the sender and receiver (i.e. srcIp,
  // srcPort, dstIp, dstPort)
  // We don't do anything with it, we just give it back when sending
  void *recv_metadata = NULL;
  while (!instance->stop) {
    int amount_received =
        instance->receive(&recv_metadata, rx_buf, sizeof(rx_buf));
    if (amount_received > 0) {
      uint64_t received_ts = instance->get_time_ns_rx();
      ptp_message_header_t *header = (ptp_message_header_t *)rx_buf;
      instance->mutex_lock(instance->mutex);
      switch (header->message_type) {
      case PTP_MESSAGE_TYPE_SYNC: {
        if (instance->debug_log) {
          instance->debug_log("Received SYNC message");
        }
        ptp_message_sync_t *msg = (ptp_message_sync_t *)rx_buf;
        ptp_delay_info_entry_t *delay_info = get_or_new_delay_info(
            instance, port_identity_to_id(&msg->header.source_port_identity));
        if (delay_info != NULL) {
          if (!msg->header.flags.two_step) {
            if (instance->debug_log) {
              instance->debug_log("Master uses one-step sync");
            }
            delay_info->delay_info.t1 = ts_to_ns(&msg->origin_timestamp);
          }
          // Don't adjust clock
          delay_info->delay_info.t2 = received_ts;
        } else if (instance->debug_log) {
          snprintf(log_buf, sizeof(log_buf),
                   "Failed to get or create new delay_info.");
          instance->debug_log(log_buf);
        }
        // TODO: Handle
        break;
      }
      case PTP_MESSAGE_TYPE_DELAY_REQ: {
        if (instance->debug_log) {
          instance->debug_log("Received DELAY_REQ message");
        }
        ptp_message_delay_req_t *msg = (ptp_message_delay_req_t *)rx_buf;
        // TODO: Handle E2E delay req
        break;
      }
      case PTP_MESSAGE_TYPE_FOLLOW_UP: {
        if (instance->debug_log) {
          instance->debug_log("Received FOLLOW_UP message");
        }
        ptp_message_follow_up_t *msg = (ptp_message_follow_up_t *)rx_buf;
        uint64_t id = port_identity_to_id(&msg->header.source_port_identity);
        ptp_delay_info_entry_t *delay_info = search_delay_info(instance, id);
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
          } else if (instance->debug_log) {
            // TODO: Notify user?
            instance->debug_log(
                "No delay has been calculated yet, ignoring new time..");
          }

          if (!instance->use_p2p) {
            if (instance->debug_log) {
              instance->debug_log(
                  "Not using P2P for delay calculation, sending DELAY_REQ..");
            }

            ptp_message_delay_req_t *resp = (ptp_message_delay_req_t *)tx_buf;
            memset((void *)resp, 0, sizeof(ptp_message_delay_req_t));
            resp->header =
                ptp_message_create_header(instance, PTP_MESSAGE_TYPE_DELAY_REQ);
            resp->header.sequence_id =
                htobe16(delay_info->delay_info.sequence_id_delay_req);

            int sent = instance->send(PTP_CONTROL_SEND_UNICAST, recv_metadata,
                                      (uint8_t *)resp,
                                      sizeof(ptp_message_delay_req_t));
            uint64_t sent_ts = instance->get_time_ns_tx();
            if (sent < sizeof(ptp_message_delay_req_t) && instance->debug_log) {
              snprintf(log_buf, sizeof(log_buf),
                       "Failed to send DELAY_REQ message. (returnval %d)",
                       sent);
              instance->debug_log(log_buf);
            } else {
              if (instance->debug_log) {
                instance->debug_log("DELAY_REQ sent.");
              }
              delay_info->delay_info.t3 = sent_ts;
              delay_info->delay_info.sequence_id_delay_req++;
            }
          }
        } else if (instance->debug_log) {
          instance->debug_log("No delay_info for sender, ignoring FOLLOW_UP");
        }
        break;
      }
      case PTP_MESSAGE_TYPE_DELAY_RESP: {
        if (instance->debug_log) {
          instance->debug_log("Received DELAY_RESP message");
        }
        ptp_message_delay_resp_t *msg = (ptp_message_delay_resp_t *)rx_buf;
        uint64_t id = port_identity_to_id(&msg->header.source_port_identity);
        ptp_delay_info_entry_t *delay_info = search_delay_info(instance, id);
        if (delay_info) {
          // TODO: Check "requesting source port identity"
          delay_info->delay_info.t4 = ts_to_ns(&msg->receive_timestamp);

          if (delay_info->delay_info.t1 != 0 &&
              delay_info->delay_info.t2 != 0 &&
              delay_info->delay_info.t3 != 0 &&
              delay_info->delay_info.t4 != 0) {
            int64_t t1 = (int64_t)delay_info->delay_info.t1;
            int64_t t2 = (int64_t)delay_info->delay_info.t2;
            int64_t t3 = (int64_t)delay_info->delay_info.t3;
            int64_t t4 = (int64_t)delay_info->delay_info.t4;
            delay_info->delay_info.last_calculated_delay =
                (uint64_t)(((t2 - t1) + (t4 - t3)) / 2);
          } else if (instance->debug_log) {
            snprintf(log_buf, sizeof(log_buf),
                     "Can't calculate E2E delay, invalid state. (t1 := %" PRIu64
                     ", t2 := %" PRIu64 ", t3 := %" PRIu64 ", t4 := %" PRIu64,
                     delay_info->delay_info.t1, delay_info->delay_info.t2,
                     delay_info->delay_info.t3, delay_info->delay_info.t4);
            instance->debug_log(log_buf);
          }
        } else if (instance->debug_log) {
          instance->debug_log("No delay_info for DELAY_RESP sender.");
        }
        // TODO: Handle E2E delay calculation
        break;
      }
      case PTP_MESSAGE_TYPE_PDELAY_REQ: {
        if (instance->debug_log) {
          instance->debug_log("Received PDELAY_REQ message");
        }
        ptp_message_pdelay_req_t *msg = (ptp_message_pdelay_req_t *)rx_buf;
        // TODO: Handle PDELAY_REQ requests coming from other peers
        uint64_t secs = htobe64(received_ts / 1000000000);
        // Get the remaining ns
        received_ts = received_ts % 1000000000;
        ptp_message_pdelay_resp_t *resp = (ptp_message_pdelay_resp_t *)tx_buf;
        resp->header =
            ptp_message_create_header(instance, PTP_MESSAGE_TYPE_PDELAY_RESP);
        resp->header.sequence_id = msg->header.sequence_id;
        resp->requesting_port_identity = msg->header.source_port_identity;
        resp->request_receipt_timestamp.nanoseconds =
            htobe32((uint32_t)received_ts);
        memcpy(resp->request_receipt_timestamp.seconds, &((uint8_t *)secs)[2],
               6);

        int sent =
            instance->send(PTP_CONTROL_SEND_UNICAST, recv_metadata,
                           (uint8_t *)resp, sizeof(ptp_message_pdelay_resp_t));
        if (sent < sizeof(ptp_message_pdelay_resp_t) && instance->debug_log) {
          snprintf(log_buf, sizeof(log_buf),
                   "Failed to send PDELAY_RESP message. (returnval %d)", sent);
          instance->debug_log(log_buf);
        } else {
          // TODO: Check sent amount
          uint64_t sent_ts = instance->get_time_ns_tx();
          ptp_message_pdelay_resp_follow_up_t *fup =
              (ptp_message_pdelay_resp_follow_up_t *)tx_buf;

          fup->header = ptp_message_create_header(
              instance, PTP_MESSAGE_TYPE_PDELAY_RESP_FOLLOW_UP);
          fup->header.sequence_id = msg->header.sequence_id;
          fup->requesting_port_identity = msg->header.source_port_identity;
          secs = htobe64(sent_ts / 1000000000);
          uint32_t sent_ns = htobe32(sent_ts % 1000000000);
          fup->response_origin_timestamp.nanoseconds = sent_ns;
          memcpy(fup->response_origin_timestamp.seconds, &((uint8_t *)secs)[2],
                 6);

          sent = instance->send(PTP_CONTROL_SEND_UNICAST, recv_metadata,
                                (uint8_t *)fup,
                                sizeof(ptp_message_pdelay_resp_follow_up_t));
          if (sent < sizeof(ptp_message_pdelay_resp_follow_up_t) &&
              instance->debug_log) {
            snprintf(
                log_buf, sizeof(log_buf),
                "Failed to send PDELAY_RESP_FOLLOW_UP message. (returnval %d)",
                sent);
            instance->debug_log(log_buf);
          }
        }
        break;
      }
      case PTP_MESSAGE_TYPE_PDELAY_RESP: {
        if (instance->debug_log) {
          instance->debug_log("Received PDELAY_RESP message");
        }
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
        } else if (instance->debug_log) {
          // TODO: Handle erroneous state
          instance->debug_log("No delay_info found for PDELAY_RESP sender.");
        }
        break;
      }
      case PTP_MESSAGE_TYPE_PDELAY_RESP_FOLLOW_UP: {
        if (instance->debug_log) {
          instance->debug_log("Received PDELAY_RESP_FOLLOW_UP message");
        }
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
          } else if (instance->debug_log) {
            // TODO: Handle weird state
            snprintf(log_buf, sizeof(log_buf),
                     "Failed to calculate new peer delay. (t3: %" PRIu64
                     ", t4: %" PRIu64 ", "
                     "t5: %" PRIu64 ", t6: %" PRIu64 ")",
                     t3, t4, t5, t6);
            instance->debug_log(log_buf);
          }
        } else if (instance->debug_log) {
          instance->debug_log(
              "No delay_info found for PDELAY_RESP_FOLLOW_UP sender");
        }
        break;
      }
      }
      instance->mutex_unlock(instance->mutex);
    } else if (amount_received < 0 && instance->debug_log) {
      snprintf(log_buf, sizeof(log_buf),
               "Failed to receive data. (returnval %d)", amount_received);
      instance->debug_log(log_buf);
    }
  }
}

#ifdef __cplusplus__
}
#endif
