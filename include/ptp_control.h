#ifndef PTP_CONTROL_H
#define PTP_CONTROL_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "ptp_message.h"

typedef uint64_t (*get_time_ns_cb)(void);
typedef bool (*set_time_ns_cb)(uint64_t);
typedef bool (*set_time_offset_ns_cb)(int64_t);
typedef void (*sleep_ms_func)(uint32_t);
typedef size_t (*receive_func)(uint8_t*, size_t);
typedef size_t (*send_func)(uint8_t*, size_t);

typedef struct timesync_clock_s
{
    /// @brief Callback to get the current time
    get_time_ns_cb get_time_ns;

    /// @brief Callback to set the new time
    set_time_ns_cb set_time_ns;

    /// @brief Callback to offset the time
    set_time_offset_ns_cb set_time_offset_ns;

    /// @brief Some kind of sleep function (expects sleep in ms)
    sleep_ms_func sleep_ms;

    /// @brief Some kind of receive function that returns a full PTP frame (NON BLOCKING)
    receive_func receive;

    /// @brief Some kind of send function that sends a PTP frame
    send_func send;

    /// @brief Flag to stop the thread
    bool stop;

    uint64_t current_delay_ns;
} timesync_clock_t;

/// @brief Run this as a thread function and pass your instance as the thread data
void ptp_thread_func(timesync_clock_t *instance);

bool ptp_handle_message(timesync_clock_t *instance, uint8_t *rx_buf, size_t len);

bool ptp_build_sync_message(timesync_clock_t *instance, uint8_t *tx_buf, size_t len);
bool ptp_build_fup_message(timesync_clock_t *instance, uint8_t *tx_buf, size_t len);

bool ptp_build_pdelay_req_message(timesync_clock_t *instance, uint8_t *tx_buf, size_t len);
bool ptp_build_pdelay_resp_message(timesync_clock_t *instance, uint8_t *tx_buf, size_t len);
bool ptp_build_pdelay_resp_fup_message(timesync_clock_t *instance, uint8_t *tx_buf, size_t len);

#endif
