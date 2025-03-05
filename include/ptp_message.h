#ifndef PTP_MESSAGE_H
#define PTP_MESSAGE_H

#ifdef __cplusplus__
extern "C" {
#endif

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "ptp_control.h"

// Workaround to stop clangd from warning about unterminated pragma pack
// https://github.com/clangd/clangd/issues/1167
static_assert(true, "");

typedef enum {
  PTP_MESSAGE_TYPE_SYNC = 0x00,
  PTP_MESSAGE_TYPE_DELAY_REQ = 0x01,
  PTP_MESSAGE_TYPE_PDELAY_REQ = 0x02,
  PTP_MESSAGE_TYPE_PDELAY_RESP = 0x03,
  PTP_MESSAGE_TYPE_FOLLOW_UP = 0x08,
  PTP_MESSAGE_TYPE_DELAY_RESP = 0x09,
  PTP_MESSAGE_TYPE_PDELAY_RESP_FOLLOW_UP = 0x0A,
  PTP_MESSAGE_TYPE_ANNOUNCE = 0x0B,
  // TODO: Signaling and Management not supported
  PTP_MESSAGE_TYPE_SIGNALING = 0x0C,
  PTP_MESSAGE_TYPE_MANAGEMENT = 0x0D,
} ptp_message_type_t;

#pragma pack(push, 1)
typedef struct {
  uint8_t clock_identity[8];
  uint16_t port_number;
} ptp_message_port_identity_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  uint8_t message_type : 4;
  uint8_t major_sdo_id : 4;
  uint8_t version : 4;
  uint8_t minor_version : 4;
  uint16_t message_length;
  uint8_t domain_num;
  uint8_t minor_sdo_id;

  union {
    uint16_t raw_val;

    struct {
      /// @brief 0 := the port of the originator is in the MASTER state
      uint8_t alternative_master : 1;
      /// @brief 0 := one step, 1 := two step
      uint8_t two_step : 1;
      /// @brief Target address of this message was a unicast address
      uint8_t unicast : 1;
      uint8_t reserved_2 : 2;
      /// @brief Defined by an alternative PTP template, otherwise 0
      uint8_t profile_specific_1 : 1;
      /// @brief Defined by an alternative PTP template, otherwise 0
      uint8_t profile_specific_2 : 1;
      /// @brief Used for the experimental security mechanism
      uint8_t security : 1;
      uint8_t leap_61 : 1;
      uint8_t leap_59 : 1;
      uint8_t utc_offset_valid : 1;
      uint8_t timescale : 1;
      uint8_t time_tracable : 1;
      uint8_t frequency_tracable : 1;
      uint8_t reserved_1 : 2;
    };
  } flags;

  /// @brief Used in all types of messages (mainly sync) to compensate for the
  ///        transmission delay on the network, this is modified by transparent
  ///        clocks (e.g. switches)
  uint64_t correction_field;
  /// @brief Used to transmit the receive timestamp of some packets on the
  ///        receive side
  uint32_t message_type_specific;
  /// @brief Indicates the ID and port number of the originating clock
  ptp_message_port_identity_t source_port_identity;
  /// @brief Indicates the sequence number of the PTP message and the
  ///        relationship between consecutive PTP messages
  uint16_t sequence_id;
  /// @brief Depending on the message type:
  ///        0x00 := Sync
  ///        0x01 := Delay_Req
  ///        0x02 := Follow_Up
  ///        0x03 := Delay_Resp
  ///        0x04 := Management
  ///        0x05 := All others
  ///        0x06 - 0xFF := Reserved
  uint8_t control_field;
  /// @brief Indicates the interval for sending PTP messages, depending on the
  ///        message type
  ///        Announce := logAnnounceInterval for multicast, 0x7F for unicast
  ///                    messages (TODO: Explain what logAnnounceInterval is...)
  ///        Sync, Follow_Up := logSyncInterval for multicast, 0x7F for unicast
  ///                           (TODO: Explain)
  ///        Delay_Resp := logMinDelayReqInterval for multicast / unicast *or*
  ///                      0x7F for unicast
  ///        Delay_Req, Signaling, Management := 0x7F
  ///        PDelay_Req := 0x7F or a value specified by the applicable PTP
  ///        profile PDelay_Resp, PDelay_Resp_Follow_Up := 0x7F
  uint8_t log_message_interval;
} ptp_message_header_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  /// @brief The seconds portion of the timestamp (big endian)
  uint8_t seconds[6];
  /// @brief The nanosecond portion of the timestamp (big endian)
  uint32_t nanoseconds;
} ptp_message_timestamp_t;
#pragma pack(pop)

typedef enum {
  PTP_TIMESOURCE_ATOMIC = 0x10,
  PTP_TIMESOURCE_GPS = 0x20,
  PTP_TIMESOURCE_TERRESTRIALRADIO = 0x22,
  PTP_TIMESOURCE_PTP = 0x40,
  PTP_TIMESOURCE_NTP = 0x50,
  PTP_TIMESOURCE_HANDSET = 0x60,
  PTP_TIMESOURCE_OTHER = 0x90,
  PTP_TIMESOURCE_INTERNAL_OSCILLATOR = 0xA0,
  // Rest is used for alternative PTP profiles and / or is reserved
} ptp_time_source_t;

typedef enum {
  PTP_CLOCK_CLASS_LOCKED_PRIMARY_REF_CLOCK = 6,
  PTP_CLOCK_CLASS_PRC_UNLOCKED = 7,
  PTP_CLOCK_CLASS_LOCKED_TO_APP_SPEC_TIMESCALE = 13,
  PTP_CLOCK_CLASS_UNLOCKED_FROM_APP_SPEC_TIME = 14,
  PTP_CLOCK_CLASS_PRC_UNLOCKED_OUT_OF_SPEC = 52, // TODO: May also be 187 (?)
  PTP_CLOCK_CLASS_APP_SPECIFIC_UNLOCKED_OUT_OF_SPEC =
      58, // TODO: May also be 193 (?)
  PTP_CLOCK_CLASS_DEFAULT = 248,
  PTP_CLOCK_CLASS_SLAVE_ONLY_CLOCK = 255,
} ptp_clock_class_t;

typedef enum {
  PTP_CLOCK_ACC_25NS = 0x20,
  PTP_CLOCK_ACC_100NS = 0x21,
  PTP_CLOCK_ACC_250NS = 0x22,
  PTP_CLOCK_ACC_1US = 0x23,
  PTP_CLOCK_ACC_2_5US = 0x24,
  PTP_CLOCK_ACC_10US = 0x25,
  PTP_CLOCK_ACC_25US = 0x26,
  PTP_CLOCK_ACC_100US = 0x27,
  PTP_CLOCK_ACC_250US = 0x28,
  PTP_CLOCK_ACC_1MS = 0x29,
  PTP_CLOCK_ACC_2_5MS = 0x2A,
  PTP_CLOCK_ACC_10MS = 0x2B,
  PTP_CLOCK_ACC_25MS = 0x2C,
  PTP_CLOCK_ACC_100MS = 0x2D,
  PTP_CLOCK_ACC_250MS = 0x2E,
  PTP_CLOCK_ACC_1S = 0x2F,
  PTP_CLOCK_ACC_10S = 0x30,
  PTP_CLOCK_ACC_GREATER_10S = 0x31,
  PTP_CLOCK_ACC_UNKNOWN = 0xFE
  // Rest is reserved and / or used for other profiles...
} ptp_clock_accuracy_t;

#pragma pack(push, 1)
typedef struct {
  uint8_t clock_class;
  uint8_t clock_accuracy;
  int16_t scaled_log_variance;
} ptp_message_clock_quality_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  ptp_message_header_t header;
  ptp_message_timestamp_t origin_timestamp;
  uint16_t current_utc_offset;
  uint8_t reserved;
  uint8_t grandmaster_priority_1;
  ptp_message_clock_quality_t grandmaster_clock_quality;
  uint8_t grandmaster_priority_2;
  uint8_t grandmaster_clock_identity[8];
  uint16_t steps_removed;
  uint8_t time_source;
} ptp_message_announce_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  ptp_message_header_t header;
  uint8_t reserved[10];
} ptp_message_sync_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  ptp_message_header_t header;
  ptp_message_timestamp_t origin_timestamp;
} ptp_message_delay_req_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  ptp_message_header_t header;
  ptp_message_timestamp_t precise_origin_timestamp;
} ptp_message_follow_up_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  ptp_message_header_t header;
  ptp_message_timestamp_t receive_timestamp;
  ptp_message_port_identity_t requesting_port_identity;
} ptp_message_delay_resp_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  ptp_message_header_t header;
  ptp_message_timestamp_t origin_timestamp;
  uint8_t reserved[10];
} ptp_message_pdelay_req_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  ptp_message_header_t header;
  ptp_message_timestamp_t request_receipt_timestamp;
  ptp_message_port_identity_t requesting_port_identity;
} ptp_message_pdelay_resp_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  ptp_message_header_t header;
  ptp_message_timestamp_t response_origin_timestamp;
  ptp_message_port_identity_t requesting_port_identity;
} ptp_message_pdelay_resp_follow_up_t;
#pragma pack(pop)

// TODO: Signaling and Management not supported
// #pragma pack(push,1)
// typedef struct {
//     ptp_message_header_t header;
//     ptp_message_port_identity_t target_port_identity;
//     uint8_t tlvs[];
// } ptp_message_signaling_t;
// #pragma pack(pop)

ptp_message_header_t ptp_message_create_header(timesync_clock_t *instance,
                                               ptp_message_type_t message_type);

#ifdef __cplusplus__
}
#endif
#endif /* PTP_MESSAGE_H */
