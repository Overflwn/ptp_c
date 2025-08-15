#include <chrono>
#include <gtest/gtest.h>
// #include <iomanip>
#include <thread>

/*
typedef uint64_t (*get_time_ns_cb)(void);
typedef bool (*set_time_ns_cb)(uint64_t);
typedef bool (*set_time_offset_ns_cb)(int64_t);
typedef void (*sleep_ms_func)(uint32_t);
typedef int (*receive_func)(void **, uint8_t *, size_t);
typedef int (*send_func)(ptp_control_send_type_t, void *, uint8_t *, size_t);
typedef void *ptp_mutex_type_t;
typedef void (*ptp_mutex_lock_func)(ptp_mutex_type_t);
typedef void (*ptp_mutex_unlock_func)(ptp_mutex_type_t);
*/

extern "C" {
#include "ptp_control.h"
#include "ptp_message.h"
static uint64_t cur_time = 0;
static uint8_t *send_buf = NULL;
static size_t send_size = 0;
static uint32_t last_sleep = 0;
static int sleeped = 0;
static int sent = 0;

static void test_cleanup() {
  if (send_buf) {
    free(send_buf);
    send_buf = NULL;
  }
  cur_time = 0;
  send_size = 0;
  last_sleep = 0;
  sleeped = 0;
  sent = 0;
}

uint64_t get_time_ns(void *userdata) { return cur_time; }

bool set_time_ns(void *userdata, uint64_t new_time) {
  cur_time = new_time;
  return true;
}

bool set_time_offset(void *userdata, int64_t offset) {
  if (offset < 0 && cur_time < (uint64_t)(offset * -1)) {
    return false;
  }
  cur_time += offset;
  return true;
}

void sleep_ms(uint32_t ms) {
  last_sleep = ms;
  sleeped++;
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

int receive(void *userdata, void **metadata, uint8_t *buf, size_t buf_size) {
  return 0;
}
int send(void *userdata, ptp_send_flags_t send_type, void *metadata,
         uint8_t *buf, size_t amount) {
  if (send_buf) {
    free(send_buf);
    send_size = 0;
  }

  send_buf = (uint8_t *)malloc(sizeof(uint8_t) * amount);
  memcpy(send_buf, buf, amount);
  send_size = amount;
  sent++;
  return send_size;
}

void mut_lock(ptp_mutex_type_t mutex) {}
void mut_unlock(ptp_mutex_type_t mutex) {}

void debug_log(void *userdata, const char *txt) {
  std::cout << txt << std::endl;
}

} // extern "C"

TEST(PtpMessageTest, BasicAssertions) {
  EXPECT_STRNE("hello", "world");
  EXPECT_EQ(7 * 6, 42);
}

TEST(PtpControl, ControlSetup) {
  // Test whether the library correctly sets itself up and utilizes the given
  // callbacks
  ptp_clock_t clock = {0};
  clock.domain_id = 1;
  clock.get_time_ns_rx = get_time_ns;
  clock.get_time_ns_tx = get_time_ns;
  clock.set_time_ns = set_time_ns;
  clock.set_time_offset_ns = set_time_offset;
  clock.sleep_ms = sleep_ms;
  clock.receive = receive;
  clock.send = send;
  clock.mutex = NULL;
  clock.mutex_lock = mut_lock;
  clock.mutex_unlock = mut_unlock;
  clock.pdelay_req_interval_ms = 500;
  clock.use_p2p = true;

  std::thread t(ptp_pdelay_req_thread_func, &clock);
  // Sleep less than the other thread is supposed to
  // -> thread should've sent only once (and sent only once)
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  clock.stop = true;
  t.join();
  EXPECT_TRUE(send_buf != nullptr);
  EXPECT_TRUE(send_size == sizeof(ptp_message_pdelay_req_t));
  EXPECT_EQ(sleeped, 1);
  EXPECT_EQ(sent, 1);
  EXPECT_EQ(last_sleep, 500);

  // TODO: Test receive thread

  // if (send_buf) {
  //   for (int i = 0; i < send_size; i++) {
  //     std::cout << ",0x" << std::setw(2) << std::setfill('0') << std::hex
  //               << static_cast<int>(send_buf[i]);
  //   }
  //   std::cout << std::endl;
  // }
  test_cleanup();
}

TEST(PtpControl, P2pDisabled) {
  // Test whether the library correctly sets itself up and utilizes the given
  // callbacks
  ptp_clock_t clock = {0};
  clock.domain_id = 1;
  clock.get_time_ns_rx = get_time_ns;
  clock.get_time_ns_tx = get_time_ns;
  clock.set_time_ns = set_time_ns;
  clock.set_time_offset_ns = set_time_offset;
  clock.sleep_ms = sleep_ms;
  clock.receive = receive;
  clock.send = send;
  clock.mutex = NULL;
  clock.mutex_lock = mut_lock;
  clock.mutex_unlock = mut_unlock;
  clock.pdelay_req_interval_ms = 500;
  clock.use_p2p = false;

  std::thread t(ptp_pdelay_req_thread_func, &clock);
  // Sleep less than the other thread is supposed to
  // -> thread should've sent only once (and sent only once)
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  clock.stop = true;
  t.join();
  EXPECT_TRUE(send_buf == nullptr);
  EXPECT_EQ(send_size, 0);
  EXPECT_EQ(sleeped, 1);
  EXPECT_EQ(sent, 0);
  EXPECT_EQ(last_sleep, 500);

  test_cleanup();
}

TEST(PtpControl, ValidPDelayRequest) {
  // Test whether the library correctly sets itself up and utilizes the given
  // callbacks
  const uint8_t valid_packet[] = {
      0x02, 0x02, 0x00, 0x36, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02,
      0x03, 0x04, 0x05, 0x06, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x05,
      0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  ptp_clock_t clock = {0};
  clock.domain_id = 1;
  clock.get_time_ns_rx = get_time_ns;
  clock.get_time_ns_tx = get_time_ns;
  clock.set_time_ns = set_time_ns;
  clock.set_time_offset_ns = set_time_offset;
  clock.sleep_ms = sleep_ms;
  clock.receive = receive;
  clock.send = send;
  clock.mutex = NULL;
  clock.mutex_lock = mut_lock;
  clock.mutex_unlock = mut_unlock;
  clock.pdelay_req_interval_ms = 500;
  clock.use_p2p = true;
  clock.source_port_identity.port_number = 0x1;
  clock.source_port_identity.clock_identity[0] = 0x1;
  clock.source_port_identity.clock_identity[1] = 0x2;
  clock.source_port_identity.clock_identity[2] = 0x3;
  clock.source_port_identity.clock_identity[3] = 0x4;
  clock.source_port_identity.clock_identity[4] = 0x5;
  clock.source_port_identity.clock_identity[5] = 0x6;

  std::thread t(ptp_pdelay_req_thread_func, &clock);
  // Sleep less than the other thread is supposed to
  // -> thread should've sent only once (and sent only once)
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  clock.stop = true;
  t.join();
  EXPECT_TRUE(send_buf != nullptr);
  EXPECT_TRUE(send_size == sizeof(ptp_message_pdelay_req_t));
  EXPECT_EQ(sleeped, 1);
  EXPECT_EQ(sent, 1);
  EXPECT_EQ(last_sleep, 500);
  EXPECT_EQ(memcmp(send_buf, valid_packet, sizeof(ptp_message_pdelay_req_t)),
            0);

  // if (send_buf) {
  //   for (int i = 0; i < send_size; i++) {
  //     std::cout << ",0x" << std::setw(2) << std::setfill('0') << std::hex
  //               << static_cast<int>(send_buf[i]);
  //   }
  //   std::cout << std::endl;
  // }
  test_cleanup();
}
