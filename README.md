# esp_captive_portal

A standalone, zero-dependency ESP-IDF component that implements a **captive portal** for Wi-Fi AP mode. After a single function call, any device that connects to your ESP32 soft-AP (iOS, macOS, Android, Windows, Firefox, Chrome) will automatically be redirected to your device's built-in web interface.

>**NOTE** Some devices DO NOT support captive portal for security reasonsm this is the only limitation!

---

## Features

- **Single function call** — works immediately after `httpd_start()`
- **Automatic IP detection** — reads the AP interface IP at request time; survives runtime IP changes
- **All major OS probes handled** — iOS/macOS, Android, Windows/Xbox, Firefox, Chrome/Chromium
- **HTTP 302 redirect** — with meta-refresh and JS fallback for older clients
- **Fully configurable** via Kconfig or runtime `captive_portal_config_t`
- **No external dependencies** — only ESP-IDF built-in components (`esp_http_server`, `esp_netif`, `log`)
- **Idempotent registration** — safe to call after an HTTP server restart

---

## Installation

### Option A — Espressif Component Registry (command-line/GUI)

Install directly from the Espressif Component Registry using the command-line.

- Using the `esp` CLI:
```
esp component install esp_captive_portal
```

- Or using `idf.py` component manager:
```
idf.py add-dependency 'esp_captive_portal:>=1.0.0'
idf.py update-dependencies
```

**Or use the included ESP-IDF component registery GUI, search for `esp_captive_portal` and install.**

Espressif's tooling will fetch and install the component into your project.

### Option B — ESP-IDF Component Manager (idf_component.yml)

Add to your `main/idf_component.yml`:

```yaml
dependencies:
  esp_captive_portal: ">=1.0.0"
```

Then run:

```sh
idf.py update-dependencies
```

### Option C — Copy into your project's `components/` directory

```
your_project/
├── components/
│   └── esp_captive_portal/   ← copy this folder here
├── main/
└── CMakeLists.txt
```

ESP-IDF automatically discovers components in the `components/` directory at the project root.

---

## Quick Start

### 1. Configure the HTTP server for wildcard URI matching

The `/browsernetworktime/*` Chrome probe requires wildcard URI matching. Set this **before** calling `httpd_start()`:

```c
#include "esp_http_server.h"
#include "captive_portal.h"

httpd_config_t config = HTTPD_DEFAULT_CONFIG();
config.uri_match_fn = httpd_uri_match_wildcard;  // required for Chrome probe

httpd_handle_t server = NULL;
httpd_start(&server, &config);
```

### 2. Register the captive portal

Call **once**, immediately after `httpd_start()`, before registering your application URIs:

```c
// Use Kconfig defaults (recommended)
captive_portal_register(server, NULL);

// ── then register your application handlers ──
httpd_register_uri_handler(server, &my_main_page);
httpd_register_uri_handler(server, &my_api_endpoint);
```

That's it. All captive portal probes are now intercepted.

---

## Configuration

### Runtime configuration (overrides Kconfig defaults)

```c
captive_portal_config_t portal_cfg = CAPTIVE_PORTAL_CONFIG_DEFAULT();

// Override specific fields as needed:
portal_cfg.redirect_url  = "http://10.0.0.1/";   // fixed URL (skips IP detection)
portal_cfg.netif_key     = "WIFI_AP_DEF";         // interface for IP detection
portal_cfg.redirect_port = 8080;                  // non-standard port

captive_portal_register(server, &portal_cfg);
```

Pass `NULL` instead of a config pointer to use all Kconfig compile-time defaults.

### Kconfig (menuconfig)

```
Component config → Captive Portal
  ├── Network interface key for IP auto-detection  [WIFI_AP_DEF]
  ├── Fallback redirect IP address                 [192.168.4.1]
  └── Redirect target TCP port                     [80]
```

Open with:

```sh
idf.py menuconfig
```

---

## Advanced: Catch-All Handler

The component's registered URIs cover all known OS probes. For applications that want to redirect *all* unknown requests (e.g. a dedicated configuration portal), you can register a wildcard catch-all **after** all application-specific handlers:

```c
// Register application handlers first
captive_portal_register(server, NULL);
httpd_register_uri_handler(server, &my_main_page);
httpd_register_uri_handler(server, &my_api);

// Catch-all: must be registered last; requires uri_match_fn = httpd_uri_match_wildcard
static esp_err_t catch_all(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, "");
    return ESP_OK;
}

static const httpd_uri_t catch_all_uri = {
    .uri     = "/*",
    .method  = HTTP_GET,
    .handler = catch_all,
};
httpd_register_uri_handler(server, &catch_all_uri);
```

> **Note:** ESP-IDF evaluates URI handlers in registration order. Registering the catch-all last ensures specific routes are matched first. THIS MEANS YOU SHOULD PLACE THE CATCH-ALL AT THE END!
> **Another Note:** Do not use the catch all if you intend to serve addresses beyound the declared URI handlers (a simple internet server for example)

---

## API Reference

### `captive_portal_register()`

```c
esp_err_t captive_portal_register(httpd_handle_t server,
                                  const captive_portal_config_t *config);
```

| Parameter | Description |
|-----------|-------------|
| `server`  | Running `httpd_handle_t` from `httpd_start()`. Must not be NULL. |
| `config`  | Runtime configuration or NULL to use Kconfig defaults. |

**Returns:** `ESP_OK`, `ESP_ERR_INVALID_ARG` (NULL server), or `ESP_FAIL` (registration error).

### `captive_portal_config_t`

| Field           | Type           | Description |
|-----------------|----------------|-------------|
| `redirect_url`  | `const char *` | Fixed redirect URL. NULL = auto-detect from `netif_key`. |
| `netif_key`     | `const char *` | esp_netif interface key. NULL = `CONFIG_CAPTIVE_PORTAL_NETIF_KEY`. |
| `redirect_port` | `uint16_t`     | Web server port. 0 = `CONFIG_CAPTIVE_PORTAL_REDIRECT_PORT`. Port 80 is omitted from URL. |

---

## Requirements

| Requirement | Version |
|-------------|---------|
| ESP-IDF     | ≥ 5.0   |
| ESP32 target | esp32, esp32s2, esp32s3, esp32c3, esp32c6, esp32h2 |

---

## License

MIT License. See [LICENSE](LICENSE) for details.

---

## Changelog

### 1.0.0 (2026-03-25)
- Initial release
- Handles iOS/macOS, Android, Windows, Firefox, and Chrome OS probes
- Single-function API with Kconfig and runtime configurability
- Automatic AP IP detection via esp_netif
