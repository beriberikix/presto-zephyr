/*
 * SPDX-License-Identifier: MIT
 *
 * Brings up the default network interface (CYW43439 on RM2) and logs a
 * heartbeat. Apply the Wi-Fi overlay + prj_wifi.conf to actually enable the
 * radio — see this app's README in the repo top-level for the command.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_wifi, LOG_LEVEL_INF);

#if !IS_ENABLED(CONFIG_NETWORKING)

int main(void)
{
	LOG_INF("Networking disabled in this build (CONFIG_NETWORKING=n)");

	while (1) {
		k_msleep(1000);
	}

	return 0;
}

#else

#include <zephyr/net/net_if.h>

int main(void)
{
	struct net_if *iface = net_if_get_default();

	if (iface == NULL) {
		LOG_ERR("No default network interface — is the Wi-Fi overlay applied?");
		return -ENODEV;
	}

	LOG_INF("Default network interface acquired (idx=%d)", net_if_get_by_iface(iface));

	while (1) {
		k_msleep(1000);
	}

	return 0;
}

#endif /* CONFIG_NETWORKING */
