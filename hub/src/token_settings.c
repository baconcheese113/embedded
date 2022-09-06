#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>
#include <zephyr/kernel.h>
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
            printk("token/str size %d is not compatible with the application len %d\n", sizeof(token_data), len);
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &token_data, sizeof(token_data));
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
    printk("Saved token/str %s of size %d in NVS, status=%d\n", token_data.access_token, sizeof(token_data), ret);
}

void save_token(const char new_access_token[100]) {
    strcpy(token_data.access_token, new_access_token);
    token_data.is_valid = strlen(token_data.access_token) > 0;
    k_work_submit(&my_work.work);
    printk("Save work_item submitted to work queue\n");
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
            printk("\tToken found: %s and is_valid: %u\n", token_data.access_token, token_data.is_valid);
        } else printk("\tNo token_val found\n");
    } else {
        printk("\tCONFIG_SETTINGS not enabled\n");
    }

    // init work queue for saving token
    k_work_init(&my_work.work, save_token_work);

    return ret;
}