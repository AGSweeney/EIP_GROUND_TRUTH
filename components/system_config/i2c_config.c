#include "i2c_config.h"
#include "system_config.h"
#include "sdkconfig.h"

bool i2c_config_get_internal_pullup_enabled(void)
{
    // Load from NVS, fallback to compile-time config
    return system_i2c_internal_pullup_load();
}

