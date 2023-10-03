#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include "diagnostic.h"

static NetworkRequests *network_reqs;
static Network *network;

bool diagnostic_running = false;

static char* msg_result;

static struct k_work work;
static struct k_work_q diagnostic_work_q;
K_THREAD_STACK_DEFINE(diagnostic_stack_area, 2048);
const k_work_queue_config diagnostic_work_q_config = {
  .name = "diagnostic_work_q",
};

void diagnostic_init(NetworkRequests* network_requests, Network* net) {
  network_reqs = network_requests;
  network = net;

  k_work_queue_start(&diagnostic_work_q, diagnostic_stack_area,
    K_THREAD_STACK_SIZEOF(diagnostic_stack_area),
    CONFIG_SYSTEM_WORKQUEUE_PRIORITY + 1, &diagnostic_work_q_config);
}

static void diagnostic_work(struct k_work* work_item) {
  printk("\n***** Running diagnostic *****\n");
  if(!network || !network_reqs) {
    printk("\tNetwork or network requests not initialized\n");
    return;
  }
  diagnostic_running = true;

  PreferredMode modes[] = {
    PreferredMode::AUTOMATIC,
    PreferredMode::GSM,
    PreferredMode::LTE,
    PreferredMode::BOTH,
  };
  int64_t total_time[] = {-1, -1, -1, -1};
  int8_t fastest_mode = -1;
  
  for (int8_t i = 0; i < 4; i++) {
    network->set_power(false);
    k_msleep(500);

    PreferredMode mode = modes[i];
    int64_t start_time = k_uptime_get();
    printk("<<<>>> ***** Test Mode %u *****\n", (uint8_t) mode);
    network->set_power(true);
    
    if (!network->wait_for_power_on()) {
      printk("\tWait for power on failed for mode %u\n", (uint8_t) mode);
      continue;
    }

    if (!network->set_preferred_mode(mode)) {
      printk("\tSet preferred mode failed\n");
      continue;
    }

    if (!network->set_power_on_and_wait_for_reg()) {
      printk("\tSet power on and wait failed\n");
      continue;
    }

    if(!network->send_test_request()) {
      if(msg_result) snprintk(msg_result, 50, "Test mode %u [FAIL]: %lldms\n", (uint8_t) modes[i], total_time[i]);
      printk("\tSend test request failed\n");
      continue;
    }
    printk("<<<>>> Send test request success!\n");
    total_time[i] = k_uptime_get() - start_time;
    if(fastest_mode == -1 || total_time[i] < total_time[fastest_mode]) {
      fastest_mode = i;
    }
    printk("<<<>>> Total time in test mode %u: %lld\n", (uint8_t) modes[i], total_time[i]);
    if(msg_result) snprintk(msg_result, 40, "Total time in mode %u: %lldms\n", (uint8_t) modes[i], total_time[i]);
  }

  printk("<<<>>> ***** Printing summary *****\n");
  for (uint8_t i = 0; i < 4; i++) {
    printk("<<<>>> Total time in test mode %u: %lld%c\n", (uint8_t) modes[i], total_time[i], i == fastest_mode ? '*' : ' ');
  }
  if(fastest_mode == -1) {
    printk("<<<>>> No mode succeeded\n");
  } else {
    if(!network->is_powered_on()) {
      printk("<<<>>> Powering on module to set preferred mode...\n");
      network->set_power(true);
      network->wait_for_power_on();
    }
    PreferredMode mode = modes[fastest_mode];
    bool success = network->set_preferred_mode(mode);
    printk("<<<>>> Set fastest mode %u: %s\n", (uint8_t) mode, success ? "success" : "fail");
  }

  network->set_power(false);

  printk("**** Diagnostics complete *****\n");

  diagnostic_running = false;
}

int diagnostic_run(char* out_result_msg) {
  if(k_work_busy_get(&work) > 0) {
    printk("Diagnostic already running\n");
    return -1;
  }
  msg_result = out_result_msg;
  k_work_init(&work, diagnostic_work);
  k_work_submit_to_queue(&diagnostic_work_q, &work);
  return 0;
}