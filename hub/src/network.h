#ifndef HUB_NETWORK_H
#define HUB_NETWORK_H

#include <zephyr/settings/settings.h>
#include <stdint.h>

// Needs to be large enough for error messages
const uint16_t RESPONSE_SIZE = 2000;

class Network {
private:
    /**
     * Static memory used for reading network response, might need to increase size if expecting larger responses
    **/
    char buffer[RESPONSE_SIZE]{};

    /**
     * Network registration status
     */
    int8_t last_status = -1;

public:

    // IMEI number, should be set in main during network setup by calling get_imei()
    char device_imei[20]{};

    // Sets up UART and MOSFET_SIM devices
    int init(void);

    // Returns the token's length or 0 if not found
    int initialize_access_token(void);

    // Set the access token, can also be an empty array to clear the token
    void set_access_token(const char new_access_token[100]);

    /**
     * Sends a request containing query to API_URL, returns a json document with
     * response in the "data" field if no errors, otherwise errors will be in "errors"
    **/
    // DynamicJsonDocument SendRequest(char* query, BLELocalDevice* BLE);

    /**
     * Utility function to set AT+CFUN=1 or 4 (1 = full, 4 = airplane mode)
     */
    void set_fun_mode(bool full_functionality);

    /**
     * @brief IMEI string from the SIM module and stores it into class's device_imei buffer
     * returns false if timed out
    **/
    bool get_imei();

    /**
     * Get the AT+CREG network status
     * 0 - Not registered, the device is currently not searching for new operator.
     * 1 - Registered to home network.
     * 2 - Not registered, but the device is currently searching for a new operator.
     * 3 - Registration denied.
     * 4 - Unknown. For example, out of range.
     * 5 - Registered, roaming. The device is registered on a foreign (national or international) network.
     *
     * -1 - Failed to retrieve status
     *
     * https://docs.eseye.com/Content/ELS61/ATCommands/ELS61CREG.htm
     */
    int8_t get_reg_status(void);

    /**
     * Can be called after get_reg_status returns 1 or 5 to read the access tech
     * 0 - GSM
     * 2 - UTRAN
     * 3 - GSM w/EGPRS
     * 4 - UTRAN w/HSDPA
     * 5 - UTRAN w/HSUPA
     * 6 - UTRAN w/HSDPA and w/HSUPA
     * 7 - E-UTRAN
     *
     * -1 - Failed to retrieve status or not registered
     *
     */
    int8_t get_acc_tech(void);

    /**
     * @brief Configure SIM7000 with any long-term settings like baud rate
     * @return True if configured without problems
     */
    bool configure_modem(void);

    /**
     * Wait until receiving the power on messages from the sim module
     * Returns true if powered on, false if timed out
     */
    bool wait_for_power_on(void);

    /**
     * Set the power on or off for the SIMCOM module
     */
    void set_power(bool on);

    /**
     * We can't cache the power state of the module since it can change state separately
     * So this does a quick <100ms query to see if the module is attached and powered
     */
    bool is_powered_on(void);

    /**
     * Shorthand for calling set_power(true), wait_for_power_on, and get_reg_status until registered
     */
    bool set_power_on_and_wait_for_reg(void);
};

#endif