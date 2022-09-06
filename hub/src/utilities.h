#ifndef HUB_UTILITIES_H
#define HUB_UTILITIES_H

#include <zephyr/drivers/uart.h>

struct Command {
  char type[30]{};
  char value[50]{};
};


namespace Utilities {

  /**
   * Single place to register all used I/O pins
   */
  int setup_pins(void);

  /**
   * Writes to the RGB pins the RGB values from 0 - 255
  **/
  void rgb_write(uint8_t r, uint8_t g, uint8_t b, bool print = false);

  /// @brief Watch the LEDs dance and rejoice 
  void happy_dance(void);

  /**
   * Parses BLE char arrays separated by a colon ( : ) delimeter into a Command struct
   * Prints an error message if unable to parse
  **/
  Command parseRawCommand(char* rawCmd);

  /**
   * Reads n bytes into buffer (ignoring head) from Serial1
   * Returns true if OK received, false otherwise
  **/
  bool readUntilResp(const char* head, char* buffer, uint16_t timeout = 1000);

}

#endif