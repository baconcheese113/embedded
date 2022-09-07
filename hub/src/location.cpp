#include <zephyr/sys/printk.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "serial.h"
#include "location.h"
#include "utilities.h"

// TODO find from a static library
#define M_PI           3.14159265358979323846

double Location::get_radians(double degrees) {
  return degrees * M_PI / 180;
}

void Location::print_loc_reading(LocReading reading) {
  printk("Latitude: %f, Longitude: %f, HDOP(m): %f"
    ", Speed(kmph): %f, Course(deg): %f\n",
    reading.lat,
    reading.lng,
    reading.hdop,
    reading.kmph,
    reading.deg
  );
}

double Location::distance(double lat1, double lng1, double lat2, double lng2) {
  lat1 = Location::get_radians(lat1);
  lng1 = Location::get_radians(lng1);
  lat2 = Location::get_radians(lat2);
  lng2 = Location::get_radians(lng2);

  double lng_distance = lng2 - lng1;
  double lat_distance = lat2 - lat1;

  double dist = pow(sin(lat_distance / 2), 2) + cos(lat1) * cos(lat2) * pow(sin(lng_distance / 2), 2);
  dist = 2 * asin(sqrt(dist));
  // Radius of Earth in KM, 6371 or 3956 miles. Multiplied by 100 to convert from km to m
  return dist *= 6371.0 * 1000.0;
}

LocReading Location::parse_inf(char* inf_buffer) {
  uint8_t param_num = 0;
  uint8_t param_start = 0;
  char temp_buf[20]{};
  LocReading reading;
  memset(temp_buf, 0, 20);
  for (uint8_t idx = 0; idx < strlen(inf_buffer); idx++) {
    if (inf_buffer[idx] == ',') {
      temp_buf[idx - param_start] = '\0';
      if (param_start < idx) {
        if (param_num == 1) {
          reading.hasFix = temp_buf[0] == '1';
        } else if (param_num == 3) { // Lat
          reading.lat = atof(temp_buf);
        } else if (param_num == 4) { // Lng
          reading.lng = atof(temp_buf);
        } else if (param_num == 6) { // kmph
          reading.kmph = atof(temp_buf);
        } else if (param_num == 7) { // deg
          reading.deg = atof(temp_buf);
        } else if (param_num == 10) { // HDOP
          reading.hdop = atof(temp_buf);
        }
      }
      param_start = idx + 1;
      param_num++;
      memset(temp_buf, 0, 20);
    } else {
      temp_buf[idx - param_start] = inf_buffer[idx];
    }
  }
  return reading;
}

bool Location::set_gps_power(bool turn_on) {
  is_powered = turn_on;
  if (turn_on) printk("\tGPS check scheduled, warming up GPS module\n");
  else printk("\tGPS module powering off\n");
  char command[15];
  snprintk(command, 15, "AT+CGNSPWR=%d\r", turn_on ? 1 : 0);
  serial_print_uart(command);
  return serial_did_return_ok(1000LL);
}
