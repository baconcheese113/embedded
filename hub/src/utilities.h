#ifndef HUB_UTILITIES_H
#define HUB_UTILITIES_H

#include <zephyr/drivers/uart.h>
#include <cJSON.h>

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
  void write_rgb(uint8_t r, uint8_t g, uint8_t b, bool print = false);

  /// @brief Watch the LEDs dance and rejoice 
  void happy_dance(void);

  /**
   * Parses BLE char arrays separated by a colon ( : ) delimeter into a Command struct
   * Prints an error message if unable to parse
  **/
  Command parse_raw_command(char* raw_cmd);

  /**
   * Reads n bytes into buffer (ignoring head) from Serial1
   * Returns true if OK received, false otherwise
  **/
  bool readUntilResp(const char* head, char* buffer, uint16_t timeout = 1000);

  /**
   * @brief Shortcut to parse through n nested objects in a cJSON object
   * @param parent the starting cJSON object from which to search
   * @param path an array of JSON object names to search through
   * @return A pointer to the found cJSON object or NULL
  */
  // cJSON* cJSON_GetNested(cJSON* parent, const char* const path[]);
}

#endif