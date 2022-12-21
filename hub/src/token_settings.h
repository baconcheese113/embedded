#ifndef TOKEN_SETTINGS_H
#define TOKEN_SETTINGS_H

#include <zephyr/kernel.h>

typedef struct {
  char access_token[100];
  bool is_valid;
} token_data_t;

/**
 * Struct with mutatable token to access API_URL as Hub, set once registration is successful
**/
extern token_data_t token_data;

#ifdef __cplusplus
extern "C" {
#endif

  /** Initializes settings and prints token_val if previously set
   * @returns 1 if previously set, 0 otherwise
   */
  uint8_t initialize_token();

  // Saves token_val to NVS
  void save_token(const char new_access_token[100]);

#ifdef __cplusplus
}
#endif

#endif