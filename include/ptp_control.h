/// @file ptp_control.h
///
/// Main library entrypoint
#ifndef PTP_CONTROL_H
#define PTP_CONTROL_H
#ifdef __cplusplus__
extern "C" {
#endif

#include "ptp_message.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// @brief Flags passed to the send callback (ORed)
typedef enum {
  /// @brief Whether the message to send is supposed to be sent multicast
  PTP_CONTROL_SEND_MULTICAST = 0x00,
  /// @brief Whether the message to send is supposed to be sent unicast
  PTP_CONTROL_SEND_UNICAST = 0x01,
  /// @brief Whether the message to send is a PTP event or general message
  PTP_CONTROL_SEND_EVENT = 0x10,
  PTP_CONTROL_SEND_GENERAL = 0x20,
} ptp_send_flags_t;

/// @brief Callback function to retrieve a nanosecond timestamp.
/// @param[in] userdata the userdata pointer taken from the instance
/// @return Timestamp in nanoseconds
typedef uint64_t (*ptp_get_time_ns_cb)(void *userdata);

/// @brief Callback function to set the time (nanosecond time).
/// @param[in] userdata the userdata pointer taken from the instance
/// @param[in] timestamp Timestamp as uint64_t
/// @return True on success
typedef bool (*ptp_set_time_ns_cb)(void *userdata, uint64_t timestamp);

/// @brief Callback function to offset the time (nanosecond offset).
/// @param[in] userdata the userdata pointer taken from the instance
/// @param[in] offset Nanosecond offset as uint64_t
/// @return True on success
typedef bool (*ptp_set_time_offset_ns_cb)(void *userdata, int64_t offset);

/// @brief Callback function to let the calling thread sleep
/// @param[in] amount Time to sleep in milliseconds
typedef void (*ptp_sleep_ms_func)(uint32_t amount);

/// @brief Callback function to adjust the clock period by a given factor
/// @param[in] factor The factor to adjust the clock period with, e.g. 0.8 ->
/// new_freq = old_freq * 0.8
typedef bool (*ptp_adjust_period_cb)(double factor);

/// @brief Callback function to receive a new PTP frame. (**NON-BLOCKING**)
/// @param[in] userdata the userdata pointer taken from the instance
/// @param[out] metadata Receive "metadata" that will get passed back to the
/// send callback, e.g. sender IP + Port information, or nothing
/// @param[in] buffer Buffer to write the received data into
/// @param[in] buffer_size Buffersize
/// @return Amount of bytes received, 0 for no data and <0 for error
typedef int (*ptp_receive_func)(void *userdata, void **metadata,
                                uint8_t *buffer, size_t buffer_size);

/// @brief Callback function to send a new PTP frame.
/// @param[in] userdata the userdata pointer taken from the instance
/// @param[in] flags sending flags
/// @param[in] metadata The "metadata" coming from the last receive call
/// @param[in] buffer Buffer to send
/// @param[in] amount Amount of bytes to send
/// @return Amount of bytes sent
typedef int (*ptp_send_func)(void *userdata, ptp_send_flags_t flags,
                             void *metadata, uint8_t *buffer, size_t amount);

/// @brief Some kind of mutex type
typedef void *ptp_mutex_type_t;

/// @brief Callback function to lock a mutex
/// @param[in] mutex The mutex to lock
typedef void (*ptp_mutex_lock_func)(ptp_mutex_type_t mutex);

/// @brief Callback function to unlock a mutex
/// @param[in] mutex The mutex to unlock
typedef void (*ptp_mutex_unlock_func)(ptp_mutex_type_t mutex);

/// @brief Optional callback function to print debug logs
/// @param[in] userdata the userdata pointer taken from the instance
/// @param[in] text Text to log
typedef void (*ptp_debug_log_func)(void *userdata, const char *text);

typedef struct ptp_delay_info_s {
  uint64_t peer_id;
  uint16_t sequence_id_delay_req;

  // Used for drift calculation
  uint64_t previous_t1;

  uint64_t t1;
  uint64_t t2;
  uint64_t t3;
  uint64_t t4;
  uint64_t t5;
  uint64_t t6;
  uint64_t last_calculated_delay;
} ptp_delay_info_t;

/**
 * @brief Simple double-linked-list helper struct
 **/
typedef struct ptp_delay_info_entry_s {
  ptp_delay_info_t delay_info;
  struct ptp_delay_info_entry_s *previous;
  struct ptp_delay_info_entry_s *next;
} ptp_delay_info_entry_t;

typedef enum ptp_clock_type_e {
  PTP_CLOCK_TYPE_SLAVE,
  PTP_CLOCK_TYPE_MASTER,
} ptp_clock_type_t;

/// @brief  Main PTP instance struct. Create the instance yourself and fill it
///         with the necessary callbacks and mutex.
typedef struct ptp_clock_s {
  /// @brief Callback to get the current time (after receiving a PTP frame)
  ///
  ///        NOTE: You might aswell pass a function that returns the timestamp
  ///        of the last inbound PTP frame! This would be even more
  ///        accurate then.
  ///
  ///        NOTE: For non-event PTP messages (i.e. FOLLOW_UP,
  ///        PDELAY_RESP_FOLLOW_UP, ...) The timestamp is not actually needed,
  ///        you might aswell return 0 in these cases
  ptp_get_time_ns_cb get_time_ns_rx;

  /// @brief Callback to get the current time (after sending a PTP frame)
  ///
  ///        NOTE: You might aswell pass a function that returns the timestamp
  ///        of the last outbound PTP frame! This would be even more
  ///        accurate then.
  ///
  ///        NOTE: For non-event PTP messages (i.e. FOLLOW_UP,
  ///        PDELAY_RESP_FOLLOW_UP, ...) The timestamp is not actually needed,
  ///        you might aswell return 0 in these cases
  ptp_get_time_ns_cb get_time_ns_tx;

  /// @brief Callback to get the current time in nanoseconds
  ptp_get_time_ns_cb get_time_ns;

  /// @brief Callback to set the new time
  ptp_set_time_ns_cb set_time_ns;

  /// @brief Callback to offset the time
  ptp_set_time_offset_ns_cb set_time_offset_ns;

  /// @brief [Opional] Callback to adjust the clock period to the drift from
  /// itself to the master
  ptp_adjust_period_cb adjust_period;

  /// @brief Some kind of sleep function (expects sleep in ms)
  ptp_sleep_ms_func sleep_ms;

  /// @brief  Some kind of receive function that returns a full PTP frame
  ///         (NON-BLOCKING)
  ptp_receive_func receive;

  /// @brief Some kind of send function that sends a PTP frame
  ptp_send_func send;

  /// @brief [Optional] A function used for logging
  ptp_debug_log_func debug_log;

  /// @brief Flag to stop the thread
  bool stop;

  /// @brief Pass your mutex handle and mutex functions here
  ptp_mutex_type_t mutex;
  ptp_mutex_lock_func mutex_lock;
  ptp_mutex_unlock_func mutex_unlock;

  /// @brief In what interval to cyclically send PDELAY_REQ messages in.
  uint32_t pdelay_req_interval_ms;

  /// @brief The domain ID this clock belongs to (0-255)
  uint8_t domain_id;
  /// @brief The major SDO ID this clock belongs to (NOTE: 4 bit)
  uint8_t major_sdo_id : 4;
  /// @brief The minor SDO ID this clock belongs to (NOTE: 4 bit)
  uint8_t minor_sdo_id : 4;

  ptp_message_port_identity_t source_port_identity;

  /// @brief The maximum time in ns to wait for a new Sync+FUP signal before
  /// we're considered to be "out of sync"
  uint64_t sync_loss_timeout_ns;

  /// @brief The maximum deviation from the master time before we're considered
  /// to be out-of-sync at the time of the Sync+FUP signal
  uint64_t sync_loss_threshold_ns;

  /// @brief Infos used for the announce messages or in the case of slave mode
  /// the information of the master
  struct {
    uint32_t announce_msg_interval_ms;
    uint32_t sync_msg_interval_ms;
    uint16_t utc_offset;
    uint8_t grandmaster_priority_1;
    ptp_message_clock_quality_t clock_quality;
    uint8_t grandmaster_priority_2;
    uint8_t grandmaster_clock_identity[8];
    uint16_t steps_removed;
    uint8_t time_source;
  } master;

  /// @brief Whether this clock is acting as a master or slave
  ptp_clock_type_t clock_type;

  /// @brief Whether to use P2P for delay calculation instead of E2E
  bool use_p2p;

  /// @brief Arbitrary userdata that will be passed to every callback function
  void *userdata;

  // Internal variables, just set these to 0
  ptp_delay_info_entry_t *delay_infos;
  uint64_t latest_t3;
  uint64_t fup_received;
  uint64_t last_ts_after_correction;

  /// @brief Statistics that are accessible at any time
  struct {
    /// @brief Last time we synced
    uint64_t last_sync_ts;
    /// @brief Last delay value
    uint64_t last_delay_ns;
    /// @brief Last offset value
    int64_t last_offset_ns;
    /// @brief Sync Loss Count
    uint32_t sync_loss_count;
    /// @brief The maximum time that passed until a sync
    uint32_t max_update_time;
    /// @brief Whether we're currently in sync with the master
    bool in_sync;
  } statistics;
} ptp_clock_t;

/// @brief  This is the PDelay_REQ thread that cyclically sends out PDELAY_REQ
///         messages
///
///         Run this as a thread function and pass your instance as the thread
///         data
/// @param[in] instance The PTP instance
void ptp_pdelay_req_thread_func(ptp_clock_t *instance);

/// @brief  This a thread function that cyclically receives data and parses PTP
///         messages in order to handle them.
///
///         Run this as a thread function and pass your instance as the thread
///         data
/// @param[in] instance The PTP instance
void ptp_rx_thread_func(ptp_clock_t *instance);

/// @brief  This a thread function that cyclically sends SYNC + FUP & ANNOUNCE
/// messages as a master.
///
///         Run this as a thread function and pass your instance as the thread
///         data
///
///         NOTE: If your instance is a slave you won't need this thread.
///
/// @param[in] instance The PTP instance
void ptp_tx_thread_func(ptp_clock_t *instance);

// bool ptp_handle_message(timesync_clock_t *instance, uint8_t *rx_buf,
//                         size_t len);
//
// bool ptp_build_sync_message(timesync_clock_t *instance, uint8_t *tx_buf,
//                             size_t len);
// bool ptp_build_fup_message(timesync_clock_t *instance, uint8_t *tx_buf,
//                            size_t len);
//
// bool ptp_build_pdelay_req_message(timesync_clock_t *instance, uint8_t
// *tx_buf,
//                                   size_t len);
// bool ptp_build_pdelay_resp_message(timesync_clock_t *instance, uint8_t
// *tx_buf,
//                                    size_t len);
// bool ptp_build_pdelay_resp_fup_message(timesync_clock_t *instance,
//                                        uint8_t *tx_buf, size_t len);

#ifdef __cplusplus__
}
#endif
#endif
