#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include "serial.h"

/* receive buffer used in UART ISR callback */
static char rx_buf[MSG_SIZE];
static uint8_t rx_buf_pos;

K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, 10, 4);

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

    if (is_break && rx_buf_pos > 0) {
      /* terminate string */
      rx_buf[rx_buf_pos] = '\0';

      /* if queue is full, message is silently dropped */
      k_msgq_put(&uart_msgq, &rx_buf, K_NO_WAIT);

      /* reset the buffer (it was copied to the msgq) */
      rx_buf_pos = 0;
    } else if (!is_break) {
      if (rx_buf_pos < (sizeof(rx_buf) - 1)) {
        rx_buf[rx_buf_pos++] = c;
      } else {
        /* else: characters beyond buffer size are dropped */
        printk("ERROR: Buffer length exceeded\n");
      }
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

bool serial_did_return_ok(int64_t timeout) {
  char tx_buf[MSG_SIZE];
  int64_t start_time = k_uptime_get();
  while (k_uptime_get() < start_time + timeout) {
    if (k_msgq_get(&uart_msgq, &tx_buf, K_NO_WAIT) == 0) {
      printk("\tSIM7000 says: %s\n", tx_buf);
      if (strcmp("OK", tx_buf) == 0) {
        return true;
      }
    }
  }
  return false;
}