# Cursor AI Security Audit - Architecture Recommendations

**Date:** 2025-12-21
**Audit:** Cursor AI Complete Codebase Review
**Scope:** Security vulnerabilities, authentication, network exposure
**Status:** RECOMMENDATIONS FOR FUTURE IMPLEMENTATION

---

## Executive Summary

This document addresses the remaining security concerns from the Cursor AI audit that require **architectural changes** or **design decisions** by the system owner. These issues cannot be fixed with simple code changes and require careful consideration of deployment scenarios, user requirements, and hardware constraints.

**Issues Addressed:**
1. Hardcoded Default Credentials
2. Telnet Server Without Authentication
3. No HTTPS/TLS Support
4. Weak Authentication Mechanism

**All safety-critical code issues (initialization, alarm validation, thread safety, string safety) have been FIXED** in commits 9b2b041 and earlier.

---

## 1. CRITICAL: Hardcoded Default Credentials

### Current State

**Location:** `src/web_server.cpp:38-39`, `src/config_unified.cpp:208-209`, `src/network_manager.cpp:46`

```cpp
// web_server.cpp
static char http_username[CONFIG_VALUE_LEN] = "admin";
static char http_password[CONFIG_VALUE_LEN] = "password";

// config_unified.cpp
if (!prefs.isKey(KEY_WEB_USERNAME)) prefs.putString(KEY_WEB_USERNAME, "admin");
if (!prefs.isKey(KEY_WEB_PASSWORD)) prefs.putString(KEY_WEB_PASSWORD, "password");

// network_manager.cpp
const char* ota_password = configGetString(KEY_OTA_PASSWORD, "bisso-ota");
```

### Risk Assessment

- **Severity:** CRITICAL
- **Impact:** Unauthorized access to web interface, OTA updates, system control
- **Exploit Difficulty:** Trivial (default credentials are publicly known)
- **CVSS Score:** 9.8 (Critical)

### Recommended Solutions

#### Option 1: Random Password Generation on First Boot (RECOMMENDED)

**Advantages:**
- Most secure default state
- Forces password change
- Prevents credential guessing

**Implementation:**
1. On first boot, generate cryptographically secure random password (16+ characters)
2. Display password via:
   - Serial console (permanent log)
   - LCD display (if available)
   - Web setup wizard (first-time only)
3. Require operator to acknowledge and change password
4. Set `password_changed` flag in NVS

**Code Example:**
```cpp
void networkManager::init() {
  if (!prefs.isKey("password_changed")) {
    // First boot - generate random password
    char random_pass[32];
    generateSecurePassword(random_pass, sizeof(random_pass));

    prefs.putString(KEY_WEB_PASSWORD, random_pass);
    prefs.putString(KEY_OTA_PASSWORD, random_pass);

    Serial.println("========================================");
    Serial.println("FIRST BOOT - GENERATED PASSWORD:");
    Serial.printf("Username: admin\n");
    Serial.printf("Password: %s\n", random_pass);
    Serial.println("CHANGE THIS PASSWORD IMMEDIATELY!");
    Serial.println("========================================");

    // Show on LCD if available
    lcdShowMessage("Password: %s", random_pass);
    lcdShowMessage("Change ASAP!");
  }
}

void generateSecurePassword(char* buffer, size_t len) {
  const char charset[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghjkmnpqrstuvwxyz23456789!@#$%";
  for (size_t i = 0; i < len - 1; i++) {
    buffer[i] = charset[esp_random() % (sizeof(charset) - 1)];
  }
  buffer[len - 1] = '\0';
}
```

#### Option 2: Force Password Change on First Login

**Advantages:**
- Simpler implementation
- Works without LCD display
- Standard industry practice

**Implementation:**
1. Set `password_must_change` flag on first boot
2. Redirect all web requests to password change page
3. Require new password meeting complexity requirements
4. Clear flag after successful change

#### Option 3: Hardware-Based Password Reset

**Advantages:**
- Physical security
- Works even if password forgotten
- No remote reset vulnerability

**Implementation:**
1. Reserve GPIO pin for "Factory Reset" button
2. Hold button for 10 seconds to reset credentials
3. Generate new random password
4. Display on serial/LCD

### Password Complexity Requirements

**Minimum Requirements:**
- **Length:** 12 characters minimum (enforce in `validateString()`)
- **Complexity:** At least 3 of: uppercase, lowercase, numbers, symbols
- **Dictionary Check:** Reject common passwords (optional)
- **History:** Prevent reuse of last 3 passwords

**Implementation:**
```cpp
static void validateString(const char* key, char* value, size_t len) {
  if (strcmp(key, KEY_WEB_PASSWORD) == 0 || strcmp(key, KEY_OTA_PASSWORD) == 0) {
    // Minimum length
    if (strlen(value) < 12) {
      logError("[CONFIG] Password too short (minimum 12 characters)");
      return;  // Reject
    }

    // Complexity check
    int uppercase = 0, lowercase = 0, digits = 0, symbols = 0;
    for (size_t i = 0; i < strlen(value); i++) {
      if (isupper(value[i])) uppercase++;
      else if (islower(value[i])) lowercase++;
      else if (isdigit(value[i])) digits++;
      else symbols++;
    }

    int complexity_types = (uppercase > 0) + (lowercase > 0) + (digits > 0) + (symbols > 0);
    if (complexity_types < 3) {
      logError("[CONFIG] Password too weak (need 3 of: upper, lower, digits, symbols)");
      return;  // Reject
    }
  }
}
```

### Password Storage

**Current:** Plaintext in NVS (INSECURE)

**Recommended:** SHA-256 hashing with salt

```cpp
void hashPassword(const char* password, const char* salt, char* hash_out, size_t hash_len) {
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
  mbedtls_md_starts(&ctx);

  // Hash password + salt
  mbedtls_md_update(&ctx, (const unsigned char*)password, strlen(password));
  mbedtls_md_update(&ctx, (const unsigned char*)salt, strlen(salt));

  unsigned char hash[32];
  mbedtls_md_finish(&ctx, hash);
  mbedtls_md_free(&ctx);

  // Convert to hex string
  for (int i = 0; i < 32 && i*2 < hash_len - 1; i++) {
    snprintf(hash_out + i*2, 3, "%02x", hash[i]);
  }
}
```

---

## 2. CRITICAL: Telnet Server Without Authentication

### Current State

**Location:** `src/network_manager.cpp:80-119`

```cpp
if (telnetClient && telnetClient.connected() && telnetClient.available()) {
  String cmd = telnetClient.readStringUntil('\n');
  cmd.trim();
  if (cmd.length() > 0) {
    cliProcessCommand(cmd.c_str());  // Direct execution without auth
  }
}
```

### Risk Assessment

- **Severity:** CRITICAL
- **Impact:** Remote code execution, unauthorized motion control, system compromise
- **Exploit Difficulty:** Trivial (telnet port 23 publicly accessible)
- **CVSS Score:** 10.0 (Critical)

### Recommended Solutions

#### Option 1: Disable Telnet in Production Builds (RECOMMENDED)

**Advantages:**
- Eliminates attack surface completely
- Simple implementation
- No performance impact

**Implementation:**
```cpp
#ifndef ENABLE_TELNET_DEBUG
  // Telnet disabled in production builds
  #define TELNET_ENABLED 0
#else
  // Telnet enabled only in debug builds
  #define TELNET_ENABLED 1
  #warning "Telnet enabled - DO NOT USE IN PRODUCTION"
#endif

void networkManager::update() {
  #if TELNET_ENABLED
    // Telnet server code here
    handleTelnetClients();
  #endif
}
```

**Build Configuration:**
```ini
# platformio.ini
[env:production]
build_flags =
  -DENABLE_TELNET_DEBUG=0  ; Telnet DISABLED

[env:debug]
build_flags =
  -DENABLE_TELNET_DEBUG=1  ; Telnet ENABLED
```

#### Option 2: Add Authentication to Telnet

**Advantages:**
- Keeps telnet functionality
- Adds security layer
- Standard practice

**Implementation:**
```cpp
// Telnet session state
struct telnet_session_t {
  WiFiClient client;
  bool authenticated;
  uint8_t failed_attempts;
  uint32_t last_attempt_time;
  char ip_address[16];
};

static telnet_session_t telnet_sessions[MAX_TELNET_CLIENTS];

void handleTelnetClient(telnet_session_t* session) {
  if (!session->client.connected()) {
    session->authenticated = false;
    return;
  }

  if (!session->authenticated) {
    // Authentication required
    session->client.println("BISSO E350 Controller");
    session->client.print("Username: ");
    String username = session->client.readStringUntil('\n');
    username.trim();

    session->client.print("Password: ");
    String password = session->client.readStringUntil('\n');
    password.trim();

    if (authenticateUser(username.c_str(), password.c_str())) {
      session->authenticated = true;
      session->failed_attempts = 0;
      logInfo("[TELNET] Authenticated: %s from %s", username.c_str(), session->ip_address);
      session->client.println("Login successful\n");
    } else {
      session->failed_attempts++;
      logWarning("[TELNET] Failed login attempt #%d from %s", session->failed_attempts, session->ip_address);

      if (session->failed_attempts >= 3) {
        logError("[TELNET] Too many failed attempts from %s - disconnecting", session->ip_address);
        session->client.println("Too many failed attempts. Disconnecting.");
        session->client.stop();

        // IP-based lockout (1 hour)
        blockIP(session->ip_address, 3600);
        return;
      }

      session->client.println("Authentication failed\n");
    }
  } else {
    // Authenticated - process commands
    if (session->client.available()) {
      String cmd = session->client.readStringUntil('\n');
      cmd.trim();
      if (cmd.length() > 0) {
        logInfo("[TELNET] Command from %s: %s", session->ip_address, cmd.c_str());
        cliProcessCommand(cmd.c_str());
      }
    }
  }
}
```

#### Option 3: Use SSH Instead of Telnet

**Advantages:**
- Encrypted communication
- Industry standard
- Built-in authentication

**Challenges:**
- Higher memory usage (mbedTLS overhead)
- More complex implementation
- May require external library (libssh-esp32)

**Note:** ESP32-S3 has sufficient RAM (512KB) for SSH server, but implementation is complex.

### Rate Limiting

Add rate limiting to prevent brute-force attacks:

```cpp
#define TELNET_MAX_ATTEMPTS_PER_HOUR 10

struct ip_rate_limit_t {
  char ip[16];
  uint8_t attempts;
  uint32_t window_start;
  bool blocked;
  uint32_t block_until;
};

bool checkRateLimit(const char* ip) {
  // Find or create rate limit entry
  ip_rate_limit_t* entry = findRateLimitEntry(ip);
  if (!entry) return true;  // No limit data, allow

  uint32_t now = millis();

  // Check if blocked
  if (entry->blocked && (now < entry->block_until)) {
    return false;  // Still blocked
  }

  // Reset window if expired
  if ((now - entry->window_start) > 3600000) {  // 1 hour
    entry->attempts = 0;
    entry->window_start = now;
    entry->blocked = false;
  }

  // Check attempts
  if (entry->attempts >= TELNET_MAX_ATTEMPTS_PER_HOUR) {
    entry->blocked = true;
    entry->block_until = now + 3600000;  // Block for 1 hour
    return false;
  }

  return true;
}
```

---

## 3. CRITICAL: No HTTPS/TLS Support

### Current State

All web server communication is unencrypted HTTP. Credentials passed via Basic Auth are base64-encoded but NOT encrypted.

### Risk Assessment

- **Severity:** HIGH (CRITICAL if exposed to internet)
- **Impact:** Credential interception, man-in-the-middle attacks, session hijacking
- **Exploit Difficulty:** Medium (requires network access)
- **CVSS Score:** 7.5 (High) - local network, 9.1 (Critical) - internet exposed

### Why HTTPS Is Challenging on ESP32

1. **Memory Overhead:** mbedTLS SSL/TLS requires ~40KB RAM per connection
2. **Performance:** Encryption/decryption overhead on embedded MCU
3. **Certificate Management:** Self-signed vs CA-signed complexity
4. **Code Complexity:** Significant implementation effort

### Recommended Solutions

#### Option 1: HTTPS with Self-Signed Certificate (BEST for Local Network)

**Advantages:**
- Encrypts all communication
- Prevents credential interception
- Works on local network

**Disadvantages:**
- Browser warnings (certificate not trusted)
- Users must add exception
- Certificate pinning required for security

**Implementation:**
```cpp
#include <HTTPSServer.h>
#include <SSLCert.h>

// Generate self-signed certificate (one-time)
SSLCert cert = SSLCert();

void webServer::init() {
  // Create HTTPS server on port 443
  server = new HTTPSServer(&cert);

  // Register all routes
  registerRoutes();

  server->start();
  Serial.println("[WEB] HTTPS server started on port 443");
}
```

**Certificate Generation (on ESP32):**
```cpp
void generateSelfSignedCert() {
  SSLCertConfig certConfig;
  certConfig.countryName = "US";
  certConfig.organizationName = "BISSO Industries";
  certConfig.commonName = "BISSO E350 Controller";
  certConfig.validFrom = 1609459200;  // 2021-01-01
  certConfig.validUntil = 1924992000;  // 2031-01-01

  SSLCert cert(certConfig);
  cert.save("/spiffs/server_cert.pem", "/spiffs/server_key.pem");
}
```

**Memory Consideration:**
- SSL handshake: ~40KB per connection
- Certificate storage: ~2KB
- Total overhead: ~45KB (acceptable on ESP32-S3 with 512KB RAM)

#### Option 2: VPN Access Only (RECOMMENDED for Production)

**Advantages:**
- Encrypted tunnel (WireGuard, OpenVPN)
- No code changes required
- Network-level security
- Works with existing HTTP server

**Implementation (User Side):**
1. Set up VPN server on local network gateway
2. Configure client devices with VPN credentials
3. Access controller only through VPN
4. Firewall blocks direct internet access

**Example (WireGuard):**
```bash
# On network gateway
sudo apt install wireguard
wg genkey | tee server_private.key | wg pubkey > server_public.key

# Configure WireGuard server
cat > /etc/wireguard/wg0.conf <<EOF
[Interface]
Address = 10.0.0.1/24
PrivateKey = <server_private_key>
ListenPort = 51820

[Peer]
PublicKey = <client_public_key>
AllowedIPs = 10.0.0.2/32
EOF

sudo wg-quick up wg0
```

**Client Configuration:**
```ini
[Interface]
Address = 10.0.0.2/24
PrivateKey = <client_private_key>

[Peer]
PublicKey = <server_public_key>
Endpoint = <gateway_ip>:51820
AllowedIPs = 192.168.1.0/24  # Local network access
PersistentKeepalive = 25
```

**Access Controller:**
```bash
# Connect to VPN
wg-quick up wg0

# Access controller through VPN tunnel
curl http://192.168.1.100/api/status
```

#### Option 3: SSH Tunnel (Alternative to VPN)

**Advantages:**
- No VPN infrastructure required
- Uses existing SSH server
- Port forwarding

**Implementation:**
```bash
# On client machine
ssh -L 8080:192.168.1.100:80 user@gateway

# Access controller through tunnel
open http://localhost:8080
```

### Certificate Pinning (If Using HTTPS)

To prevent MITM attacks with self-signed certificates:

```javascript
// web/js/security.js
const CERT_FINGERPRINT = "SHA256:1234567890abcdef...";

async function validateCertificate() {
  const response = await fetch('/api/cert-info');
  const certInfo = await response.json();

  if (certInfo.fingerprint !== CERT_FINGERPRINT) {
    alert("SECURITY WARNING: Certificate fingerprint mismatch!");
    throw new Error("Certificate validation failed");
  }
}
```

---

## 4. Authentication Mechanism Improvements

### Current State

HTTP Basic Auth with base64 encoding (not encryption). No session management, no CSRF protection, no token-based auth.

### Recommended Improvements

#### Token-Based Authentication

Replace Basic Auth with JWT (JSON Web Tokens):

```cpp
#include <jwt.h>

String generateJWT(const char* username) {
  jwt_t* jwt;
  jwt_new(&jwt);

  // Set claims
  jwt_add_grant(jwt, "iss", "BISSO E350");
  jwt_add_grant(jwt, "sub", username);
  jwt_add_grant_int(jwt, "exp", time(NULL) + 3600);  // 1 hour expiration

  // Sign with secret
  const char* secret = configGetString(KEY_JWT_SECRET, "change-this-secret");
  jwt_set_alg(jwt, JWT_ALG_HS256, (unsigned char*)secret, strlen(secret));

  char* token = jwt_encode_str(jwt);
  String result(token);

  jwt_free(jwt);
  free(token);

  return result;
}

bool validateJWT(const char* token) {
  jwt_t* jwt;
  const char* secret = configGetString(KEY_JWT_SECRET, "change-this-secret");

  int result = jwt_decode(&jwt, token, (unsigned char*)secret, strlen(secret));
  if (result != 0) return false;

  // Check expiration
  long exp = jwt_get_grant_int(jwt, "exp");
  jwt_free(jwt);

  return (time(NULL) < exp);
}
```

#### Session Management

```cpp
struct session_t {
  char token[64];
  char username[32];
  uint32_t created;
  uint32_t last_activity;
  bool active;
};

static session_t sessions[MAX_SESSIONS];

void createSession(const char* username, char* token_out, size_t token_len) {
  // Find free session slot
  session_t* session = findFreeSession();
  if (!session) {
    // Evict oldest inactive session
    session = findOldestSession();
  }

  // Generate random token
  generateSecureToken(session->token, sizeof(session->token));
  safe_strcpy(session->username, sizeof(session->username), username);
  session->created = millis();
  session->last_activity = millis();
  session->active = true;

  safe_strcpy(token_out, token_len, session->token);
  logInfo("[AUTH] Session created for %s", username);
}
```

#### CSRF Protection

```cpp
String generateCSRFToken() {
  char token[32];
  for (int i = 0; i < sizeof(token) - 1; i++) {
    token[i] = 'A' + (esp_random() % 26);
  }
  token[sizeof(token) - 1] = '\0';
  return String(token);
}

void webServerHandleMove(AsyncWebServerRequest* request) {
  // Verify CSRF token
  if (!request->hasHeader("X-CSRF-Token")) {
    request->send(403, "application/json", "{\"error\":\"CSRF token missing\"}");
    return;
  }

  String csrf_token = request->header("X-CSRF-Token");
  if (!validateCSRFToken(csrf_token.c_str())) {
    request->send(403, "application/json", "{\"error\":\"Invalid CSRF token\"}");
    return;
  }

  // Process request...
}
```

---

## 5. Deployment Security Checklist

### Before Production Deployment

**CRITICAL:**
- [ ] Change default web username/password
- [ ] Change default OTA password
- [ ] Disable telnet (or add authentication)
- [ ] Test physical E-Stop button
- [ ] Verify password complexity requirements
- [ ] Enable HTTPS (or use VPN)

**HIGH PRIORITY:**
- [ ] Configure WiFi WPA2 encryption
- [ ] Disable WiFi AP mode (use infrastructure mode)
- [ ] Set up firewall rules (block internet access)
- [ ] Configure IP whitelist for web access
- [ ] Test backup/restore procedures

**MEDIUM PRIORITY:**
- [ ] Enable audit logging
- [ ] Configure log retention policy
- [ ] Test password reset procedure
- [ ] Document recovery procedures
- [ ] Train operators on security

### Network Deployment Scenarios

#### Scenario 1: Isolated Local Network (RECOMMENDED)

```
[Internet] -X- (Firewall blocks) -X- [Local Network] --- [Controller]
                                         |
                                    [Operator PC]
```

**Security:**
- No internet exposure
- Local network only
- HTTP acceptable (trusted network)
- Physical security controls facility access

**Configuration:**
```cpp
#define ALLOW_INTERNET_ACCESS 0
#define REQUIRE_HTTPS 0
#define TELNET_ENABLED 0
```

#### Scenario 2: VPN Access

```
[Internet] --- [VPN Gateway] --- [Local Network] --- [Controller]
    |
[Remote User] ---(encrypted)---^
```

**Security:**
- Encrypted VPN tunnel
- HTTP acceptable (inside VPN)
- Multi-factor auth on VPN
- Audit logging

**Configuration:**
```cpp
#define ALLOW_INTERNET_ACCESS 0  // Controller still isolated
#define REQUIRE_HTTPS 0  // VPN provides encryption
#define VPN_REQUIRED 1
```

#### Scenario 3: HTTPS (If Internet Exposed) ⚠️ NOT RECOMMENDED

```
[Internet] --- [Firewall] --- [DMZ] --- [Controller]
```

**Security:**
- HTTPS required
- Strong passwords
- Rate limiting
- IDS/IPS monitoring
- Regular security updates

**Configuration:**
```cpp
#define ALLOW_INTERNET_ACCESS 1
#define REQUIRE_HTTPS 1
#define ENFORCE_STRONG_PASSWORDS 1
#define ENABLE_RATE_LIMITING 1
```

**WARNING:** Direct internet exposure NOT RECOMMENDED for industrial control systems!

---

## 6. Implementation Priority

### Phase 1: Immediate (Before Production)
1. Change default passwords
2. Disable or authenticate telnet
3. Deploy on isolated network or VPN

### Phase 2: Short-Term (Within 1 Month)
4. Implement password complexity requirements
5. Add password hashing (SHA-256)
6. Implement rate limiting
7. Add session management

### Phase 3: Long-Term (Future Enhancement)
8. Implement HTTPS support
9. Add certificate management
10. Implement JWT authentication
11. Add CSRF protection

---

## 7. References

- **ESP32-S3 mbedTLS Documentation:** https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/protocols/mbedtls.html
- **OWASP Authentication Cheat Sheet:** https://cheatsheetseries.owasp.org/cheatsheets/Authentication_Cheat_Sheet.html
- **NIST Password Guidelines:** https://pages.nist.gov/800-63-3/sp800-63b.html
- **Industrial Control System Security:** IEC 62443 series

---

## Conclusion

The security issues identified by Cursor AI are **valid and serious**, but most can be mitigated through proper **network architecture** and **deployment practices** rather than code changes alone.

**Recommended Approach:**
1. **Deploy on isolated local network** (highest priority)
2. **Change default credentials immediately**
3. **Disable telnet in production builds**
4. **Use VPN for remote access** (if needed)
5. **Plan HTTPS implementation** (future enhancement)

With these measures, the BISSO E350 Controller can be securely deployed in industrial environments while maintaining functionality and usability.
