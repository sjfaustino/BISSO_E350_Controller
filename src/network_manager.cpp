#include "network_manager.h"
#include "cli.h"
#include "config_keys.h"    // For KEY_OTA_PASSWORD, KEY_ETH_ENABLED
#include "config_unified.h" // For OTA password from NVS
#include "serial_logger.h"
#include "web_server.h"
#include "auth_manager.h"   // PHASE 5.10: For authVerifyCredentials (SHA-256)
#include "gcode_parser.h"   // For GCodeParser in telnet status
#include "motion.h"         // For motion functions
#include "safety.h"         // For safetyIsAlarmed
#include "board_variant.h"  // Board-specific GPIO definitions
#include <ArduinoOTA.h>
#include <DNSServer.h>  // For captive portal (used directly, not via ESPAsyncWiFiManager)
#include <time.h>            // For NTP time sync
#include "ota_manager.h"     // For otaStartBackgroundCheck
#include <esp_now.h>
#include <WiFi.h>

// Ethernet support: LAN8720 for v1.6, W5500 for v3.1
#include <ETH.h>
#if BOARD_HAS_W5500
  // KC868-A16 v3.1: W5500 SPI Ethernet via ESP-IDF drivers
  #include "esp_eth.h"
  #include "esp_eth_mac.h"
  #include "esp_eth_phy.h"
  #include "driver/spi_master.h"
  #include "esp_netif.h"
  #include "esp_event.h"
  #include "esp_eth_netif_glue.h"
  
  // SPI Host for S3
  #ifndef SPI2_HOST
    #define SPI2_HOST (spi_host_device_t)1
  #endif
  
  #define ETHERNET_AVAILABLE 1
#else
  // KC868-A16 v1.6: LAN8720 Ethernet PHY (RMII interface)
  #define ETH_PHY_TYPE    ETH_PHY_LAN8720
  #define ETH_PHY_ADDR    0
  #define ETH_PHY_MDC     PIN_ETH_MDC
  #define ETH_PHY_MDIO    PIN_ETH_MDIO
  #undef ETH_CLK_MODE  // Avoid redefinition warning from ETH.h
  #define ETH_CLK_MODE    ETH_CLOCK_GPIO17_OUT
  #define ETHERNET_AVAILABLE 1
#endif

NetworkManager networkManager;

// Telnet authentication state
enum TelnetAuthState {
  TELNET_AUTH_IDLE = 0,
  TELNET_AUTH_WAIT_USERNAME,
  TELNET_AUTH_WAIT_PASSWORD,
  TELNET_AUTH_AUTHENTICATED,
  TELNET_AUTH_LOCKED_OUT
};

static TelnetAuthState telnet_auth_state = TELNET_AUTH_IDLE;
static char telnet_username_attempt[32] = {0};
static uint8_t telnet_failed_attempts = 0;
static uint32_t telnet_lockout_until = 0;
static uint32_t telnet_last_activity = 0;

#define TELNET_MAX_FAILED_ATTEMPTS 3
#define TELNET_LOCKOUT_DURATION_MS 60000 // 1 minute lockout
#define TELNET_SESSION_TIMEOUT_MS 300000 // 5 minute inactivity timeout

// NTP configuration
#define NTP_SERVER "pool.ntp.org"
#define NTP_GMT_OFFSET 0           // UTC offset in seconds (0 = UTC)
#define NTP_DAYLIGHT_OFFSET 0      // Daylight saving offset in seconds
#define NTP_SYNC_INTERVAL_MS 3600000 // Resync every hour

static bool ntp_synced = false;
static uint32_t ntp_last_sync = 0;

// Telnet protocol commands for password masking
#define TELNET_IAC  255  // Interpret As Command
#define TELNET_WILL 251  // Will option
#define TELNET_WONT 252  // Won't option
#define TELNET_ECHO 1    // Echo option

// Helper: Suppress client echo (for password entry)
static void telnetSuppressEcho(WiFiClient& client) {
    uint8_t cmd[] = { TELNET_IAC, TELNET_WILL, TELNET_ECHO };
    client.write(cmd, 3);
    client.flush();
}

// Helper: Restore client echo and discard any IAC response
static void telnetRestoreEcho(WiFiClient& client) {
    uint8_t cmd[] = { TELNET_IAC, TELNET_WONT, TELNET_ECHO };
    client.write(cmd, 3);
    client.flush();
    // Give client time to respond, then discard IAC responses
    delay(50);
    while (client.available()) {
        uint8_t b = client.peek();
        if (b == TELNET_IAC) {
            client.read();  // Discard IAC
            if (client.available()) client.read();  // Discard command
            if (client.available()) client.read();  // Discard option
        } else {
            break;  // Not an IAC sequence, leave it
        }
    }
}

// Helper: Sync time via NTP (called when WiFi connected)
static void tryNtpSync() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    // Check if it's time to sync
    uint32_t now = millis();
    if (ntp_synced && (now - ntp_last_sync) < NTP_SYNC_INTERVAL_MS) return;
    
    // Configure NTP
    configTime(NTP_GMT_OFFSET, NTP_DAYLIGHT_OFFSET, NTP_SERVER);
    
    // Wait for time to be set (with timeout)
    struct tm timeinfo;
    int retries = 10;
    while (retries-- > 0) {
        if (getLocalTime(&timeinfo, 100)) {
            ntp_synced = true;
            ntp_last_sync = now;
            char buf[32];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
            logPrintf("[NTP] Time synced: %s UTC\r\n", buf);
            return;
        }
    }
    // Sync failed - will retry next update cycle if not yet synced
}

NetworkManager::NetworkManager() {
  telnetServer = nullptr;
  dnsServer = nullptr;
  clientConnected = false;
  ethernetConnected = false;
  ethernetLinkSpeed = 0;
}

NetworkManager::~NetworkManager() {
  // Clean up allocated resources
  if (telnetServer) {
    telnetServer->stop();
    delete telnetServer;
    telnetServer = nullptr;
  }
  if (dnsServer) {
    dnsServer->stop();
    delete dnsServer;
    dnsServer = nullptr;
  }
  if (telnetClient) {
    telnetClient.stop();
  }
}

static void resetTelnetAuthState() {
  telnet_auth_state = TELNET_AUTH_IDLE;
  memset(telnet_username_attempt, 0, sizeof(telnet_username_attempt));
}

// WiFi event handler - detects auth failures to stop redundant reconnect loops
static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
      uint8_t reason = info.wifi_sta_disconnected.reason;
      // 202 = WIFI_REASON_AUTH_FAIL, 15 = WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT
      if (reason == 202 || reason == 15) {
        logError("[WIFI] Auth Failed (Reason: %u). Stopping auto-reconnect to protect encoder bus.", reason);
        WiFi.setAutoReconnect(false);
      } else {
        logDebug("[WIFI] Disconnected (Reason: %u)", reason);
      }
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      logInfo("[WIFI] [OK] IP Address: %s", WiFi.localIP().toString().c_str());
      break;
    default:
      break;
  }
}

#if ETHERNET_AVAILABLE
// Ethernet event handler - called by WiFi event system (LAN8720 only)
static void onEthernetEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      logInfo("[ETH] Ethernet started");
      ETH.setHostname("bisso-e350");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      logInfo("[ETH] Ethernet link up");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      logInfo("[ETH] [OK] IP: %s, Speed: %dMbps, %s", 
              ETH.localIP().toString().c_str(),
              ETH.linkSpeed(),
              ETH.fullDuplex() ? "Full Duplex" : "Half Duplex");
      networkManager.ethernetConnected = true;
      networkManager.ethernetLinkSpeed = ETH.linkSpeed();
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      logWarning("[ETH] Ethernet disconnected");
      networkManager.ethernetConnected = false;
      networkManager.ethernetLinkSpeed = 0;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      logInfo("[ETH] Ethernet stopped");
      networkManager.ethernetConnected = false;
      break;
    default:
      break;
  }
}
#endif // ETHERNET_AVAILABLE

String NetworkManager::getEthernetIP() const {
#if ETHERNET_AVAILABLE
  if (ethernetConnected) {
    return ETH.localIP().toString();
  }
#endif
  return String("");
}

void NetworkManager::initEthernet() {
#if ETHERNET_AVAILABLE
  int eth_enabled = configGetInt(KEY_ETH_ENABLED, 0);  // Default: disabled to save memory
  
  if (!eth_enabled) {
    logInfo("[ETH] Ethernet disabled in config");
    return;
  }
  
  logInfo("[ETH] Initializing %s Ethernet PHY...", BOARD_HAS_W5500 ? "W5500" : "LAN8720");
  
  // Register Ethernet event handler before starting interface
  WiFi.onEvent(onEthernetEvent);
  
  // Start Ethernet with board-specific configuration
  bool success = false;
#if BOARD_HAS_W5500
  // W5500 SPI Ethernet initialization on v3.1 using ESP-IDF drivers
  // This manual path is required because Arduino 2.0.x lacks SPI Ethernet support in ETH.h
  
  logInfo("[ETH] Initializing W5500 SPI Ethernet...");
  
  // 1. SPI Bus initialization
  spi_bus_config_t buscfg = {
      .mosi_io_num = PIN_ETH_MOSI,
      .miso_io_num = PIN_ETH_MISO,
      .sclk_io_num = PIN_ETH_CLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
  };
  if (spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK) {
      logError("[ETH] Failed to initialize SPI bus");
      return;
  }
  
  // 2. SPI Device initialization
  spi_device_interface_config_t devcfg = {
      .command_bits = 16,
      .address_bits = 8,
      .mode = 0,
      .clock_speed_hz = 20 * 1000 * 1000, 
      .spics_io_num = PIN_ETH_CS,
      .queue_size = 20,
  };
  spi_device_handle_t spi_handle;
  if (spi_bus_add_device(SPI2_HOST, &devcfg, &spi_handle) != ESP_OK) {
      logError("[ETH] Failed to add SPI device");
      return;
  }
  
  // 3. MAC and PHY Configuration
  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
  phy_config.phy_addr = 1; // Standard for KC868-A16
  phy_config.reset_gpio_num = PIN_ETH_RST;
  
  eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(spi_handle);
  w5500_config.int_gpio_num = PIN_ETH_INT;
  
  esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
  esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
  
  // 4. Driver Install
  esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
  esp_eth_handle_t eth_handle = NULL;
  if (esp_eth_driver_install(&config, &eth_handle) != ESP_OK) {
      logError("[ETH] Failed to install Ethernet driver");
      return;
  }
  
  // 5. Netif integration (LwIP)
  // Note: We use the Arduino ESP32 context, so we might need to check if netif is already init
  esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
  esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);
  esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle));
  
  success = (esp_eth_start(eth_handle) == ESP_OK);
  
  if (success) {
      logInfo("[ETH] [OK] W5500 Ethernet started");
      // Since this is bypassing ETH.h, the standard ETH class won't track this.
      // We might need to manually update networkManager state.
  }
#else
  // RMII initialization for LAN8720 on v1.6
  success = ETH.begin(ETH_PHY_ADDR, -1, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_TYPE, ETH_CLK_MODE);
#endif

  if (success) {
    logInfo("[ETH] [OK] Ethernet initialization queued");
    
    // Configure static IP if DHCP disabled
    int use_dhcp = configGetInt(KEY_ETH_DHCP, 1);  // Default: DHCP
    if (!use_dhcp) {
      const char* ip_str = configGetString(KEY_ETH_IP, "");
      const char* gw_str = configGetString(KEY_ETH_GW, "");
      const char* mask_str = configGetString(KEY_ETH_MASK, "255.255.255.0");
      const char* dns_str = configGetString(KEY_ETH_DNS, "8.8.8.8");
      
      if (strlen(ip_str) > 0 && strlen(gw_str) > 0) {
        IPAddress ip, gateway, subnet, dns;
        if (ip.fromString(ip_str) && gateway.fromString(gw_str) &&
            subnet.fromString(mask_str) && dns.fromString(dns_str)) {
          ETH.config(ip, gateway, subnet, dns);
          logInfo("[ETH] Static IP configured: %s", ip_str);
        } else {
          logError("[ETH] Invalid static IP configuration - using DHCP");
        }
      } else {
        logWarning("[ETH] Static IP mode but no IP configured - using DHCP");
      }
    }
  } else {
    logError("[ETH] Failed to initialize Ethernet");
  }
#else
  logInfo("[ETH] Ethernet not configured for this board");
#endif
}

void NetworkManager::init() {
  logPrintln("[NET] Initializing Network Stack...");

  // Register WiFi event handler (always needed)
  WiFi.onEvent(onWiFiEvent);

  // 0. Ethernet Initialization (KC868-A16 LAN8720) - runs in parallel with WiFi
  initEthernet();

  // 1. WiFi Initialization (Non-blocking to allow boot to continue)
  int ap_enabled = configGetInt(KEY_WIFI_AP_EN, 1); // Phase 5.8: Configurable AP

  if (ap_enabled) {
    const char *ap_ssid = configGetString(KEY_WIFI_AP_SSID, "BISSO-E350-Setup");
    const char *ap_pass = configGetString(KEY_WIFI_AP_PASS, "password");

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ap_ssid, ap_pass);

    // PHASE 5.9: Captive Portal DNS with safety checks
    // NOTE: DNS server only responds to queries from AP clients (192.168.4.x)
    // Does NOT interfere with STA mode DNS resolution (ESP32 network stack keeps interfaces separate)
    // Cleanup existing DNS server if init() called multiple times (prevents memory leak)
    if (dnsServer) {
      dnsServer->stop();
      delete dnsServer;
      dnsServer = nullptr;
      logDebug("[NET] Cleaned up existing DNS server before re-init");
    }

    // Allocate and start DNS server for captive portal
    dnsServer = new DNSServer();
    if (dnsServer == nullptr) {
      logError("[NET] Failed to allocate DNS server (out of memory)");
    } else {
      // Start DNS server: Redirect all DNS queries to AP IP (192.168.4.1)
      // This triggers captive portal on mobile devices when they connect to AP
      if (dnsServer->start(NET_DNS_PORT, "*", WiFi.softAPIP())) {
        logInfo("[NET] [OK] Captive portal DNS started on port %d", NET_DNS_PORT);
      } else {
        logError("[NET] Failed to start DNS server (port conflict?)");
        delete dnsServer;
        dnsServer = nullptr;
      }
    }

    logInfo("[NET] AP Mode ENABLED (SSID: %s, IP: %s)", ap_ssid, WiFi.softAPIP().toString().c_str());
  } else {
    WiFi.mode(WIFI_STA);
    logInfo("[NET] AP Mode DISABLED (Station only)");

    // Ensure DNS server cleaned up if switching from AP to STA mode
    if (dnsServer) {
      dnsServer->stop();
      delete dnsServer;
      dnsServer = nullptr;
      logDebug("[NET] DNS server stopped (STA mode only)");
    }
  }

  // Try to connect to saved network without blocking
  WiFi.setAutoReconnect(true); // Ensure it tries to reconnect if disconnected
  WiFi.begin(); // Uses credentials from previous autoConnect()

  // Don't wait for connection - boot continues
  // WiFi will connect in background during networkManager.update() calls
  logInfo("[NET] [OK] WiFi initialization queued (non-blocking)");

  // NOTE: AsyncWiFiManager with autoConnect() was BLOCKING boot sequence
  // Removed to allow taskManagerStart() to execute immediately

  // 2. OTA Setup
  ArduinoOTA.setHostname("bisso-e350");

  // SECURITY FIX: Load OTA password from NVS instead of hardcoding
  // Prevents password exposure in source code and allows runtime changes
  const char *ota_password = configGetString(KEY_OTA_PASSWORD, "bisso-ota");
  int ota_pw_changed = configGetInt(KEY_OTA_PW_CHANGED, 0);

  ArduinoOTA.setPassword(ota_password);

  if (ota_pw_changed == 0) {
    logWarning("[OTA] Default password in use - change recommended!");
    logWarning("[OTA] Use CLI command: ota_setpass <new_password>");
  } else {
    logInfo("[OTA] [OK] Custom password loaded from NVS");
  }

  ArduinoOTA.onStart([]() {
    String type =
        (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    logInfo("[OTA] Start updating %s", type.c_str());
    // Safety: Stop Motion immediately
    // motionEmergencyStop();
  });
  ArduinoOTA.onEnd([]() { logPrintln("\n[OTA] End"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    logPrintf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    const char* err_str = "Unknown";
    if (error == OTA_AUTH_ERROR) err_str = "Auth Failed";
    else if (error == OTA_BEGIN_ERROR) err_str = "Begin Failed";
    else if (error == OTA_CONNECT_ERROR) err_str = "Connect Failed";
    else if (error == OTA_RECEIVE_ERROR) err_str = "Receive Failed";
    else if (error == OTA_END_ERROR) err_str = "End Failed";
    logError("[OTA] Error[%u]: %s", error, err_str);
  });
  ArduinoOTA.begin();

  // 3. Telnet Server
  telnetServer = new WiFiServer(TELNET_PORT);
  telnetServer->begin();
  telnetServer->setNoDelay(true);
  logPrintln("[NET] Telnet Server Started on Port 23 (Authentication Required)");
  
  // 4. Start background OTA update check (non-blocking)
  // DISABLED: Moved to main.cpp setup() for synchronous check at boot (better memory management)
  // otaStartBackgroundCheck();

  // 5. Initialize ESP-NOW for Remote DRO
  if (esp_now_init() == ESP_OK) {
    logInfo("[NET] [OK] ESP-NOW initialized for Remote DRO");
    
    // Add broadcast peer
    esp_now_peer_info_t peerInfo = {};
    memset(peerInfo.peer_addr, 0xFF, 6); // Broadcast address
    peerInfo.channel = 0;  // Use current channel
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      logError("[NET] Failed to add ESP-NOW broadcast peer");
    }
  } else {
    logError("[NET] Failed to initialize ESP-NOW");
  }
}

void NetworkManager::update() {
  // 1. Handle OTA
  ArduinoOTA.handle();

  // 1.1 Handle DNS (Captive Portal)
  if (dnsServer) {
    dnsServer->processNextRequest();
  }

  // 1.2 NTP Time Sync (when WiFi connected)
  tryNtpSync();

  // 2. Handle Telnet
  if (telnetServer->hasClient()) {
    if (!telnetClient || !telnetClient.connected()) {
      if (telnetClient)
        telnetClient.stop();
      telnetClient = telnetServer->available();
      clientConnected = true;
      telnetClient.flush();

      // PHASE 5.10: Check lockout BEFORE resetting auth state
      // (bug: reset was clearing lockout state before checking it)
      if (telnet_auth_state == TELNET_AUTH_LOCKED_OUT &&
          millis() < telnet_lockout_until) {
        telnetClient.println("Connection refused: Too many failed attempts.");
        telnetClient.println("Try again later.");
        telnetClient.stop();
        return;
      }

      // Clear lockout if expired
      if (telnet_auth_state == TELNET_AUTH_LOCKED_OUT &&
          millis() >= telnet_lockout_until) {
        telnet_auth_state = TELNET_AUTH_IDLE;
        telnet_failed_attempts = 0;
      }

      // Reset auth state for new connection (after lockout check)
      resetTelnetAuthState();

      telnetClient.println("==================================");
      telnetClient.println("   BISSO E350 REMOTE TERMINAL     ");
      telnetClient.println("==================================");
      telnetClient.println("Authentication required.");
      telnetClient.print("Username: ");
      telnet_auth_state = TELNET_AUTH_WAIT_USERNAME;
      telnet_last_activity = millis();
      logInfo("[NET] Telnet Client Connected - Awaiting Authentication");
    } else {
      // Reject multiple clients (Simple 1-client implementation)
      WiFiClient reject = telnetServer->available();
      reject.stop();
    }
  }

  // Check session timeout
  if (telnetClient && telnetClient.connected() &&
      telnet_auth_state == TELNET_AUTH_AUTHENTICATED &&
      (millis() - telnet_last_activity) > TELNET_SESSION_TIMEOUT_MS) {
    telnetClient.println("\r\nSession timed out due to inactivity.");
    telnetClient.stop();
    resetTelnetAuthState();
    logInfo("[NET] Telnet Session Timed Out");
    return;
  }

  // Process Telnet Input
  if (telnetClient && telnetClient.connected() && telnetClient.available()) {
    String input = telnetClient.readStringUntil('\n');
    input.trim();
    telnet_last_activity = millis();

    if (input.length() == 0)
      return;

    switch (telnet_auth_state) {
    case TELNET_AUTH_WAIT_USERNAME:
      strncpy(telnet_username_attempt, input.c_str(),
              sizeof(telnet_username_attempt) - 1);
      telnet_username_attempt[sizeof(telnet_username_attempt) - 1] = '\0';
      telnetSuppressEcho(telnetClient);  // Hide password as user types
      telnetClient.print("Password: ");
      telnet_auth_state = TELNET_AUTH_WAIT_PASSWORD;
      break;

    case TELNET_AUTH_WAIT_PASSWORD: {
      // PHASE 5.10: Use auth_manager for credential verification (SHA-256)
      // Previously used plain config credentials, now synced with web auth
      telnetRestoreEcho(telnetClient);  // Restore echo after password entry
      if (authVerifyCredentials(telnet_username_attempt, input.c_str())) {
        // Authentication successful
        telnet_auth_state = TELNET_AUTH_AUTHENTICATED;
        telnet_failed_attempts = 0;
        // Log to serial first (before telnet gets prompt)
        logPrintf("[INFO]  [NET] Telnet Auth SUCCESS for user '%s'\r\n", telnet_username_attempt);
        telnetClient.println("\r\nAuthentication successful.");
        telnetClient.println("Type 'help' for available commands.");
        telnetClient.print("> ");
      } else {
        // Authentication failed
        telnet_failed_attempts++;
        logWarning("[NET] Telnet Auth FAILED (attempt %d/%d)",
                      telnet_failed_attempts, TELNET_MAX_FAILED_ATTEMPTS);

        if (telnet_failed_attempts >= TELNET_MAX_FAILED_ATTEMPTS) {
          telnetClient.println(
              "\r\nToo many failed attempts. Connection closed.");
          telnet_auth_state = TELNET_AUTH_LOCKED_OUT;
          telnet_lockout_until = millis() + TELNET_LOCKOUT_DURATION_MS;
          telnetClient.stop();
          logWarning("[NET] Telnet LOCKOUT for %d seconds",
                        TELNET_LOCKOUT_DURATION_MS / 1000);
        } else {
          telnetClient.println("\r\nInvalid credentials.");
          telnetClient.print("Username: ");
          telnet_auth_state = TELNET_AUTH_WAIT_USERNAME;
        }
      }
      break;
    }

    case TELNET_AUTH_AUTHENTICATED:
      // Authenticated - process command
      if (input.equalsIgnoreCase("logout") || input.equalsIgnoreCase("exit")) {
        telnetClient.println("Goodbye.");
        telnetClient.stop();
        resetTelnetAuthState();
        logPrintln("[INFO]  [NET] Telnet Client Logged Out");
      } else if (input == "?") {
        // Real-time status query - format Grbl-compatible status
        const char* state = "Idle";
        if (motionIsEmergencyStopped()) state = "Alarm";
        else if (safetyIsAlarmed()) state = "Hold:1";
        else if (motionIsMoving()) state = "Run";
        
        float mPos[4] = { motionGetPositionMM(0), motionGetPositionMM(1), 
                          motionGetPositionMM(2), motionGetPositionMM(3) };
        float wPos[4];
        for (int i = 0; i < 4; i++) wPos[i] = gcodeParser.getWorkPosition(i, mPos[i]);
        
        char buf[160];
        snprintf(buf, sizeof(buf), "<%s|MPos:%.3f,%.3f,%.3f,%.3f|WPos:%.3f,%.3f,%.3f,%.3f>",
            state, mPos[0], mPos[1], mPos[2], mPos[3], wPos[0], wPos[1], wPos[2], wPos[3]);
        telnetClient.println(buf);
        telnetClient.print("> ");
      } else if (input == "!") {
        // Feed hold
        motionPause();
        telnetClient.println("ok");
        telnetClient.print("> ");
      } else if (input == "~") {
        // Cycle start / resume
        motionResume();
        telnetClient.println("ok");
        telnetClient.print("> ");
      } else {
        // Log to serial only (not mirrored to telnet)
        logPrintf("[INFO]  [NET] Remote Command: %s\r\n", input.c_str());
        // Inject into CLI processor
        cliProcessCommand(input.c_str());
        telnetClient.print("> ");
      }
      break;

    default:
      // Invalid state - reset
      resetTelnetAuthState();
      break;
    }
  }
}

void NetworkManager::telnetPrint(const char *str) {
  if (telnetClient && telnetClient.connected() &&
      telnet_auth_state == TELNET_AUTH_AUTHENTICATED) {
    // Convert LF to CRLF for telnet protocol compliance
    const char *p = str;
    while (*p) {
      if (*p == '\n' && (p == str || *(p-1) != '\r')) {
        telnetClient.print("\r\n");
      } else {
        telnetClient.write(*p);
      }
      p++;
    }
  }
}

void NetworkManager::telnetPrintln(const char *str) {
  if (telnetClient && telnetClient.connected() &&
      telnet_auth_state == TELNET_AUTH_AUTHENTICATED) {
    // Convert LF to CRLF for telnet protocol compliance
    const char *p = str;
    while (*p) {
      if (*p == '\n' && (p == str || *(p-1) != '\r')) {
        telnetClient.print("\r\n");
      } else {
        telnetClient.write(*p);
      }
      p++;
    }
    telnetClient.print("\r\n");
  }
}
