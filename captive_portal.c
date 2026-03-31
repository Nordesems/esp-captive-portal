/**
 * @file captive_portal.c
 * @brief Standalone captive portal implementation for ESP-IDF.
 * @author Ahmed Al-Tameemi, Nordes Sp. z o. o.
 *
 * Registers the well-known OS captive-portal probe URIs on the application's
 * HTTP server and redirects every probe to the device's local web interface.
 * No external dependencies beyond ESP-IDF built-in components.
 *
 * Design notes:
 *   - Module-static storage holds the effective configuration so that the
 *     HTTP handler (which receives only httpd_req_t*) can access it without
 *     heap allocation.
 *   - The redirect URL is resolved at every request to always reflect the
 *     current AP IP address, which may change after runtime netif reconfiguration.
 *   - The static URI table is shared across all registrations; ESP-IDF stores
 *     only the pointer internally, so the table must have static storage lifetime.
 */

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_server.h"

#include "captive_portal.h"

static const char *TAG = "captive_portal";

/* --------------------------------------------------------------------------
 * Module-static configuration
 * Populated once by captive_portal_register(); read by the handler task.
 * -------------------------------------------------------------------------- */
static struct {
    char     redirect_url[128]; /*!< Non-empty: fixed URL. Empty: auto-detect. */
    char     netif_key[32];     /*!< esp_netif interface key for IP resolution. */
    uint16_t redirect_port;     /*!< Web server port; 80 is omitted from URLs.  */
} s_cfg = {
    .redirect_url = "",
    .netif_key    = CONFIG_CAPTIVE_PORTAL_NETIF_KEY,
    .redirect_port = (uint16_t)(CONFIG_CAPTIVE_PORTAL_REDIRECT_PORT),
};

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

/**
 * @brief Resolve the redirect target URL into @p buf.
 *
 * Resolution order:
 *   1. s_cfg.redirect_url if non-empty (fixed URL, used verbatim).
 *   2. IP obtained from the esp_netif identified by s_cfg.netif_key.
 *   3. CONFIG_CAPTIVE_PORTAL_FALLBACK_IP (compile-time fallback).
 */
static void build_redirect_url(char *buf, size_t buf_size)
{
    /* Fixed URL takes priority – copy and return immediately. */
    if (s_cfg.redirect_url[0] != '\0') {
        strlcpy(buf, s_cfg.redirect_url, buf_size);
        return;
    }

    /* Attempt to read the AP interface IP address. */
    const char *ip_str = CONFIG_CAPTIVE_PORTAL_FALLBACK_IP;
    char        ip_buf[16] = {0};

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey(s_cfg.netif_key);
    if (netif != NULL) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            snprintf(ip_buf, sizeof(ip_buf), IPSTR, IP2STR(&ip_info.ip));
            ip_str = ip_buf;
        }
    }

    /* Build URL; omit port number for the standard HTTP port 80. */
    if (s_cfg.redirect_port == 80U) {
        snprintf(buf, buf_size, "http://%s/", ip_str);
    } else {
        snprintf(buf, buf_size, "http://%s:%u/", ip_str, (unsigned)s_cfg.redirect_port);
    }
}

/* --------------------------------------------------------------------------
 * HTTP request handler
 * -------------------------------------------------------------------------- */

/**
 * @brief HTTP GET handler for all registered captive-portal probe endpoints.
 *
 * Issues an HTTP 302 redirect with:
 *   - Location header pointing to the device's web interface.
 *   - A minimal HTML body for clients that do not follow redirects automatically.
 *   - Cache-Control: no-store to prevent caching of the redirect response.
 */
static esp_err_t captive_portal_http_handler(httpd_req_t *req)
{
    char url[128];
    build_redirect_url(url, sizeof(url));

    /* Compact HTML with three redirect mechanisms for maximum client compatibility:
     *   1. <meta http-equiv="refresh">  – handled by most basic browsers.
     *   2. JavaScript window.location   – handled by modern browsers.
     *   3. A visible hyperlink          – fallback for users whose browsers block both. */
    /* Buffer must hold static template (~215 bytes) + url (up to 128 bytes) × 3. */
    char body[640];
    snprintf(body, sizeof(body),
             "<!DOCTYPE html><html><head>"
             "<meta http-equiv=\"refresh\" content=\"0;url=%s\">"
             "<title>Redirecting</title></head><body>"
             "<p>Redirecting to <a href=\"%s\">device</a>...</p>"
             "<script>window.location.replace('%s');</script>"
             "</body></html>",
             url, url, url);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", url);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, body);

    ESP_LOGD(TAG, "Redirected '%s' -> '%s'", req->uri, url);
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * URI table
 *
 * Static lifetime required: ESP-IDF stores only the pointer to httpd_uri_t.
 * user_ctx is intentionally NULL – the handler reads config from s_cfg.
 * -------------------------------------------------------------------------- */
static const httpd_uri_t s_portal_uris[] = {
    /* Apple CNA (Captive Network Assistant) */
    { .uri = "/hotspot-detect.html",       .method = HTTP_GET,
      .handler = captive_portal_http_handler, .user_ctx = NULL },
    { .uri = "/library/test/success.html", .method = HTTP_GET,
      .handler = captive_portal_http_handler, .user_ctx = NULL },

    /* Android / Google connectivity check */
    { .uri = "/generate_204",              .method = HTTP_GET,
      .handler = captive_portal_http_handler, .user_ctx = NULL },
    { .uri = "/gen_204",                   .method = HTTP_GET,
      .handler = captive_portal_http_handler, .user_ctx = NULL },

    /* Microsoft Windows / Xbox connectivity check */
    { .uri = "/connecttest.txt",           .method = HTTP_GET,
      .handler = captive_portal_http_handler, .user_ctx = NULL },
    { .uri = "/ncsi.txt",                  .method = HTTP_GET,
      .handler = captive_portal_http_handler, .user_ctx = NULL },

    /* Windows Web Proxy Auto-Discovery */
    { .uri = "/wpad.dat",                  .method = HTTP_GET,
      .handler = captive_portal_http_handler, .user_ctx = NULL },

    /* Mozilla Firefox connectivity check */
    { .uri = "/success.txt",               .method = HTTP_GET,
      .handler = captive_portal_http_handler, .user_ctx = NULL },

    /* Generic redirect endpoint */
    { .uri = "/redirect",                  .method = HTTP_GET,
      .handler = captive_portal_http_handler, .user_ctx = NULL },

    /* Chrome / Chromium browser network time (wildcard URI –
     * requires httpd_config_t.uri_match_fn = httpd_uri_match_wildcard) */
    { .uri = "/browsernetworktime/*",      .method = HTTP_GET,
      .handler = captive_portal_http_handler, .user_ctx = NULL },
};

#define NUM_PORTAL_URIS  (sizeof(s_portal_uris) / sizeof(s_portal_uris[0]))

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

esp_err_t captive_portal_register(httpd_handle_t server,
                                  const captive_portal_config_t *config)
{
    if (server == NULL) {
        ESP_LOGE(TAG, "captive_portal_register: server handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    /* ---- Resolve effective configuration --------------------------------- */
    const char *eff_url  = NULL;
    const char *eff_key  = CONFIG_CAPTIVE_PORTAL_NETIF_KEY;
    uint16_t    eff_port = (uint16_t)(CONFIG_CAPTIVE_PORTAL_REDIRECT_PORT);

    if (config != NULL) {
        if (config->redirect_url  != NULL) { eff_url  = config->redirect_url;  }
        if (config->netif_key     != NULL) { eff_key  = config->netif_key;     }
        if (config->redirect_port != 0U)   { eff_port = config->redirect_port; }
    }

    /* Copy into module-static storage (safe for handler access from any task). */
    if (eff_url != NULL) {
        strlcpy(s_cfg.redirect_url, eff_url, sizeof(s_cfg.redirect_url));
    } else {
        s_cfg.redirect_url[0] = '\0';
    }
    strlcpy(s_cfg.netif_key, eff_key, sizeof(s_cfg.netif_key));
    s_cfg.redirect_port = eff_port;

    /* ---- Register URI handlers ------------------------------------------- */
    esp_err_t result = ESP_OK;

    for (size_t i = 0; i < NUM_PORTAL_URIS; i++) {
        esp_err_t err = httpd_register_uri_handler(server, &s_portal_uris[i]);
        if (err == ESP_ERR_HTTPD_HANDLER_EXISTS) {
            /* Handler already registered – treat as success (idempotent call). */
            ESP_LOGW(TAG, "URI '%s' already registered, skipping",
                     s_portal_uris[i].uri);
        } else if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register URI '%s': %s",
                     s_portal_uris[i].uri, esp_err_to_name(err));
            result = ESP_FAIL;
        }
    }

    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Captive portal ready (%zu URIs, netif: '%s')",
                 NUM_PORTAL_URIS, s_cfg.netif_key);
    }

    return result;
}
