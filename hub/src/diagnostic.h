#ifndef HUB_DIAGNOSTIC_H
#define HUB_DIAGNOSTIC_H

#include "network_requests.h"
#include "network.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * @brief Setup pointers needed for network requests
   * @param network_requests Pointer to network requests instance
   * @param net Pointer to network instance
   */
  void diagnostic_init(NetworkRequests *network_requests, Network *net);

  /**
   * @brief Run a full diagnostic test.
   * @return 0 on success, other numbers on error
   */
  int diagnostic_run();

#ifdef __cplusplus
}
#endif

#endif