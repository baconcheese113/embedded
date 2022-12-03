#ifndef HUB_LOCATION_H
#define HUB_LOCATION_H

#include "network_requests.h"
#include "network.h"

struct LocReading {
  bool hasFix = false;
  double lat = 0;
  double lng = 0;
  double kmph = 0;
  double deg = 0;
  double hdop = 0;
};

// Interval is the amount of time between checks
const unsigned long GPS_UPDATE_INTERVAL = 15 * 60 * 1000;
// When a check is ready to occur, the module is powered
// on up to this amount of time
const unsigned long GPS_BUFFER_TIME = 60000;

class Location
{

private:
  static double get_radians(double degrees);

  Network* network;
  NetworkRequests* network_reqs;

  /**
   * @brief convienience function to turn off location/network and print a msg
   * @param msg the string to print while shutting down
   */
  void turn_off(const char* msg);

  // The last time (in millis) that location was queried
  unsigned long last_gps_time = 0;

  // If greater than 0, a warm up was kicked off at this ms time
  int64_t warm_up_start_time = 0;

  // The last sent LocReading sent to the server
  LocReading last_sent_reading;

  // If the GPS module is powered on (should be off on init)
  bool is_powered = false;
public:

  static void print_loc_reading(LocReading reading);

  /**
   * Returns the distance (in meters) between 2 locations
   */
  static double distance(double lat1, double lng1, double lat2, double lng2);

  /**
   * Returns the distance (in meters) between passed in point
   * and lastSent point
   */
  double distance_from_last_point(double lat, double lng) {
    return distance(lat, lng, last_sent_reading.lat, last_sent_reading.lng);
  }

  /**
   * Parse a line received from the AT+CGNSINF command
   * and return a LocReading struct
   */
  static LocReading parse_inf(char* inf_buffer);

  /**
   * @brief Set up pointers needed for network requests
   */
  void init(Network* net, NetworkRequests* network_requests);

  /**
   * @return true if it's time to send another network request and
   * send_update() should be called
   */
  bool should_send_update();

  /**
   * @return true if GPS_INTERVAL has passed since last update and
   * haven't already started warming up
   */
  bool should_warm_up();

  /**
   * @brief Starts warm up process of turning on modem, should_send_update
   * should be polled until send_update can be called
   * @return 0 on success, -1 for any failure starting modem
   */
  int start_warm_up();

  /**
   * @brief Blocks while warming up the modem and reading location
   * then attempts to send it across the network
   * @return 0 on success, -1 for any failures
   */
  int send_update();

  /**
   * Powers on/off GPS module
   * @return True if received OK response before timeout
   */
  bool set_gps_power(bool turn_on);
};

#endif