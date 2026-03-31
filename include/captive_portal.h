/**
 * @file captive_portal.h
 * @brief Standalone captive portal component for ESP-IDF.
 * @author Ahmed Al-Tameemi, Nordes Sp. z o. o.
 *
 * Registers the standard OS captive-portal detection endpoints on a running
 * ESP-IDF HTTP server (esp_http_server). A 302 redirect to the device's local
 * web interface is returned for every probe request, causing iOS, macOS,
 * Android, Windows, and Firefox to automatically display the built-in
 * configuration page when a user connects to the device's Wi-Fi AP.
 *
 * Detected OS probe endpoints:
 *   - iOS / macOS : /hotspot-detect.html, /library/test/success.html
 *   - Android     : /generate_204, /gen_204
 *   - Windows     : /connecttest.txt, /ncsi.txt, /wpad.dat
 *   - Firefox     : /success.txt
 *   - Generic     : /redirect, /browsernetworktime/[wildcard]
 *
 * Minimal usage (Kconfig defaults):
 * @code
 *   httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
 *   cfg.uri_match_fn = httpd_uri_match_wildcard;  // required for wildcard URI /browsernetworktime/
 *   httpd_handle_t server = NULL;
 *   httpd_start(&server, &cfg);
 *
 *   captive_portal_register(server, NULL);   // NULL = use Kconfig defaults
 * @endcode
 *
 * Custom redirect URL:
 * @code
 *   captive_portal_config_t portal_cfg = CAPTIVE_PORTAL_CONFIG_DEFAULT();
 *   portal_cfg.redirect_url = "http://192.168.1.1/";
 *   captive_portal_register(server, &portal_cfg);
 * @endcode
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Captive portal runtime configuration.
 *
 * All fields are optional. NULL / zero values fall back to the corresponding
 * Kconfig compile-time defaults (see sdkconfig).
 */
typedef struct {
    /**
     * @brief Fixed redirect target URL, e.g. @c "http://192.168.4.1/".
     *
     * If non-NULL this URL is used verbatim for every redirect response.
     * If NULL the component resolves the IP at request time by querying
     * the network interface identified by @p netif_key, falling back to
     * CONFIG_CAPTIVE_PORTAL_FALLBACK_IP on failure.
     */
    const char *redirect_url;

    /**
     * @brief esp_netif interface key used for dynamic IP detection.
     *
     * Ignored when @p redirect_url is set.
     * NULL defaults to @c CONFIG_CAPTIVE_PORTAL_NETIF_KEY.
     *
     * Common values:
     *   - @c "WIFI_AP_DEF"  – Wi-Fi soft-AP   (default)
     *   - @c "WIFI_STA_DEF" – Wi-Fi station
     *   - @c "ETH_DEF"      – Ethernet
     */
    const char *netif_key;

    /**
     * @brief TCP port of the target web server.
     *
     * 0 defaults to @c CONFIG_CAPTIVE_PORTAL_REDIRECT_PORT (normally 80).
     * Port 80 is omitted from the generated redirect URL (standard HTTP).
     */
    uint16_t redirect_port;
} captive_portal_config_t;

/**
 * @brief Zero-initializer that applies the Kconfig defaults for all fields.
 *
 * @code
 *   captive_portal_config_t cfg = CAPTIVE_PORTAL_CONFIG_DEFAULT();
 *   cfg.redirect_url = "http://10.0.0.1/";   // override one field, keep rest
 *   captive_portal_register(server, &cfg);
 * @endcode
 */
#define CAPTIVE_PORTAL_CONFIG_DEFAULT() {                               \
    .redirect_url  = NULL,                                              \
    .netif_key     = CONFIG_CAPTIVE_PORTAL_NETIF_KEY,                  \
    .redirect_port = (uint16_t)(CONFIG_CAPTIVE_PORTAL_REDIRECT_PORT),  \
}

/**
 * @brief Register all standard captive-portal detection URI handlers on a
 *        running ESP-IDF HTTP server.
 *
 * Each registered handler issues an HTTP 302 redirect to the device's local
 * web interface. The redirect target is resolved in order:
 *   1. @c config->redirect_url  (if non-NULL, used verbatim)
 *   2. IP auto-detected at request time from the @c config->netif_key netif
 *   3. @c CONFIG_CAPTIVE_PORTAL_FALLBACK_IP  (compile-time Kconfig fallback)
 *
 * @note  The @c /browsernetworktime/ URI (registered with a trailing wildcard) requires wildcard URI matching.
 *        Set @c httpd_config_t.uri_match_fn = httpd_uri_match_wildcard
 *        before calling @c httpd_start(). Registration of this URI will
 *        still succeed without wildcard matching but requests will not match.
 *
 * @note  Call this function once, immediately after @c httpd_start() and
 *        before registering your application-specific URI handlers, to
 *        ensure portal probes are intercepted reliably.
 *
 * @param server  A valid @c httpd_handle_t returned by @c httpd_start().
 *                Must not be NULL.
 * @param config  Pointer to a configuration struct, or NULL to use the
 *                Kconfig compile-time defaults for all fields.
 *
 * @return
 *   - @c ESP_OK              – All handlers registered successfully.
 *   - @c ESP_ERR_INVALID_ARG – @p server is NULL.
 *   - @c ESP_FAIL            – One or more handler registrations failed
 *                              (details logged at ERROR level).
 */
esp_err_t captive_portal_register(httpd_handle_t server,
                                  const captive_portal_config_t *config);

#ifdef __cplusplus
}
#endif
