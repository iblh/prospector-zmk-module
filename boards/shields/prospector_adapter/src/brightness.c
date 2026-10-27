#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <errno.h>
#include <string.h>

#if defined(CONFIG_PROSPECTOR_TOUCH_UI)
#include <zephyr/settings/settings.h>
#endif

#include <prospector/brightness.h>
#include <zephyr/logging/log.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
LOG_MODULE_REGISTER(prospector_brightness, LOG_LEVEL_INF);

static const struct device *pwm_leds_dev = DEVICE_DT_GET_ONE(pwm_leds);
#define DISP_BL DT_NODE_CHILD_IDX(DT_NODELABEL(disp_bl))

static int set_backlight_brightness(uint8_t brightness) {
    int rc = led_set_brightness(pwm_leds_dev, DISP_BL, brightness);
    if (rc != 0) {
        LOG_ERR("Failed to set brightness: %d", rc);
    }

    return rc;
}

#ifdef CONFIG_PROSPECTOR_USE_AMBIENT_LIGHT_SENSOR
#define PROSPECTOR_DEFAULT_BRIGHTNESS 100
#define PROSPECTOR_DEFAULT_AUTO_MODE true
#else
#define PROSPECTOR_DEFAULT_BRIGHTNESS CONFIG_PROSPECTOR_FIXED_BRIGHTNESS
#define PROSPECTOR_DEFAULT_AUTO_MODE false
#endif

static uint8_t target_brightness = PROSPECTOR_DEFAULT_BRIGHTNESS;
static uint8_t displayed_brightness = PROSPECTOR_DEFAULT_BRIGHTNESS;
static bool backlight_active = true;
static bool auto_brightness_enabled = PROSPECTOR_DEFAULT_AUTO_MODE;
K_MUTEX_DEFINE(brightness_lock);

#define PWM_MIN 10  // Keep manual brightness usable and avoid an overly dim panel
#define PWM_MAX 100 // Maximum PWM duty cycle (%)

#define FADE_SLEEP_BRIGHTEN_MS 3
#define FADE_SLEEP_DARKEN_MS 10

static void brightness_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(brightness_work, brightness_work_handler);

#if defined(CONFIG_PROSPECTOR_TOUCH_UI)
#define BRIGHTNESS_SETTING "prospector_touch/brightness"
#define BRIGHTNESS_SAVE_DELAY K_MSEC(1200)

static uint8_t saved_brightness = PROSPECTOR_DEFAULT_BRIGHTNESS;
static void brightness_save_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(brightness_save_work, brightness_save_work_handler);
#endif

static uint8_t clamp_brightness(uint8_t brightness) { return CLAMP(brightness, PWM_MIN, PWM_MAX); }

/* One PWM update per invocation keeps LVGL and ZMK event handlers responsive. */
static void brightness_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    uint8_t current;
    uint8_t target;

    k_mutex_lock(&brightness_lock, K_FOREVER);
    current = displayed_brightness;
    target = backlight_active ? target_brightness : 0;
    k_mutex_unlock(&brightness_lock);

    if (current == target) {
        return;
    }

    uint8_t next = current < target ? current + 1 : current - 1;
    if (set_backlight_brightness(next) != 0) {
        return;
    }

    k_mutex_lock(&brightness_lock, K_FOREVER);
    displayed_brightness = next;
    target = backlight_active ? target_brightness : 0;
    k_mutex_unlock(&brightness_lock);

    if (next != target) {
        (void)k_work_reschedule(&brightness_work,
                                K_MSEC(next < target ? FADE_SLEEP_BRIGHTEN_MS
                                                     : FADE_SLEEP_DARKEN_MS));
    }
}

static int set_target_brightness(uint8_t brightness, bool manual_mode) {
    uint8_t target = clamp_brightness(brightness);

    k_mutex_lock(&brightness_lock, K_FOREVER);
    if (manual_mode) {
        auto_brightness_enabled = false;
    }
    target_brightness = target;
    k_mutex_unlock(&brightness_lock);

    (void)k_work_reschedule(&brightness_work, K_NO_WAIT);
#if defined(CONFIG_PROSPECTOR_TOUCH_UI)
    if (manual_mode) {
        (void)k_work_reschedule(&brightness_save_work, BRIGHTNESS_SAVE_DELAY);
    }
#endif
    return 0;
}

static int set_backlight_active(bool active) {
    k_mutex_lock(&brightness_lock, K_FOREVER);
    backlight_active = active;
    k_mutex_unlock(&brightness_lock);
    (void)k_work_reschedule(&brightness_work, K_NO_WAIT);
    return 0;
}

int prospector_brightness_auto_on(void) {
#ifdef CONFIG_PROSPECTOR_USE_AMBIENT_LIGHT_SENSOR
    k_mutex_lock(&brightness_lock, K_FOREVER);
    auto_brightness_enabled = true;
    k_mutex_unlock(&brightness_lock);
    return 0;
#else
    return -ENOTSUP;
#endif
}

int prospector_brightness_manual_on(void) {
    k_mutex_lock(&brightness_lock, K_FOREVER);
    auto_brightness_enabled = false;
    k_mutex_unlock(&brightness_lock);
    return 0;
}

int prospector_brightness_toggle_auto(void) {
#ifdef CONFIG_PROSPECTOR_USE_AMBIENT_LIGHT_SENSOR
    k_mutex_lock(&brightness_lock, K_FOREVER);
    auto_brightness_enabled = !auto_brightness_enabled;
    k_mutex_unlock(&brightness_lock);
    return 0;
#else
    return -ENOTSUP;
#endif
}

int prospector_brightness_set_manual(uint8_t brightness) {
    return set_target_brightness(brightness, true);
}

uint8_t prospector_brightness_get_manual(void) {
    uint8_t value;

    k_mutex_lock(&brightness_lock, K_FOREVER);
    value = target_brightness;
    k_mutex_unlock(&brightness_lock);
    return value;
}

int prospector_brightness_manual_step(int direction) {
    uint8_t next;

    k_mutex_lock(&brightness_lock, K_FOREVER);
    if (direction > 0) {
        next = MIN(target_brightness + CONFIG_PROSPECTOR_BRIGHTNESS_STEP, PWM_MAX);
    } else {
        next = MAX(target_brightness - CONFIG_PROSPECTOR_BRIGHTNESS_STEP, PWM_MIN);
    }
    k_mutex_unlock(&brightness_lock);

    return set_target_brightness(next, true);
}

bool prospector_brightness_is_auto(void) {
    bool enabled;

    k_mutex_lock(&brightness_lock, K_FOREVER);
    enabled = auto_brightness_enabled;
    k_mutex_unlock(&brightness_lock);

    return enabled;
}

#if defined(CONFIG_PROSPECTOR_TOUCH_UI)
static int brightness_settings_set(const char *name, size_t len,
                                   settings_read_cb read_cb, void *cb_arg) {
    uint8_t value;
    int rc;

    if (strcmp(name, "brightness") != 0) {
        return -ENOENT;
    }
    if (len != sizeof(value)) {
        return -EINVAL;
    }

    rc = read_cb(cb_arg, &value, sizeof(value));
    if (rc < 0) {
        return rc;
    }
    if (rc != sizeof(value) || value < PWM_MIN || value > PWM_MAX) {
        return -EINVAL;
    }

    k_mutex_lock(&brightness_lock, K_FOREVER);
    target_brightness = value;
    saved_brightness = value;
    auto_brightness_enabled = false;
    k_mutex_unlock(&brightness_lock);
    return 0;
}

static int brightness_settings_commit(void) {
    (void)k_work_reschedule(&brightness_work, K_NO_WAIT);
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(prospector_touch, "prospector_touch", NULL,
                               brightness_settings_set, brightness_settings_commit, NULL);

static void brightness_save_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    uint8_t value;

    k_mutex_lock(&brightness_lock, K_FOREVER);
    value = target_brightness;
    bool unchanged = value == saved_brightness;
    k_mutex_unlock(&brightness_lock);

    if (unchanged) {
        return;
    }

    int rc = settings_save_one(BRIGHTNESS_SETTING, &value, sizeof(value));
    if (rc != 0) {
        LOG_ERR("Failed to save brightness: %d", rc);
        return;
    }

    k_mutex_lock(&brightness_lock, K_FOREVER);
    saved_brightness = value;
    bool changed_again = target_brightness != value;
    k_mutex_unlock(&brightness_lock);
    if (changed_again) {
        (void)k_work_reschedule(&brightness_save_work, BRIGHTNESS_SAVE_DELAY);
    }
}
#endif

#ifdef CONFIG_PROSPECTOR_USE_AMBIENT_LIGHT_SENSOR

#define SENSOR_MIN 0   // Minimum sensor reading
#define SENSOR_MAX 100 // Maximum sensor reading

#define FADE_THRESHOLD 10

#define NORMAL_SAMPLE_SLEEP_MS 100

#define BURST_SAMPLE_SLEEP_MS 30
#define BURST_SAMPLE_TIMEOUT 10
#define BURST_SAMPLE_CONSECUTIVE 3

uint8_t map_light_to_pwm(int32_t sensor_reading) {
    // Handle invalid/error readings
    if (sensor_reading < SENSOR_MIN) {
        return PWM_MIN; // Default to minimum brightness on error
    }

    // Clamp to maximum
    if (sensor_reading > SENSOR_MAX) {
        sensor_reading = SENSOR_MAX;
    }

    // Linear mapping
    uint8_t pwm_value = (uint8_t)(PWM_MIN + ((PWM_MAX - PWM_MIN) * (sensor_reading - SENSOR_MIN)) /
                                                (SENSOR_MAX - SENSOR_MIN));

    return pwm_value;
}

extern void als_thread(void *d0, void *d1, void *d2) {
    ARG_UNUSED(d0);
    ARG_UNUSED(d1);
    ARG_UNUSED(d2);

    const struct device *dev;
    struct sensor_value intensity;
    uint8_t mapped_brightness;

    dev = DEVICE_DT_GET_ONE(avago_apds9960);
    if (!device_is_ready(dev)) {
        printk("sensor: device not ready.\n");
    }

    // led_set_brightness(pwm_leds_dev, DISP_BL, 100);

    while (1) {

        k_msleep(NORMAL_SAMPLE_SLEEP_MS);

        uint8_t compare_brightness;
        bool active;
        bool auto_enabled;

        k_mutex_lock(&brightness_lock, K_FOREVER);
        active = backlight_active;
        auto_enabled = auto_brightness_enabled;
        compare_brightness = target_brightness;
        k_mutex_unlock(&brightness_lock);

        if (!active || !auto_enabled) {
            continue;
        }

        int rc = sensor_sample_fetch(dev);
        if (rc != 0) {
            LOG_ERR("sensor_sample_fetch failed: %d", rc);
            continue;
        }

        rc = sensor_channel_get(dev, SENSOR_CHAN_LIGHT, &intensity);
        if (rc != 0) {
            LOG_ERR("Cannot read ALS data: %d", rc);
            continue;
        }

        // LOG_INF("ambient light intensity %d", intensity.val1);

        mapped_brightness = map_light_to_pwm(intensity.val1);
        // LOG_INF("NORMAL: mapped PWM duty cycle %d\n", mapped_brightness);

        if (abs(mapped_brightness - compare_brightness) > FADE_THRESHOLD) {
            uint8_t integrator = 0;

            for (int i = 0; i < BURST_SAMPLE_TIMEOUT; i++) {
                uint8_t burst_compare_brightness;

                k_msleep(BURST_SAMPLE_SLEEP_MS);

                rc = sensor_sample_fetch(dev);
                if (rc != 0) {
                    LOG_ERR("sensor_sample_fetch failed: %d", rc);
                    continue;
                }
                rc = sensor_channel_get(dev, SENSOR_CHAN_LIGHT, &intensity);
                if (rc != 0) {
                    LOG_ERR("Cannot read ALS data: %d", rc);
                    continue;
                }

                mapped_brightness = map_light_to_pwm(intensity.val1);
                // LOG_INF("BURST: mapped PWM duty cycle %d\n", mapped_brightness);

                k_mutex_lock(&brightness_lock, K_FOREVER);
                active = backlight_active;
                auto_enabled = auto_brightness_enabled;
                burst_compare_brightness = target_brightness;
                k_mutex_unlock(&brightness_lock);

                if (!active || !auto_enabled) {
                    break;
                }

                if (abs(mapped_brightness - burst_compare_brightness) > FADE_THRESHOLD) {
                    integrator++;
                    // printk("integrator at: %d", integrator);
                    if (integrator >= BURST_SAMPLE_CONSECUTIVE) {
                        rc = set_target_brightness(mapped_brightness, false);
                        if (rc != 0) {
                            LOG_ERR("Failed to fade brightness: %d", rc);
                        }
                        // LOG_INF("SETTING NEW BRIGHTNESS: %d", mapped_brightness);
                        break;
                    }
                }
            }
        }
        // led_set_brightness(pwm_leds_dev, DISP_BL,
        // map_light_to_pwm(intensity.val1));
    }
}

static int als_brightness_activity_listener(const zmk_event_t *eh) {
    struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);

    if (ev == NULL) {
        return -ENOTSUP;
    }

    switch (ev->state) {
    case ZMK_ACTIVITY_ACTIVE:
        return set_backlight_active(true);
    case ZMK_ACTIVITY_IDLE:
    case ZMK_ACTIVITY_SLEEP:
        return set_backlight_active(false);
    default:
        return -EINVAL;
    }
}

ZMK_LISTENER(prospector_als_brightness, als_brightness_activity_listener);
ZMK_SUBSCRIPTION(prospector_als_brightness, zmk_activity_state_changed);

K_THREAD_DEFINE(als_tid, 1024, als_thread, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0,
                0);

#else

static int fixed_brightness_activity_listener(const zmk_event_t *eh) {
    struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);

    if (ev == NULL) {
        return -ENOTSUP;
    }

    switch (ev->state) {
    case ZMK_ACTIVITY_ACTIVE:
        return set_backlight_active(true);
    case ZMK_ACTIVITY_IDLE:
    case ZMK_ACTIVITY_SLEEP:
        return set_backlight_active(false);
    default:
        return -EINVAL;
    }
}

ZMK_LISTENER(prospector_fixed_brightness, fixed_brightness_activity_listener);
ZMK_SUBSCRIPTION(prospector_fixed_brightness, zmk_activity_state_changed);

static int init_fixed_brightness(void) {
    return set_backlight_brightness(CONFIG_PROSPECTOR_FIXED_BRIGHTNESS);
}

SYS_INIT(init_fixed_brightness, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif
