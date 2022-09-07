#ifndef SERIAL_UART_H
#define SERIAL_UART_H

#include <stdint.h>

#define MSG_SIZE 128

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
   * @brief Shorthand for calling serial_did_return_str with "OK"
   * @param timeout ms to wait for OK response before returning false
   * @param print [optional] Whether to print the received msgs. Default is true
   * @return True if `OK` received before timeout
   */
  bool serial_did_return_ok(int64_t timeout, bool print = true);

  /**
  * @brief Blocks while waiting for a response starting with str
  * @param str the exact string to wait for
  * @param timeout ms to wait for message before returning false
  * @param print [optional] Whether to print the received msgs. Default is true
  * @return True if `OK` received before timeout
  */
  bool serial_did_return_str(const char* str, int64_t timeout, bool print = true);

  /**
   * @brief Blocks while waiting for a response to read into out_buf
   * @param out_buf the buffer to read the message into
   * @param timeout ms to wait for message before returning false
   * @param print [optional] Whether to print the received msgs. Default is true
   * @return True if a message was received before timeout
   */
  bool serial_read_queue(char* out_buf, int64_t timeout, bool print = true);

  /**
   * @brief Blocks while concatenating all message queue messages until
   * a message starting with str is read
   * @param str the exact string to read until
   * @param out_buf the buffer to copy the full string into if str is read
   * @param timeout ms to wait for str before returning false
   * @return True if a message starting with str was read
   */
  bool serial_read_raw_until(const char* str, char* out_buf, int64_t timeout);

#ifdef __cplusplus
}
#endif

#endif