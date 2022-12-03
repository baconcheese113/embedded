#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include "serial.h"

/* receive buffer used in UART ISR callback */
static char rx_buf[MSG_SIZE];
static uint8_t rx_buf_pos;

// queue to store up to 10 messages (aligned to 4-byte boundary)
K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, 10, 4);

static const struct device* uart0_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));

static const struct device* uart1_dev = DEVICE_DT_GET(DT_NODELABEL(uart1));

/*
 * Read characters from UART until line end is detected. Afterwards push the
 * data to the message queue.
 */
static void serial_cb(const struct device* dev, void* user_data)
{
  uint8_t c;

  if (!uart_irq_update(uart1_dev)) {
    return;
  }

  while (uart_irq_rx_ready(uart1_dev)) {

    uart_fifo_read(uart1_dev, &c, 1);
    bool is_break = c == '\n' || c == '\r';

    if (rx_buf_pos == 0 && is_break) continue;

    if (!is_break) rx_buf[rx_buf_pos++] = c;

    if ((rx_buf_pos == sizeof(rx_buf) - 1) || is_break) {
      rx_buf[rx_buf_pos] = '\0';

      /* if queue is full, message is silently dropped */
      k_msgq_put(&uart_msgq, &rx_buf, K_NO_WAIT);

      /* reset the buffer (it was copied to the msgq) */
      rx_buf_pos = 0;
    }
  }
}

void serial_init(void) {
  if (!device_is_ready(uart1_dev)) {
    printk("UART device not found!\n");
    return;
  }

  /* configure interrupt and callback to receive data */
  uart_irq_callback_user_data_set(uart1_dev, serial_cb, NULL);
  uart_irq_rx_enable(uart1_dev);
  printk("\tSerial online\n");
}

void serial_print_uart(const char* buf) {
  int msg_len = strlen(buf);

  for (int i = 0; i < msg_len; i++) {
    uart_poll_out(uart1_dev, buf[i]);
  }
}

void serial_purge(void) {
  k_msgq_purge(&uart_msgq);
}

bool serial_did_return_ok(int64_t timeout, bool print) {
  return serial_did_return_str("OK", timeout, print);
}

bool serial_did_return_str(const char* str, int64_t timeout, bool print) {
  char tx_buf[MSG_SIZE];
  int64_t start_time = k_uptime_get();
  while (k_uptime_get() < start_time + timeout) {
    if (k_msgq_get(&uart_msgq, &tx_buf, K_NO_WAIT) == 0) {
      if (print) printk("\tSIM7000 says: %s\n", tx_buf);
      if (strncmp(str, tx_buf, strlen(str)) == 0) {
        return true;
      } else if (strncmp("ERROR", tx_buf, 5) == 0) {
        return false;
      }
    }
  }
  return false;
}

bool serial_read_queue(char* out_buf, int64_t timeout, bool print) {
  char tx_buf[MSG_SIZE];
  int64_t start_time = k_uptime_get();
  while (k_uptime_get() < start_time + timeout) {
    if (k_msgq_get(&uart_msgq, &tx_buf, K_NO_WAIT) == 0) {
      if (print) printk("\tSIM7000 says: %s\n", tx_buf);
      strcpy(out_buf, tx_buf);
      return true;
    }
  }
  return false;
}

bool serial_read_raw_until(const char* str, char* out_buf, int64_t timeout) {
  char tx_buf[MSG_SIZE];
  int64_t start_time = k_uptime_get();
  while (k_uptime_get() < start_time + timeout) {
    if (k_msgq_get(&uart_msgq, &tx_buf, K_NO_WAIT) == 0) {
      printk("\tSIM7000 says: %s\n", tx_buf);
      uint8_t left_padding = 0;
      if (strncmp(str, tx_buf, strlen(str)) == 0) {
        return true;
      }
      while (tx_buf[left_padding] == ' ') left_padding++;
      strcpy(out_buf + strlen(out_buf), tx_buf + left_padding);
    }
  }
  return false;
}

bool serial_infinite_io() {
  char tx_buf[MSG_SIZE]{};
  unsigned char c;
  while (1) {
    while (k_msgq_get(&uart_msgq, &tx_buf, K_NO_WAIT) == 0) {
      tx_buf[MSG_SIZE - 1] = '\0';
      printk("Echo: %s\r\n", tx_buf);
    }

    while (uart_poll_in(uart0_dev, &c) == 0) {
      printk("%c", c);
      uart_poll_out(uart1_dev, c);
    }

  }
}
