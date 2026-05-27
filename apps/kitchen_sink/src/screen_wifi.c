/*
 * SPDX-License-Identifier: MIT
 *
 * Wi-Fi screen: reports default-iface status. Only meaningful when this app
 * is built with the Wi-Fi overlay + prj_wifi.conf applied.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "screens.h"

LOG_MODULE_REGISTER(screen_wifi, LOG_LEVEL_INF);

static int logged_once;

static int wifi_enter(void)
{
	logged_once = 0;
	return 0;
}

#if IS_ENABLED(CONFIG_NETWORKING)

#include <zephyr/net/net_if.h>

static void wifi_update(void)
{
	if (logged_once) {
		return;
	}

	struct net_if *iface = net_if_get_default();

	if (iface == NULL) {
		LOG_INF("Wi-Fi screen: no default iface (build with -DEXTRA_CONF_FILE=prj_wifi.conf)");
	} else {
		LOG_INF("Wi-Fi screen: iface idx=%d", net_if_get_by_iface(iface));
	}

	logged_once = 1;
}

#else

static void wifi_update(void)
{
	if (logged_once) {
		return;
	}

	LOG_INF("Wi-Fi screen: networking disabled in this build");
	logged_once = 1;
}

#endif /* CONFIG_NETWORKING */

static void wifi_leave(void)
{
}

static const struct screen instance = {
	.name = "wifi",
	.enter = wifi_enter,
	.update = wifi_update,
	.leave = wifi_leave,
};

const struct screen *screen_wifi(void)
{
	return &instance;
}
