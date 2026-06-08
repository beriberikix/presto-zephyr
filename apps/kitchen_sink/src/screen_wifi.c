/*
 * SPDX-License-Identifier: MIT
 *
 * Wi-Fi screen: associates to the configured AP, takes a DHCP lease and shows
 * the SSID / state / IP on the panel. The connect is non-blocking; the net
 * management callbacks drive a small state machine. Mirrors apps/wifi_display.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

#include "screens.h"
#include "ui.h"

LOG_MODULE_REGISTER(screen_wifi, LOG_LEVEL_INF);

#if IS_ENABLED(CONFIG_WIFI)

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/dhcpv4.h>

#define WIFI_MGMT_EVENTS (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)
#define SSID CONFIG_KITCHEN_SINK_WIFI_SSID
#define PSK  CONFIG_KITCHEN_SINK_WIFI_PSK

enum wifi_ui_state { WS_IDLE, WS_CONNECTING, WS_ASSOC, WS_BOUND, WS_ERR };

/* iface is set once (to a stable pointer) before any connect, so the callbacks
 * can read it without synchronisation. The mutable state below is shared
 * between the net-management callback thread and the UI thread, so it uses
 * atomics rather than volatile.
 */
static struct net_if *iface;
static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;
static bool cbs_registered;
static bool dhcp_kicked;
static atomic_t ws_state = ATOMIC_INIT(WS_IDLE);
static atomic_t dirty;
static atomic_t last_status; /* status from the last connect result; <0 = no iface */
static char ip_str[NET_IPV4_ADDR_LEN];

/* The AIROC connect blocks (whd_wifi_join), so run it off the UI thread. */
static K_THREAD_STACK_DEFINE(conn_stack, 4096);
static struct k_thread conn_thread;
static K_SEM_DEFINE(connect_req, 0, 1);
static bool thread_started;
static int retries;
static int64_t err_ms; /* when the current error started; 0 = not erroring */

#define WIFI_MAX_RETRIES 3
#define WIFI_RETRY_MS    2000

static void set_state(enum wifi_ui_state s)
{
	atomic_set(&ws_state, s);
	atomic_set(&dirty, 1);
}

static void wifi_evt(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *fc)
{
	if (fc != iface) {
		return; /* not the interface this screen manages */
	}

	if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
		const struct wifi_status *st = (const struct wifi_status *)cb->info;

		atomic_set(&last_status, st->status);
		set_state(st->status ? WS_ERR : WS_ASSOC); /* main loop kicks DHCP */
	} else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		dhcp_kicked = false;
		set_state(WS_IDLE);
	}
}

static void ipv4_evt(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *fc)
{
	ARG_UNUSED(cb);

	if (event != NET_EVENT_IPV4_ADDR_ADD || fc != iface || fc->config.ip.ipv4 == NULL) {
		return;
	}
	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		if (fc->config.ip.ipv4->unicast[i].ipv4.addr_type != NET_ADDR_DHCP) {
			continue;
		}
		net_addr_ntop(NET_AF_INET, &fc->config.ip.ipv4->unicast[i].ipv4.address.in_addr,
			      ip_str, sizeof(ip_str));
		set_state(WS_BOUND);
		return;
	}
}

static void wifi_conn_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (;;) {
		k_sem_take(&connect_req, K_FOREVER);
		if (iface == NULL) {
			continue;
		}

		struct wifi_connect_req_params p = {
			.ssid = (const uint8_t *)SSID,
			.ssid_length = sizeof(SSID) - 1,
			.psk = (const uint8_t *)PSK,
			.psk_length = sizeof(PSK) - 1,
			.security = (sizeof(PSK) - 1) ? WIFI_SECURITY_TYPE_PSK
						      : WIFI_SECURITY_TYPE_NONE,
			.channel = WIFI_CHANNEL_ANY,
			.band = WIFI_FREQ_BAND_2_4_GHZ,
			.mfp = WIFI_MFP_OPTIONAL,
		};

		LOG_INF("connecting to SSID \"%s\"", SSID);
		(void)net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &p, sizeof(p));
	}
}

static int wifi_enter(void)
{
	iface = net_if_get_default();
	atomic_set(&dirty, 1);

	if (!cbs_registered) {
		net_mgmt_init_event_callback(&wifi_cb, wifi_evt, WIFI_MGMT_EVENTS);
		net_mgmt_add_event_callback(&wifi_cb);
		net_mgmt_init_event_callback(&ipv4_cb, ipv4_evt, NET_EVENT_IPV4_ADDR_ADD);
		net_mgmt_add_event_callback(&ipv4_cb);
		cbs_registered = true;
	}

	if (!thread_started) {
		k_thread_create(&conn_thread, conn_stack, K_THREAD_STACK_SIZEOF(conn_stack),
				wifi_conn_fn, NULL, NULL, NULL, K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
		k_thread_name_set(&conn_thread, "wifi_connect");
		thread_started = true;
	}

	if (iface == NULL) {
		LOG_WRN("no default network interface");
		atomic_set(&last_status, -1);
		atomic_set(&ws_state, WS_ERR);
		return -ENODEV;
	}

	/* Kick a connect off the UI thread (the AIROC join blocks). Re-entering
	 * after an error restarts the attempt.
	 */
	enum wifi_ui_state s = atomic_get(&ws_state);

	if ((s == WS_IDLE || s == WS_ERR) && sizeof(SSID) - 1 > 0) {
		retries = 0;
		err_ms = 0;
		dhcp_kicked = false;
		set_state(WS_CONNECTING);
		k_sem_give(&connect_req);
	}
	return 0;
}

static bool wifi_update(void)
{
	bool d = atomic_set(&dirty, 0) != 0;
	enum wifi_ui_state s = atomic_get(&ws_state);

	/* Start DHCP from the main thread once associated. */
	if (s == WS_ASSOC && !dhcp_kicked && iface != NULL) {
		net_dhcpv4_start(iface);
		dhcp_kicked = true;
	}

	/* The AIROC join can fail transiently under load — retry a few times. */
	if (s == WS_ERR && atomic_get(&last_status) >= 0 && retries < WIFI_MAX_RETRIES &&
	    iface != NULL) {
		if (err_ms == 0) {
			err_ms = k_uptime_get();
		} else if (k_uptime_get() - err_ms > WIFI_RETRY_MS) {
			retries++;
			err_ms = 0;
			dhcp_kicked = false;
			set_state(WS_CONNECTING);
			k_sem_give(&connect_req);
			d = true;
		}
	}
	return d;
}

static void wifi_render(void)
{
	char buf[20];
	const char *st;
	uint16_t c;
	int ls = atomic_get(&last_status);

	ui_begin("Wi-Fi", COL_BLUE);
	gfx_text(8, 44, (sizeof(SSID) - 1) ? SSID : "(no SSID set)", COL_WHITE, UI_BG, 2);

	switch (atomic_get(&ws_state)) {
	case WS_CONNECTING: st = "connecting"; c = COL_AMBER; break;
	case WS_ASSOC:      st = "getting IP"; c = COL_CYAN;  break;
	case WS_BOUND:      st = ip_str;       c = COL_GREEN; break;
	case WS_ERR:
		c = COL_RED;
		if (ls < 0) {
			st = "no iface";
		} else {
			snprintf(buf, sizeof(buf), "err %d", ls);
			st = buf;
		}
		break;
	default:            st = "idle";       c = COL_GREY;  break;
	}

	gfx_fill_rect(8, 92, gfx_width() - 16, 40, COL_DKGREY);
	gfx_text(16, 100, st, c, COL_DKGREY, 2);
	ui_footer();
}

static void wifi_leave(void)
{
}

#else /* no Wi-Fi */

static int wifi_enter(void)
{
	return 0;
}

static bool wifi_update(void)
{
	return false;
}

static void wifi_render(void)
{
	ui_begin("Wi-Fi", COL_BLUE);
	gfx_text(8, 96, "Wi-Fi n/a", COL_GREY, UI_BG, 2);
	ui_footer();
}

static void wifi_leave(void)
{
}

#endif /* CONFIG_WIFI */

static const struct screen instance = {
	.name = "wifi",
	.enter = wifi_enter,
	.update = wifi_update,
	.render = wifi_render,
	.leave = wifi_leave,
};

const struct screen *screen_wifi(void)
{
	return &instance;
}
