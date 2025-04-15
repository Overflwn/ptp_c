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
static ptp_delay_info_entry_t *search_delay_info(const ptp_clock_t *instance,
                                                 const uint64_t id) {
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
static ptp_delay_info_entry_t *get_or_new_delay_info(ptp_clock_t *instance,
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
static void remove_delay_info(ptp_clock_t *instance, const uint64_t id) {
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

/// @brief Helper function to convert a 64bit unsigned int to
/// ptp_message_timestamp_t
/// TODO: Move this function (and probably others aswell) to a utils file
/// @param[in] ts The timestamp to convert
/// @return The converted timestamp or 0 on error
static ptp_message_timestamp_t ns_to_ts(uint64_t ts) {
  uint64_t temp_secs = htobe64(ts / 1000000000);
  uint64_t temp = ts % 1000000000;
  ptp_message_timestamp_t result = {0};
  uint64_t temp_secs_be = 0;
  memcpy(result.seconds, &((uint8_t *)(&temp_secs))[2], sizeof(result.seconds));
  result.nanoseconds = htobe32(temp);
  return result;
}

void ptp_pdelay_req_thread_func(ptp_clock_t *instance) {
  ptp_message_pdelay_req_t req = {{0}};
  req.header = ptp_message_create_header(instance, PTP_MESSAGE_TYPE_PDELAY_REQ);
  uint16_t sequence_id = 0;
  req.header.sequence_id = htons(sequence_id);
  // 0x05 := "All others", see struct field comment
  req.header.control_field = 0x05;
  req.header.log_message_interval = 0x7F;
  // origintimestamp shall be 0 (not needed for PDELAY_REQ)

  char log_buf[512];
  while (!instance->stop) {
    if (instance->use_p2p) {
      instance->mutex_lock(instance->mutex);
      int sent = instance->send(
          instance->userdata,
          PTP_CONTROL_SEND_MULTICAST | PTP_CONTROL_SEND_EVENT, NULL,
          (uint8_t *)&req, sizeof(ptp_message_pdelay_req_t));
      if (sent < sizeof(ptp_message_pdelay_req_t) && instance->debug_log) {
        snprintf(log_buf, sizeof(log_buf),
                 "Failed to send PDelay_Req. (returnval %d)", sent);
        instance->debug_log(instance->userdata, log_buf);
      }
      instance->latest_t3 = instance->get_time_ns_tx(instance->userdata);
      // TODO: Handle error, incomplete send, etc.
      sequence_id++;
      req.header.sequence_id = htons(sequence_id);
      instance->mutex_unlock(instance->mutex);
    }
    instance->sleep_ms(instance->pdelay_req_interval_ms);
  }
}

static void calculate_new_time(ptp_clock_t *instance,
                               ptp_delay_info_entry_t *delay_info,
                               void *recv_metadata, uint8_t *tx_buf) {
  char log_buf[512];
  if (delay_info->delay_info.last_calculated_delay > 0 &&
      delay_info->delay_info.t1 != 0 && delay_info->delay_info.t2 != 0) {
    // TODO: int64 theoretically reduces the possible
    //       time range, what do?
    int64_t offset = ((int64_t)delay_info->delay_info.t2 -
                      (int64_t)delay_info->delay_info.t1) -
                     (int64_t)delay_info->delay_info.last_calculated_delay;
    // Adjust for a little bit of runtime overhead up to this point
    // offset += (int64_t)(instance->get_time_ns(instance->userdata) -
    //                     instance->fup_received);
    // TODO: Check returnval
    instance->set_time_offset_ns(instance->userdata, offset);
    // uint64_t ts = instance->get_time_ns(instance->userdata);
    // Get the master time delta
    if (instance->adjust_period) {
      double master_delta = (double)(delay_info->delay_info.t1 -
                                     delay_info->delay_info.previous_t1);
      double our_delta = (double)(delay_info->delay_info.t2 -
                                  instance->last_ts_after_correction -
                                  delay_info->delay_info.last_calculated_delay);
      double drift = 1.0 - ((our_delta - master_delta) / master_delta);
      if (drift > -0.1 || drift < 0.1) {
        if (instance->adjust_period(drift) && instance->debug_log) {
          snprintf(log_buf, sizeof(log_buf), "Adjusted period by %.09lf",
                   drift);
          instance->debug_log(instance->userdata, log_buf);
        } else if (instance->debug_log) {
          snprintf(log_buf, sizeof(log_buf),
                   "Failed to adjust period by %.09lf", drift);
          instance->debug_log(instance->userdata, log_buf);
        }
      } else if (instance->debug_log) {
        snprintf(log_buf, sizeof(log_buf),
                 "Unplausible clock drift (factor: %.09lf), ignoring..", drift);
        instance->debug_log(instance->userdata, log_buf);
      }
    }
    instance->last_ts_after_correction = delay_info->delay_info.t2 + offset;
    uint64_t old_t1 = delay_info->delay_info.t1;
    uint64_t old_t2 = delay_info->delay_info.t2;
    if (instance->use_p2p) {
      // Reset all timestamps so that we don't calculate false delays by
      // accident when having bad timing (e.g. PDelayReq -> Sync+FUP -> New
      // offset -> PDelayResp)
      delay_info->delay_info.t1 = 0;
      delay_info->delay_info.t2 = 0;
      delay_info->delay_info.t3 = 0;
      delay_info->delay_info.t4 = 0;
      delay_info->delay_info.t5 = 0;
      delay_info->delay_info.t6 = 0;
      instance->latest_t3 = 0;
    }
    instance->statistics.last_offset_ns = offset;
    instance->statistics.last_sync_ts =
        instance->get_time_ns(instance->userdata);
    instance->statistics.last_delay_ns =
        delay_info->delay_info.last_calculated_delay;
    if (instance->debug_log) {
      snprintf(log_buf, sizeof(log_buf),
               "New offset: %" PRId64 " (t1 := %" PRIu64 ", t2 := %" PRIu64 ")",
               offset, old_t1, old_t2);
      instance->debug_log(instance->userdata, log_buf);
    }
  } else {
    if (instance->use_p2p) {
      // Reset T1 and T2 so that we don't block PDelay by accident
      // calculate_new_time only gets called after FUP (in P2P mode) so this
      // should be fine
      delay_info->delay_info.t1 = 0;
      delay_info->delay_info.t2 = 0;
      instance->latest_t3 = 0;
    }
    // TODO: Notify user?
    if (instance->debug_log) {
      snprintf(log_buf, sizeof(log_buf),
               "No offset or delay info has been calculated yet, ignoring new "
               "time.. (t1: %" PRIu64 ", t2: %" PRIu64 ")",
               delay_info->delay_info.t1, delay_info->delay_info.t2);
      instance->debug_log(instance->userdata, log_buf);
    }
  }

  // Don't send a DELAY_REQ if we're using P2P *or* if no tx_buf is given (->
  // only recalculate offset)
  if (!instance->use_p2p && tx_buf) {
    if (instance->debug_log) {
      instance->debug_log(
          instance->userdata,
          "Not using P2P for delay calculation, sending DELAY_REQ..");
    }

    ptp_message_delay_req_t *resp = (ptp_message_delay_req_t *)tx_buf;
    memset((void *)resp, 0, sizeof(ptp_message_delay_req_t));
    resp->header =
        ptp_message_create_header(instance, PTP_MESSAGE_TYPE_DELAY_REQ);
    resp->header.sequence_id =
        htobe16(delay_info->delay_info.sequence_id_delay_req);

    int sent = instance->send(
        instance->userdata, PTP_CONTROL_SEND_UNICAST | PTP_CONTROL_SEND_EVENT,
        recv_metadata, (uint8_t *)resp, sizeof(ptp_message_delay_req_t));
    uint64_t sent_ts = instance->get_time_ns_tx(instance->userdata);
    if (sent < sizeof(ptp_message_delay_req_t) && instance->debug_log) {
      snprintf(log_buf, sizeof(log_buf),
               "Failed to send DELAY_REQ message. (returnval %d)", sent);
      instance->debug_log(instance->userdata, log_buf);
    } else {
      if (instance->debug_log) {
        snprintf(log_buf, sizeof(log_buf), "DELAY_REQ sent. (t3 = %" PRIu64 ")",
                 sent_ts);
        instance->debug_log(instance->userdata, log_buf);
      }
      delay_info->delay_info.t3 = sent_ts;
      delay_info->delay_info.sequence_id_delay_req++;
    }
  }
}

void ptp_thread_func(ptp_clock_t *instance) {
  uint8_t rx_buf[512];
  uint8_t tx_buf[512];
  char log_buf[512];
  // We get this from the user-defined receive function
  // It might be for example metadata about the sender and receiver (i.e. srcIp,
  // srcPort, dstIp, dstPort)
  // We don't do anything with it, we just give it back when sending
  void *recv_metadata = NULL;
  uint32_t announce_time = 0;
  uint32_t sync_time = 0;
  while (!instance->stop) {
    int amount_received = instance->receive(instance->userdata, &recv_metadata,
                                            rx_buf, sizeof(rx_buf));
    if (amount_received > 0) {
      uint64_t received_ts = instance->get_time_ns_rx(instance->userdata);
      ptp_message_header_t *header = (ptp_message_header_t *)rx_buf;
      // Ignore messages that looped back from ourselves
      if (0 != memcmp(&header->source_port_identity,
                      &instance->source_port_identity,
                      sizeof(header->source_port_identity))) {
        instance->mutex_lock(instance->mutex);
        switch (header->message_type) {
        case PTP_MESSAGE_TYPE_SYNC: {
          if (instance->debug_log) {
            instance->debug_log(instance->userdata, "Received SYNC message");
          }
          if (instance->clock_type == PTP_CLOCK_TYPE_MASTER) {
            if (instance->debug_log) {
              instance->debug_log(instance->userdata,
                                  "We're set as master, ignoring SYNC...");
            }
            break;
          }
          ptp_message_sync_t *msg = (ptp_message_sync_t *)rx_buf;
          ptp_delay_info_entry_t *delay_info = get_or_new_delay_info(
              instance, port_identity_to_id(&msg->header.source_port_identity));
          if (delay_info != NULL) {
            delay_info->delay_info.t2 = received_ts;
            if (instance->debug_log) {
              snprintf(log_buf, sizeof(log_buf), "Received t2: %" PRIu64,
                       received_ts);
              instance->debug_log(instance->userdata, log_buf);
            }
            if (!msg->header.flags.two_step) {
              if (instance->debug_log) {
                instance->debug_log(instance->userdata,
                                    "Master uses one-step sync");
              }
              delay_info->delay_info.t1 = ts_to_ns(&msg->origin_timestamp);
              calculate_new_time(instance, delay_info, recv_metadata, tx_buf);
            } // else adjust after FOLLOW_UP
          } else if (instance->debug_log) {
            snprintf(log_buf, sizeof(log_buf),
                     "Failed to get or create new delay_info.");
            instance->debug_log(instance->userdata, log_buf);
          }
          // TODO: Handle
          break;
        }
        case PTP_MESSAGE_TYPE_DELAY_REQ: {
          if (instance->debug_log) {
            instance->debug_log(instance->userdata,
                                "Received DELAY_REQ message");
          }
          ptp_message_delay_req_t *msg = (ptp_message_delay_req_t *)rx_buf;
          ptp_message_delay_resp_t *resp = (ptp_message_delay_resp_t *)tx_buf;
          memset(resp, 0, sizeof(ptp_message_delay_resp_t));
          resp->header =
              ptp_message_create_header(instance, PTP_MESSAGE_TYPE_DELAY_RESP);
          resp->receive_timestamp = ns_to_ts(received_ts);
          resp->requesting_port_identity = msg->header.source_port_identity;
          int sent = instance->send(
              instance->userdata,
              PTP_CONTROL_SEND_UNICAST | PTP_CONTROL_SEND_GENERAL,
              recv_metadata, (uint8_t *)resp, sizeof(ptp_message_delay_resp_t));
          if (sent < sizeof(ptp_message_delay_resp_t) && instance->debug_log) {
            snprintf(log_buf, sizeof(log_buf),
                     "Failed to send DELAY_RESP. (returnval: %d)", sent);
            instance->debug_log(instance->userdata, log_buf);
          }
          // TODO: Handle E2E delay req
          break;
        }
        case PTP_MESSAGE_TYPE_FOLLOW_UP: {
          instance->fup_received = received_ts;
          if (instance->debug_log) {
            instance->debug_log(instance->userdata,
                                "Received FOLLOW_UP message");
          }
          if (instance->clock_type == PTP_CLOCK_TYPE_MASTER) {
            if (instance->debug_log) {
              instance->debug_log(instance->userdata,
                                  "We're set as master, ignoring SYNC...");
            }
            break;
          }
          ptp_message_follow_up_t *msg = (ptp_message_follow_up_t *)rx_buf;
          uint64_t id = port_identity_to_id(&msg->header.source_port_identity);
          ptp_delay_info_entry_t *delay_info = search_delay_info(instance, id);
          // TODO: Check if sequence_id is reasonable (== sequence_id from last
          // SYNC message)
          if (delay_info != NULL) {
            // Save previous t1 for drift calculation
            delay_info->delay_info.previous_t1 = delay_info->delay_info.t1;
            delay_info->delay_info.t1 =
                ts_to_ns(&msg->precise_origin_timestamp);
            if (!instance->use_p2p) {

              if (instance->debug_log) {
                instance->debug_log(
                    instance->userdata,
                    "Not using P2P for delay calculation, sending DELAY_REQ..");
              }

              ptp_message_delay_req_t *resp = (ptp_message_delay_req_t *)tx_buf;
              memset((void *)resp, 0, sizeof(ptp_message_delay_req_t));
              resp->header = ptp_message_create_header(
                  instance, PTP_MESSAGE_TYPE_DELAY_REQ);
              resp->header.sequence_id =
                  htobe16(delay_info->delay_info.sequence_id_delay_req);

              int sent = instance->send(instance->userdata,
                                        PTP_CONTROL_SEND_UNICAST |
                                            PTP_CONTROL_SEND_EVENT,
                                        recv_metadata, (uint8_t *)resp,
                                        sizeof(ptp_message_delay_req_t));
              uint64_t sent_ts = instance->get_time_ns_tx(instance->userdata);
              if (sent < sizeof(ptp_message_delay_req_t) &&
                  instance->debug_log) {
                snprintf(log_buf, sizeof(log_buf),
                         "Failed to send DELAY_REQ message. (returnval %d)",
                         sent);
                instance->debug_log(instance->userdata, log_buf);
              } else {
                if (instance->debug_log) {
                  snprintf(log_buf, sizeof(log_buf),
                           "DELAY_REQ sent. (t3 = %" PRIu64 ")", sent_ts);
                  instance->debug_log(instance->userdata, log_buf);
                }
                delay_info->delay_info.t3 = sent_ts;
                delay_info->delay_info.sequence_id_delay_req++;
              }
            } else {
              calculate_new_time(instance, delay_info, recv_metadata, tx_buf);
            }
          } else if (instance->debug_log) {
            instance->debug_log(instance->userdata,
                                "No delay_info for sender, ignoring FOLLOW_UP");
          }
          break;
        }
        case PTP_MESSAGE_TYPE_DELAY_RESP: {
          if (instance->debug_log) {
            instance->debug_log(instance->userdata,
                                "Received DELAY_RESP message");
          }
          ptp_message_delay_resp_t *msg = (ptp_message_delay_resp_t *)rx_buf;
          uint64_t id = port_identity_to_id(&msg->header.source_port_identity);
          ptp_delay_info_entry_t *delay_info = search_delay_info(instance, id);
          if (delay_info) {
            // TODO: Check "requesting source port identity" (if it is ours)
            delay_info->delay_info.t4 = ts_to_ns(&msg->receive_timestamp);

            if (delay_info->delay_info.t1 != 0 &&
                delay_info->delay_info.t2 != 0 &&
                delay_info->delay_info.t3 != 0 &&
                delay_info->delay_info.t4 != 0) {
              int64_t t1 = (int64_t)delay_info->delay_info.t1;
              int64_t t2 = (int64_t)delay_info->delay_info.t2;
              int64_t t3 = (int64_t)delay_info->delay_info.t3;
              int64_t t4 = (int64_t)delay_info->delay_info.t4;
              if (instance->debug_log) {
                snprintf(log_buf, sizeof(log_buf),
                         "New delay (%" PRIu64 "). (t1 := %" PRIu64
                         ", t2 := %" PRIu64 ", t3 := %" PRIu64
                         ", t4 := %" PRIu64,
                         (uint64_t)(((t2 - t1) + (t4 - t3)) / 2),
                         delay_info->delay_info.t1, delay_info->delay_info.t2,
                         delay_info->delay_info.t3, delay_info->delay_info.t4);
                instance->debug_log(instance->userdata, log_buf);
              }
              delay_info->delay_info.last_calculated_delay =
                  (uint64_t)(((t2 - t1) + (t4 - t3)) / 2);
              // Trigger re-calculation of offset
              // Don't pass tx_buf as we don't want to send DELAY_REQ yet again
              calculate_new_time(instance, delay_info, recv_metadata, NULL);
            } else if (instance->debug_log) {
              snprintf(
                  log_buf, sizeof(log_buf),
                  "Can't calculate E2E delay, invalid state. (t1 := %" PRIu64
                  ", t2 := %" PRIu64 ", t3 := %" PRIu64 ", t4 := %" PRIu64,
                  delay_info->delay_info.t1, delay_info->delay_info.t2,
                  delay_info->delay_info.t3, delay_info->delay_info.t4);
              instance->debug_log(instance->userdata, log_buf);
            }
          } else if (instance->debug_log) {
            instance->debug_log(instance->userdata,
                                "No delay_info for DELAY_RESP sender.");
          }
          break;
        }
        case PTP_MESSAGE_TYPE_PDELAY_REQ: {
          if (instance->debug_log) {
            instance->debug_log(instance->userdata,
                                "Received PDELAY_REQ message");
          }
          ptp_message_pdelay_req_t *msg = (ptp_message_pdelay_req_t *)rx_buf;
          uint64_t secs = htobe64(received_ts / 1000000000);
          // Get the remaining ns
          received_ts = received_ts % 1000000000;
          ptp_message_pdelay_resp_t *resp = (ptp_message_pdelay_resp_t *)tx_buf;
          memset(resp, 0, sizeof(ptp_message_pdelay_resp_t));
          resp->header =
              ptp_message_create_header(instance, PTP_MESSAGE_TYPE_PDELAY_RESP);
          resp->header.sequence_id = msg->header.sequence_id;
          resp->requesting_port_identity = msg->header.source_port_identity;
          resp->request_receipt_timestamp.nanoseconds =
              htobe32((uint32_t)received_ts);
          memcpy((uint8_t *)resp->request_receipt_timestamp.seconds,
                 &((uint8_t *)&secs)[2], 6);

          int sent = instance->send(
              instance->userdata,
              PTP_CONTROL_SEND_UNICAST | PTP_CONTROL_SEND_EVENT, recv_metadata,
              (uint8_t *)resp, sizeof(ptp_message_pdelay_resp_t));
          if (sent < sizeof(ptp_message_pdelay_resp_t) && instance->debug_log) {
            snprintf(log_buf, sizeof(log_buf),
                     "Failed to send PDELAY_RESP message. (returnval %d)",
                     sent);
            instance->debug_log(instance->userdata, log_buf);
          } else {
            // TODO: Check sent amount
            uint64_t sent_ts = instance->get_time_ns_tx(instance->userdata);
            ptp_message_pdelay_resp_follow_up_t *fup =
                (ptp_message_pdelay_resp_follow_up_t *)tx_buf;

            fup->header = ptp_message_create_header(
                instance, PTP_MESSAGE_TYPE_PDELAY_RESP_FOLLOW_UP);
            fup->header.sequence_id = msg->header.sequence_id;
            fup->requesting_port_identity = msg->header.source_port_identity;
            secs = htobe64(sent_ts / 1000000000);
            uint32_t sent_ns = htobe32(sent_ts % 1000000000);
            fup->response_origin_timestamp.nanoseconds = sent_ns;
            memcpy(fup->response_origin_timestamp.seconds,
                   &((uint8_t *)&secs)[2], 6);

            sent = instance->send(instance->userdata,
                                  PTP_CONTROL_SEND_UNICAST |
                                      PTP_CONTROL_SEND_GENERAL,
                                  recv_metadata, (uint8_t *)fup,
                                  sizeof(ptp_message_pdelay_resp_follow_up_t));
            if (sent < sizeof(ptp_message_pdelay_resp_follow_up_t) &&
                instance->debug_log) {
              snprintf(log_buf, sizeof(log_buf),
                       "Failed to send PDELAY_RESP_FOLLOW_UP message. "
                       "(returnval %d)",
                       sent);
              instance->debug_log(instance->userdata, log_buf);
            }
          }
          break;
        }
        case PTP_MESSAGE_TYPE_PDELAY_RESP: {
          if (instance->debug_log) {
            instance->debug_log(instance->userdata,
                                "Received PDELAY_RESP message");
          }
          ptp_message_pdelay_resp_t *msg = (ptp_message_pdelay_resp_t *)rx_buf;
          uint64_t id = port_identity_to_id(&msg->header.source_port_identity);
          ptp_delay_info_entry_t *delay_info =
              get_or_new_delay_info(instance, id);
          if (delay_info != NULL) {
            // TODO: Check sequence_id to be safe
            if (delay_info->delay_info.t2 != 0 || instance->latest_t3 == 0) {
              // We at least got a SYNC message and still haven't recalculated
              // the offset (see calculate_new_offset function) but already
              // initiated pdelay -> Ignore this PDelay iteration
              break;
            }
            uint64_t ts = ts_to_ns(&msg->request_receipt_timestamp);
            delay_info->delay_info.t3 = instance->latest_t3;
            delay_info->delay_info.t4 = ts;
            delay_info->delay_info.t6 = received_ts;
          } else if (instance->debug_log) {
            // TODO: Handle erroneous state
            instance->debug_log(instance->userdata,
                                "No delay_info found for PDELAY_RESP sender.");
          }
          break;
        }
        case PTP_MESSAGE_TYPE_PDELAY_RESP_FOLLOW_UP: {
          if (instance->debug_log) {
            instance->debug_log(instance->userdata,
                                "Received PDELAY_RESP_FOLLOW_UP message");
          }
          ptp_message_pdelay_resp_follow_up_t *msg =
              (ptp_message_pdelay_resp_follow_up_t *)rx_buf;
          uint64_t id = port_identity_to_id(&msg->header.source_port_identity);
          ptp_delay_info_entry_t *delay_info = search_delay_info(instance, id);
          if (delay_info != NULL) {
            if (delay_info->delay_info.t2 != 0 || instance->latest_t3 == 0) {
              // We at least got a SYNC message and still haven't recalculated
              // the offset (see calculate_new_offset function) but already
              // initiated pdelay -> Ignore this PDelay iteration
              break;
            }
            uint64_t ts = ts_to_ns(&msg->response_origin_timestamp);
            delay_info->delay_info.t5 = ts;
            uint64_t t3 = delay_info->delay_info.t3;
            uint64_t t4 = delay_info->delay_info.t4;
            uint64_t t5 = delay_info->delay_info.t5;
            uint64_t t6 = delay_info->delay_info.t6;
            if (t3 != 0 && t4 != 0 && t5 != 0 && t6 != 0) {
              uint64_t delay = ((t6 - t3) - (t5 - t4)) / 2;
              delay_info->delay_info.last_calculated_delay = delay;
              if (instance->debug_log) {
                snprintf(log_buf, sizeof(log_buf), "New PDelay: %" PRIu64,
                         delay);
                instance->debug_log(instance->userdata, log_buf);
              }
              // Trigger re-calculation of offset
              // Don't pass tx_buf as we don't want to send DELAY_REQ yet again
              // TODO: *Don't* Recalculate time after PDelay RespFUP -> Needs
              // testing calculate_new_time(instance, delay_info, recv_metadata,
              // NULL);
            } else if (instance->debug_log) {
              // TODO: Handle weird state
              snprintf(log_buf, sizeof(log_buf),
                       "Failed to calculate new peer delay. (t3: %" PRIu64
                       ", t4: %" PRIu64 ", "
                       "t5: %" PRIu64 ", t6: %" PRIu64 ")",
                       t3, t4, t5, t6);
              instance->debug_log(instance->userdata, log_buf);
            }
          } else if (instance->debug_log) {
            instance->debug_log(
                instance->userdata,
                "No delay_info found for PDELAY_RESP_FOLLOW_UP sender");
          }
          break;
        }
        }
        instance->mutex_unlock(instance->mutex);
      }
    } else if (amount_received < 0 && instance->debug_log) {
      // snprintf(log_buf, sizeof(log_buf),
      //          "Failed to receive data. (returnval %d)", amount_received);
      // instance->debug_log(instance->userdata, log_buf);
    }

    if (instance->clock_type == PTP_CLOCK_TYPE_MASTER &&
        announce_time >= instance->master.announce_msg_interval_ms) {
      instance->mutex_lock(instance->mutex);
      ptp_message_announce_t *msg = (ptp_message_announce_t *)tx_buf;
      memset(msg, 0, sizeof(ptp_message_announce_t));
      msg->header =
          ptp_message_create_header(instance, PTP_MESSAGE_TYPE_ANNOUNCE);
      msg->current_utc_offset = htobe16(instance->master.utc_offset);
      msg->grandmaster_priority_1 = instance->master.grandmaster_priority_1;
      msg->grandmaster_clock_quality.clock_accuracy =
          instance->master.clock_quality.clock_accuracy;
      msg->grandmaster_clock_quality.clock_class =
          instance->master.clock_quality.clock_class;
      msg->grandmaster_clock_quality.scaled_log_variance =
          htobe16(instance->master.clock_quality.scaled_log_variance);
      msg->grandmaster_priority_2 = instance->master.grandmaster_priority_2;
      memcpy(msg->grandmaster_clock_identity,
             instance->master.grandmaster_clock_identity,
             sizeof(msg->grandmaster_clock_identity));
      msg->steps_removed = htons(instance->master.steps_removed);
      msg->time_source = instance->master.time_source;
      msg->origin_timestamp =
          ns_to_ts(instance->get_time_ns(instance->userdata));

      if (instance->debug_log) {
        instance->debug_log(instance->userdata, "Sending ANNOUNCE message..");
      }
      int sent =
          instance->send(instance->userdata,
                         PTP_CONTROL_SEND_MULTICAST | PTP_CONTROL_SEND_GENERAL,
                         NULL, (uint8_t *)msg, sizeof(ptp_message_announce_t));

      if (sent < sizeof(ptp_message_announce_t) && instance->debug_log) {
        snprintf(log_buf, sizeof(log_buf),
                 "Failed to send ANNOUNCE message. (retunval %d)", sent);
        instance->debug_log(instance->userdata, log_buf);
      }

      announce_time = 0;
      instance->mutex_unlock(instance->mutex);
    }

    if (instance->clock_type == PTP_CLOCK_TYPE_MASTER &&
        sync_time >= instance->master.sync_msg_interval_ms) {
      instance->mutex_lock(instance->mutex);
      ptp_message_sync_t *msg = (ptp_message_sync_t *)tx_buf;
      memset(msg, 0, sizeof(ptp_message_sync_t));
      msg->header = ptp_message_create_header(instance, PTP_MESSAGE_TYPE_SYNC);
      // This would be one-step mode
      // msg->origin_timestamp = ns_to_ts(instance->get_time_ns());

      if (instance->debug_log) {
        instance->debug_log(instance->userdata, "Sending SYNC message..");
      }
      int sent =
          instance->send(instance->userdata,
                         PTP_CONTROL_SEND_MULTICAST | PTP_CONTROL_SEND_EVENT,
                         NULL, (uint8_t *)msg, sizeof(ptp_message_sync_t));
      uint64_t sent_ts = instance->get_time_ns_tx(instance->userdata);

      if (sent < sizeof(ptp_message_sync_t) && instance->debug_log) {
        snprintf(log_buf, sizeof(log_buf),
                 "Failed to send SYNC message. (retunval %d)", sent);
        instance->debug_log(instance->userdata, log_buf);
      } else {
        ptp_message_follow_up_t *fup = (ptp_message_follow_up_t *)tx_buf;
        memset(fup, 0, sizeof(ptp_message_follow_up_t));
        fup->header =
            ptp_message_create_header(instance, PTP_MESSAGE_TYPE_FOLLOW_UP);
        fup->precise_origin_timestamp = ns_to_ts(sent_ts);
        if (instance->debug_log) {
          instance->debug_log(instance->userdata,
                              "Sending FOLLOW_UP message..");
        }
        sent = instance->send(
            instance->userdata,
            PTP_CONTROL_SEND_MULTICAST | PTP_CONTROL_SEND_GENERAL, NULL,
            (uint8_t *)fup, sizeof(ptp_message_follow_up_t));
        if (sent < sizeof(ptp_message_follow_up_t) && instance->debug_log) {
          snprintf(log_buf, sizeof(log_buf),
                   "Failed to send FOLLOW_UP message. (retunval %d)", sent);
          instance->debug_log(instance->userdata, log_buf);
        }
      }

      sync_time = 0;
      instance->mutex_unlock(instance->mutex);
    }
    // NOTE: This "counter" expects the sleep function to be exact and that the
    // above receiving part does not introduce any runtime overhead (->
    // drifting), which is not the reality.
    // TODO: Move SYNC+FUP & ANNOUNCE to a third thread function to be more
    // accurate
    instance->sleep_ms(1);
    announce_time++;
    sync_time++;
  }
}

#ifdef __cplusplus__
}
#endif
