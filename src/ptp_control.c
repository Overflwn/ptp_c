#include "ptp_control.h"
#include "ptp_message.h"

void ptp_thread_func(timesync_clock_t *instance)
{
    uint8_t rx_buf[2048];
    while (!instance->stop)
    {
        int amount_received = instance->receive(rx_buf, sizeof(rx_buf));
        if (amount_received > 0)
        {
            ptp_message_header_t *header = (ptp_message_header_t*)rx_buf;
            switch (header->message_type)
            {
                case PTP_MESSAGE_TYPE_SYNC:
                {
                    ptp_message_sync_t *msg = (ptp_message_sync_t*)rx_buf;
                    // TODO: Handle
                    break;
                }
                case PTP_MESSAGE_TYPE_DELAY_REQ:
                {
                    // TODO: We currently don't support master mode
                    break;
                }
                case PTP_MESSAGE_TYPE_FOLLOW_UP:
                {

                    break;
                }
                case PTP_MESSAGE_TYPE_DELAY_RESP:
                {

                    break;
                }
                case PTP_MESSAGE_TYPE_PDELAY_REQ:
                {

                    break;
                }
                case PTP_MESSAGE_TYPE_PDELAY_RESP:
                {

                    break;
                }
                case PTP_MESSAGE_TYPE_PDELAY_RESP_FOLLOW_UP:
                {
                    break;
                }
            }
        }
        instance->sleep_ms(1);
    }
}
