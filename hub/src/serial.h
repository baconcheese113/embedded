#ifndef SERIAL_UART_H
#define SERIAL_UART_H

#include <zephyr/kernel.h>

#define MSG_SIZE 32

/* queue to store up to 10 messages (aligned to 4-byte boundary) */
extern struct k_msgq uart_msgq;

#ifdef __cplusplus
extern "C" {
#endif
  // Sets up uart1 device, registers IRQ and message queue
  void serial_init(void);

  /*
   * @brief Prints a null-terminated string character by character to the UART1 interface
   * @param buf the char array to print, needs to be null terminated
   */
  void serial_print_uart(const char* buf);

  /**
   * @brief Purges out the message queue
   */
  void serial_purge(void);

  /**
   * @brief Blocks while waiting for a response of `OK`
   * @param timeout ms to wait for OK response before returning false
   * @return True if `OK` received before timeout
   */
  bool serial_did_return_ok(int64_t timeout);

#ifdef __cplusplus
}
#endif

#endif