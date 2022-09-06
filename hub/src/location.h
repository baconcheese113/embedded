#ifndef HUB_LOCATION_H
#define HUB_LOCATION_H

struct LocReading {
  bool hasFix = false;
  double lat = 0;
  double lng = 0;
  double kmph = 0;
  double deg = 0;
  double hdop = 0;
};

// Interval is the amount of time between checks
const unsigned long GPS_UPDATE_INTERVAL = 29.5 * 60 * 1000;
// When a check is ready to occur, the module is 
// powered on for this amount of time before reading
const unsigned long GPS_BUFFER_TIME = 20000;

class Location
{

private:
  static double get_radians(double degrees);

public:
  // The last time (in millis) that location was queried
  unsigned long last_gps_time = 0;

  // The last sent LocReading sent to the server
  LocReading last_sent_reading;

  // If the GPS module is powered on (should be off on init)
  bool is_powered = false;

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
   * Powers on/off GPS module
   * @return True if received OK response before timeout
   */
  bool set_gps_power(bool turn_on);
};

#endif