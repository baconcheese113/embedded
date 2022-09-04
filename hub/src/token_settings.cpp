// Peristent storage
#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(token_settings);
#include <sys/errno.h>
#include <string.h>

#include "token_settings.h"

struct work_info {
    struct k_work work;
    char name[25];
} my_work;

token_data_t token_data;

static int token_settings_set(const char* name, size_t len, settings_read_cb read_cb, void* cb_arg)
{
    const char* next;
    int rc;
    if (settings_name_steq(name, "str", &next) && !next) {
        if (len != sizeof(token_data)) {
            LOG_ERR("token/str size %d is not compatible with the application len %d", sizeof(token_data), len);
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &token_data, sizeof(token_data));
        LOG_DBG("rc is %d", rc);
        if (rc >= 0) {
            return 0;
        }
        return rc;
    }
    return -ENOENT;
}

static struct settings_handler token_conf = {
    .name = "token",
    .h_set = token_settings_set,
};

static void save_token_work(struct k_work* work_item) {
    int ret = settings_save_one("token/str", &token_data, sizeof(token_data));
    LOG_INF("Saved token/str %s of size %d in NVS, status=%d", token_data.access_token, sizeof(token_data), ret);
}

void save_token(const char new_access_token[100]) {
    strcpy(token_data.access_token, new_access_token);
    token_data.is_valid = strlen(token_data.access_token) > 0;
    k_work_submit(&my_work.work);
    LOG_DBG("Save work_item submitted to work queue");
}

// Should be called in main to setup Settings and load token
uint8_t initialize_token() {
    int ret = 0;
    memset(token_data.access_token, 0, 100);
    token_data.is_valid = false;
    if (IS_ENABLED(CONFIG_SETTINGS)) {
        settings_subsys_init();
        settings_register(&token_conf);
        settings_load();
        ret = strlen(token_data.access_token);
        if (ret > 0) {
            LOG_INF("Token found: %s and is_valid: %u\r", token_data.access_token, token_data.is_valid);
        } else LOG_INF("No token_val found");
    } else {
        LOG_ERR("CONFIG_SETTINGS not enabled");
    }

    // init work queue for saving token
    k_work_init(&my_work.work, save_token_work);

    return ret;
}