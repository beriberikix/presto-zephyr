/*
 * Wi-Fi + display coexistence demo for the Pimoroni Presto.
 *
 * Proves the freed-SRAM coexistence end to end: the ST7701 panel scans out a
 * framebuffer in PSRAM (boards/presto_rp2350b_m33.conf selects
 * CONFIG_ST7701_PRESTO_FB_PSRAM) while the CYW43439 stack associates to an AP,
 * takes a DHCP lease, resolves a name and does a plain-HTTP GET. The display
 * is filled with a status colour at each phase, so the panel visibly tracks
 * the network state while traffic runs:
 *
 *   amber  connecting        cyan   DHCP lease bound
 *   blue   Wi-Fi associated  green  HTTP GET succeeded
 *   red    error             grey   no Wi-Fi (native_sim)
 *
 * On native_sim there is no radio: networking is disabled and the app just
 * exercises the drawing layer on the SDL display.
 *
 * Copyright (c) 2026 Jonathan Beri
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(wifi_display, LOG_LEVEL_INF);

/* Status colours (standard little-endian RGB565; display_write byteswaps). */
#define COL_AMBER 0xFD20
#define COL_BLUE  0x001F
#define COL_CYAN  0x07FF
#define COL_GREEN 0x07E0
#define COL_RED   0xF800
#define COL_GREY  0x8410

#define MAX_WIDTH 480
#define CHUNK_H   16

static uint16_t chunk_buf[MAX_WIDTH * CHUNK_H];

static const struct device *disp;
static uint16_t disp_w, disp_h;

/* Fill the whole panel with one colour, CHUNK_H rows at a time. */
static void fill_screen(uint16_t color)
{
	struct display_buffer_descriptor desc = {
		.width = disp_w,
		.pitch = disp_w,
	};

	if (disp == NULL) {
		return;
	}

	for (uint16_t i = 0; i < disp_w * CHUNK_H; i++) {
		chunk_buf[i] = color;
	}

	for (uint16_t y = 0; y < disp_h; y += CHUNK_H) {
		uint16_t rows = MIN(CHUNK_H, disp_h - y);

		desc.height = rows;
		desc.buf_size = (size_t)disp_w * rows * sizeof(uint16_t);
		display_write(disp, 0, y, &desc, chunk_buf);
	}
}

static int display_init(void)
{
	struct display_capabilities caps;

	disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(disp)) {
		LOG_ERR("display device not ready");
		disp = NULL;
		return -ENODEV;
	}

	(void)display_set_pixel_format(disp, PIXEL_FORMAT_RGB_565);
	display_get_capabilities(disp, &caps);
	disp_w = MIN(caps.x_resolution, MAX_WIDTH);
	disp_h = caps.y_resolution;
	display_blanking_off(disp);
	LOG_INF("display %ux%u ready", disp_w, disp_h);
	return 0;
}

#if IS_ENABLED(CONFIG_WIFI)

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/socket.h>

#define WIFI_MGMT_EVENTS (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;

static K_SEM_DEFINE(wifi_connected, 0, 1);
static K_SEM_DEFINE(ipv4_bound, 0, 1);
static volatile int wifi_status;
static char ip_str[NET_IPV4_ADDR_LEN];

static void wifi_evt(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	ARG_UNUSED(iface);

	if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
		const struct wifi_status *st = (const struct wifi_status *)cb->info;

		wifi_status = st->status;
		k_sem_give(&wifi_connected);
	} else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		LOG_WRN("Wi-Fi disconnected");
	}
}

static void ipv4_evt(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	ARG_UNUSED(cb);

	if (event != NET_EVENT_IPV4_ADDR_ADD || iface->config.ip.ipv4 == NULL) {
		return;
	}

	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		if (iface->config.ip.ipv4->unicast[i].ipv4.addr_type != NET_ADDR_DHCP) {
			continue;
		}
		net_addr_ntop(NET_AF_INET,
			      &iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr,
			      ip_str, sizeof(ip_str));
		k_sem_give(&ipv4_bound);
		return;
	}
}

static int wifi_connect(struct net_if *iface)
{
	struct wifi_connect_req_params params = {
		.ssid = (const uint8_t *)CONFIG_WIFI_DISPLAY_SSID,
		.ssid_length = sizeof(CONFIG_WIFI_DISPLAY_SSID) - 1,
		.psk = (const uint8_t *)CONFIG_WIFI_DISPLAY_PSK,
		.psk_length = sizeof(CONFIG_WIFI_DISPLAY_PSK) - 1,
		.security = (sizeof(CONFIG_WIFI_DISPLAY_PSK) - 1) ? WIFI_SECURITY_TYPE_PSK
								  : WIFI_SECURITY_TYPE_NONE,
		.channel = WIFI_CHANNEL_ANY,
		.band = WIFI_FREQ_BAND_2_4_GHZ,
		.mfp = WIFI_MFP_OPTIONAL,
	};

	LOG_INF("connecting to SSID \"%s\"", CONFIG_WIFI_DISPLAY_SSID);
	return net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
}

/* Plain-HTTP GET as the connectivity proof. Returns 0 on a 2xx response. */
static int http_get(void)
{
	const char *host = CONFIG_WIFI_DISPLAY_HTTP_HOST;
	const char *path = CONFIG_WIFI_DISPLAY_HTTP_PATH;
	char port[8];
	struct zsock_addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	struct zsock_addrinfo *res = NULL;
	char req[256];
	char resp[256];
	int sock = -1;
	int ret;

	snprintf(port, sizeof(port), "%d", CONFIG_WIFI_DISPLAY_HTTP_PORT);

	ret = zsock_getaddrinfo(host, port, &hints, &res);
	if (ret != 0 || res == NULL) {
		LOG_ERR("DNS resolve of %s failed (%d)", host, ret);
		return -EIO;
	}
	LOG_INF("resolved %s", host);

	sock = zsock_socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sock < 0) {
		LOG_ERR("socket() failed (%d)", errno);
		ret = -errno;
		goto out;
	}

	if (zsock_connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
		LOG_ERR("connect() to %s:%s failed (%d)", host, port, errno);
		ret = -errno;
		goto out;
	}

	ret = snprintf(req, sizeof(req),
		       "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n"
		       "User-Agent: presto-zephyr\r\n\r\n",
		       path, host);
	if (zsock_send(sock, req, ret, 0) < 0) {
		LOG_ERR("send() failed (%d)", errno);
		ret = -errno;
		goto out;
	}

	ret = zsock_recv(sock, resp, sizeof(resp) - 1, 0);
	if (ret <= 0) {
		LOG_ERR("recv() failed (%d)", errno);
		ret = -EIO;
		goto out;
	}
	resp[ret] = '\0';

	/* First line is "HTTP/1.x NNN ..." */
	if (strncmp(resp, "HTTP/1.", 7) == 0) {
		int code = atoi(resp + 9);

		LOG_INF("HTTP response: %.*s", (int)(strchr(resp, '\r') - resp), resp);
		ret = (code >= 200 && code < 400) ? 0 : -EIO;
	} else {
		LOG_ERR("unexpected response");
		ret = -EIO;
	}

out:
	if (sock >= 0) {
		zsock_close(sock);
	}
	if (res != NULL) {
		zsock_freeaddrinfo(res);
	}
	return ret;
}

static int run_network(void)
{
	struct net_if *iface = net_if_get_default();

	if (iface == NULL) {
		LOG_ERR("no default network interface");
		return -ENODEV;
	}

	if (sizeof(CONFIG_WIFI_DISPLAY_SSID) - 1 == 0) {
		LOG_ERR("CONFIG_WIFI_DISPLAY_SSID is empty — set it to your AP's SSID");
		return -EINVAL;
	}

	net_mgmt_init_event_callback(&wifi_cb, wifi_evt, WIFI_MGMT_EVENTS);
	net_mgmt_add_event_callback(&wifi_cb);
	net_mgmt_init_event_callback(&ipv4_cb, ipv4_evt, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_cb);

	if (wifi_connect(iface) != 0) {
		LOG_ERR("connect request failed");
		return -EIO;
	}

	if (k_sem_take(&wifi_connected, K_SECONDS(30)) != 0) {
		LOG_ERR("timed out waiting for association");
		return -ETIMEDOUT;
	}
	if (wifi_status != 0) {
		LOG_ERR("association failed (status %d)", wifi_status);
		return -ECONNREFUSED;
	}
	LOG_INF("associated; requesting DHCP lease");
	fill_screen(COL_BLUE);

	net_dhcpv4_start(iface);
	if (k_sem_take(&ipv4_bound, K_SECONDS(30)) != 0) {
		LOG_ERR("timed out waiting for DHCP lease");
		return -ETIMEDOUT;
	}
	LOG_INF("DHCP lease: %s", ip_str);
	fill_screen(COL_CYAN);

	return http_get();
}

#endif /* CONFIG_WIFI */

int main(void)
{
	display_init();
	fill_screen(COL_AMBER);

#if IS_ENABLED(CONFIG_WIFI)
	int ret = run_network();

	if (ret == 0) {
		LOG_INF("connectivity proof OK — Wi-Fi and display coexisting");
		fill_screen(COL_GREEN);
	} else {
		LOG_ERR("network bring-up failed (%d)", ret);
		fill_screen(COL_RED);
	}
#else
	LOG_INF("Wi-Fi disabled in this build (native_sim?) — display only");
	fill_screen(COL_GREY);
#endif

	/* Keep the panel scanning out (it runs off the driver's DMA/ISRs). */
	uint32_t beat = 0;

	while (1) {
		k_msleep(1000);
		LOG_DBG("alive %u", beat++);
	}

	return 0;
}
