#ifndef PTP_CONTROL_H
#define PTP_CONTROL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint64_t (*get_time_ns_cb)(void);
typedef bool (*set_time_ns_cb)(uint64_t);
typedef bool (*set_time_offset_ns_cb)(int64_t);
typedef void (*sleep_ms_func)(uint32_t);
typedef int (*receive_func)(uint8_t *, size_t);
typedef int (*send_func)(uint8_t *, size_t);
typedef void *ptp_mutex_type_t;
typedef void (*ptp_mutex_lock_func)(ptp_mutex_type_t);
typedef void (*ptp_mutex_unlock_func)(ptp_mutex_type_t);

typedef struct ptp_delay_info_s {
  uint64_t peer_id;
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

/// @brief  Main PTP instance struct. Create the instance yourself and fill it
///         with the necessary callbacks and mutex.
typedef struct timesync_clock_s {
  /// @brief Callback to get the current time
  get_time_ns_cb get_time_ns;

  /// @brief Callback to set the new time
  set_time_ns_cb set_time_ns;

  /// @brief Callback to offset the time
  set_time_offset_ns_cb set_time_offset_ns;

  /// @brief Some kind of sleep function (expects sleep in ms)
  sleep_ms_func sleep_ms;

  /// @brief  Some kind of receive function that returns a full PTP frame
  ///         (BLOCKING)
  receive_func receive;

  /// @brief Some kind of send function that sends a PTP frame
  send_func send;

  /// @brief Flag to stop the thread
  bool stop;

  /// @brief Pass your mutex handle and mutex functions here
  ptp_mutex_type_t *mutex;
  ptp_mutex_lock_func mutex_lock;
  ptp_mutex_unlock_func mutex_unlock;

  ptp_delay_info_entry_t *delay_infos;

  /// @brief In what interval to cyclically send PDELAY_REQ messages in.
  uint64_t pdelay_req_interval_ms;

  // Internal variables, just set these to 0
  uint64_t current_delay_ns;
  uint64_t latest_t3;
} timesync_clock_t;

/// @brief  This is the PDelay_REQ thread that cyclically sends out PDELAY_REQ
///         messages
///         Run this as a thread function and pass your instance as the thread
///         data
/// @param[in] instance The PTP instance
void ptp_req_thread_func(timesync_clock_t *instance);

/// @brief  This a thread function that cyclically receives data and parses PTP
///         messages in order to handle them.
///         Run this as a thread function and pass your instance as the thread
///         data
/// @param[in] instance The PTP instance
void ptp_thread_func(timesync_clock_t *instance);

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

#endif
