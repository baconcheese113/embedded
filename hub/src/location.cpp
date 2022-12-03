#include <zephyr/sys/printk.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "serial.h"
#include "location.h"
#include "utilities.h"
#include "network.h"
#include "network_requests.h"

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

void Location::init(Network* net, NetworkRequests* network_requests) {
  network = net;
  network_reqs = network_requests;
}

bool Location::should_warm_up() {
  return !warm_up_start_time && (last_gps_time == 0 || k_uptime_get() > last_gps_time + GPS_UPDATE_INTERVAL);
}

bool Location::should_send_update() {
  return warm_up_start_time > 0;
}

void Location::turn_off(const char* msg) {
  if (strlen(msg)) printk("%s", msg);
  Utilities::write_rgb(0, 0, 0);
  set_gps_power(false);
  network->set_power(false);
  warm_up_start_time = 0;
}

int Location::start_warm_up() {
  // warm up the module
  Utilities::write_rgb(235, 30, 180);
  network->set_power(true);
  if (!network->wait_for_power_on()) {
    turn_off("Error: Network didn't start\n");
    return -1;
  } else if (!set_gps_power(true)) {
    turn_off("Error: Unable to turn on gps\n");
    return -1;
  }
  warm_up_start_time = k_uptime_get();
  return 0;
}

int Location::send_update() {
  Utilities::write_rgb(120, 10, 50);

  if (k_uptime_get() > warm_up_start_time + GPS_BUFFER_TIME) {
    turn_off("Location check timed out, aborting\n");
    return -1;
  }

  if (!network->is_powered_on()) {
    turn_off("Modem isn't powered, aborting\n");
    return -1;
  }
  // module is warmed up
  last_gps_time = k_uptime_get();
  serial_print_uart("AT+CGNSINF\r");
  serial_did_return_str("AT+CGNSINF", 5000LL);
  char inf_buf[200];
  serial_read_queue(inf_buf, 5000LL);
  if (strlen(inf_buf) < 10) {
    printk("Failed to read inf from SIM module, aborting\n");
    return -1;
  }

  // We have a reading
  printk("\tBuffer %s\n", inf_buf);
  LocReading reading = parse_inf(inf_buf);
  printk("\n*****Updating GPS location*****\n");
  if (!reading.hasFix) {
    printk("No GPS fix yet, aborting\n");
    return -1;
  }
  print_loc_reading(reading);

  double dist = distance_from_last_point(reading.lat, reading.lng);
  if (dist < 20) {
    turn_off("New location is less than 20m away from previously sent location, aborting\n");
    return -1;
  }

  int err = network_reqs->handle_update_gps_loc(reading.lat, reading.lng, reading.hdop, reading.kmph, reading.deg);
  last_sent_reading = reading;
  if (err) turn_off("Network request to update gps loc failed\n");
  else turn_off("Successfully updated gps location!\n");
  return err;
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
