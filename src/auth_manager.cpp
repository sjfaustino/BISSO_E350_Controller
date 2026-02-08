/**
 * @file auth_manager.cpp
 * @brief Secure authentication with SHA-256 password hashing
 * @details PHASE 5.10: Security hardening - CVSS 7.5 vulnerability fix
 */

#include "auth_manager.h"
#include "cli.h"
#include "config_unified.h"
#include "config_keys.h"
#include "serial_logger.h"
#include "system_constants.h"
#include <Preferences.h>
#include <mbedtls/sha256.h>
#include <esp_random.h>
#include <string.h>
#include "string_safety.h"
#include "system_utils.h" // PHASE 8.1

// Internal state
static char current_username[32] = "admin";
static char stored_password_hash[AUTH_MAX_STORED_PW_LEN] = "";
static bool password_change_required = false;
static bool credentials_loaded = false;

// Rate limiting for brute force protection
#define AUTH_RATE_LIMIT_MAX_IPS 16
#define AUTH_RATE_LIMIT_MAX_ATTEMPTS 5
#define AUTH_RATE_LIMIT_WINDOW_MS 60000  // 1 minute

struct AuthRateLimitEntry {
  char ip_address[16];  // "xxx.xxx.xxx.xxx"
  uint32_t attempt_count;
  uint32_t first_attempt_time;
  uint32_t last_attempt_time;
};

static AuthRateLimitEntry rate_limit_table[AUTH_RATE_LIMIT_MAX_IPS];
static int rate_limit_entries = 0;

// Minimum password requirements
#define MIN_PASSWORD_LENGTH 8
#define MAX_PASSWORD_LENGTH 64

/**
 * @brief Convert binary data to hex string
 * @param data Binary data
 * @param data_len Length of binary data
 * @param output Output buffer for hex string
 * @param output_len Length of output buffer (must be >= data_len*2 + 1)
 */
static void binToHex(const uint8_t* data, size_t data_len, char* output, size_t output_len) {
  if (output_len < data_len * 2 + 1) {
    logError("[AUTH] Output buffer too small for hex conversion");
    return;
  }

  for (size_t i = 0; i < data_len; i++) {
    snprintf(output + i * 2, 3, "%02x", data[i]);
  }
  output[data_len * 2] = '\0';
}

/**
 * @brief Convert hex string to binary data
 * @param hex Hex string
 * @param output Output buffer for binary data
 * @param output_len Length of output buffer
 * @return Number of bytes written, or 0 on error
 */
static size_t hexToBin(const char* hex, uint8_t* output, size_t output_len) {
  size_t hex_len = strlen(hex);
  if (hex_len % 2 != 0 || hex_len / 2 > output_len) {
    return 0;
  }

  for (size_t i = 0; i < hex_len / 2; i++) {
    char byte_str[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
    output[i] = (uint8_t)strtol(byte_str, NULL, 16);
  }

  return hex_len / 2;
}

/**
 * @brief Hash password with SHA-256 and salt
 * @param password Plain text password
 * @param salt Salt bytes (AUTH_SALT_BYTES)
 * @param output Output buffer for hash (AUTH_HASH_BYTES)
 */
static void hashPassword(const char* password, const uint8_t* salt, uint8_t* output) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);  // 0 = SHA-256 (not SHA-224)

  // Hash: salt + password
  mbedtls_sha256_update(&ctx, salt, AUTH_SALT_BYTES);
  mbedtls_sha256_update(&ctx, (const uint8_t*)password, strlen(password));

  mbedtls_sha256_finish(&ctx, output);
  mbedtls_sha256_free(&ctx);
}

/**
 * @brief Create password hash string for storage
 * @param password Plain text password
 * @param output Output buffer (AUTH_MAX_STORED_PW_LEN)
 */
static void createPasswordHash(const char* password, char* output, size_t output_len) {
  if (output_len < AUTH_MAX_STORED_PW_LEN) {
    logError("[AUTH] Output buffer too small for password hash");
    return;
  }

  // Generate random salt using ESP32 hardware RNG
  uint8_t salt[AUTH_SALT_BYTES];
  esp_fill_random(salt, AUTH_SALT_BYTES);

  // Hash password
  uint8_t hash[AUTH_HASH_BYTES];
  hashPassword(password, salt, hash);

  // Convert to hex strings
  char salt_hex[AUTH_SALT_BYTES * 2 + 1];
  char hash_hex[AUTH_HASH_BYTES * 2 + 1];
  binToHex(salt, AUTH_SALT_BYTES, salt_hex, sizeof(salt_hex));
  binToHex(hash, AUTH_HASH_BYTES, hash_hex, sizeof(hash_hex));

  // Format: $sha256$<salt_hex>$<hash_hex>
  snprintf(output, output_len, "$sha256$%s$%s", salt_hex, hash_hex);
}

/**
 * @brief Verify password against stored hash
 * @param password Plain text password
 * @param stored_hash Stored hash string (format: $sha256$<salt>$<hash>)
 * @return true if password matches
 */
static bool verifyPassword(const char* password, const char* stored_hash) {
  // Check format
  if (strncmp(stored_hash, "$sha256$", 8) != 0) {
    // Legacy plain text password - upgrade needed
    logWarning("[AUTH] Plain text password detected - auto-upgrading to hashed");
    return strcmp(password, stored_hash) == 0;
  }

  // Parse: $sha256$<salt_hex>$<hash_hex>
  const char* salt_hex = stored_hash + 8;  // Skip "$sha256$"
  const char* hash_hex = strchr(salt_hex, '$');
  if (!hash_hex) {
    logError("[AUTH] Invalid hash format - missing hash separator");
    return false;
  }
  hash_hex++;  // Skip '$'

  // Extract salt
  size_t salt_hex_len = hash_hex - salt_hex - 1;
  if (salt_hex_len != AUTH_SALT_BYTES * 2) {
    logError("[AUTH] Invalid salt length: %d (expected %d)", salt_hex_len, AUTH_SALT_BYTES * 2);
    return false;
  }

  char salt_hex_str[AUTH_SALT_BYTES * 2 + 1];
  SAFE_STRCPY(salt_hex_str, salt_hex, sizeof(salt_hex_str));

  uint8_t salt[AUTH_SALT_BYTES];
  if (hexToBin(salt_hex_str, salt, sizeof(salt)) != AUTH_SALT_BYTES) {
    logError("[AUTH] Failed to decode salt");
    return false;
  }

  // Extract expected hash
  uint8_t expected_hash[AUTH_HASH_BYTES];
  if (hexToBin(hash_hex, expected_hash, sizeof(expected_hash)) != AUTH_HASH_BYTES) {
    logError("[AUTH] Failed to decode hash");
    return false;
  }

  // Compute actual hash
  uint8_t actual_hash[AUTH_HASH_BYTES];
  hashPassword(password, salt, actual_hash);

  // Constant-time comparison to prevent timing attacks
  uint8_t diff = 0;
  for (size_t i = 0; i < AUTH_HASH_BYTES; i++) {
    diff |= expected_hash[i] ^ actual_hash[i];
  }

  return diff == 0;
}

void authGenerateRandomPassword(char* output, size_t length) {
  if (length < 13) {
    logError("[AUTH] Output buffer too small for generated password");
    return;
  }

  // Character sets
  const char* lowercase = "abcdefghijklmnopqrstuvwxyz";
  const char* uppercase = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  const char* digits = "0123456789";
  const char* symbols = "!@#$%^&*";

  // Generate 12-character password: 3 lowercase + 3 uppercase + 3 digits + 3 symbols
  uint8_t random_bytes[12];
  esp_fill_random(random_bytes, 12);

  output[0] = lowercase[random_bytes[0] % 26];
  output[1] = lowercase[random_bytes[1] % 26];
  output[2] = lowercase[random_bytes[2] % 26];
  output[3] = uppercase[random_bytes[3] % 26];
  output[4] = uppercase[random_bytes[4] % 26];
  output[5] = uppercase[random_bytes[5] % 26];
  output[6] = digits[random_bytes[6] % 10];
  output[7] = digits[random_bytes[7] % 10];
  output[8] = digits[random_bytes[8] % 10];
  output[9] = symbols[random_bytes[9] % 8];
  output[10] = symbols[random_bytes[10] % 8];
  output[11] = symbols[random_bytes[11] % 8];
  output[12] = '\0';
}

bool authValidatePasswordStrength(const char* password) {
  if (!password) {
    return false;
  }

  size_t len = strlen(password);
  if (len < MIN_PASSWORD_LENGTH || len > MAX_PASSWORD_LENGTH) {
    logWarning("[AUTH] Password length %d not in range [%d, %d]", len, MIN_PASSWORD_LENGTH, MAX_PASSWORD_LENGTH);
    return false;
  }

  // Check for weak passwords
  const char* weak_passwords[] = {
    "password", "12345678", "admin123", "qwerty12", "00000000",
    "Password1", "Admin123", "letmein1", "welcome1"
  };

  for (size_t i = 0; i < sizeof(weak_passwords) / sizeof(weak_passwords[0]); i++) {
    if (strcmp(password, weak_passwords[i]) == 0) {
      logWarning("[AUTH] Password matches weak password list");
      return false;
    }
  }

  // Require at least 3 character types (lowercase, uppercase, digits, symbols)
  bool has_lower = false, has_upper = false, has_digit = false, has_symbol = false;
  for (size_t i = 0; i < len; i++) {
    if (password[i] >= 'a' && password[i] <= 'z') has_lower = true;
    else if (password[i] >= 'A' && password[i] <= 'Z') has_upper = true;
    else if (password[i] >= '0' && password[i] <= '9') has_digit = true;
    else has_symbol = true;
  }

  int char_types = (has_lower ? 1 : 0) + (has_upper ? 1 : 0) + (has_digit ? 1 : 0) + (has_symbol ? 1 : 0);
  if (char_types < 3) {
    logWarning("[AUTH] Password needs at least 3 character types (lower/upper/digit/symbol)");
    return false;
  }

  return true;
}

void authInit() {
  logModuleInit("AUTH");

  Preferences prefs;
  prefs.begin("auth", true);  // Read-only

  // Load username
  String user = prefs.getString("username", "");
  if (user.length() > 0) {
    SAFE_STRCPY(current_username, user.c_str(), sizeof(current_username));
  }

  // Load password hash
  String pw_hash = prefs.getString("password", "");
  bool is_first_boot = (pw_hash.length() == 0);

  prefs.end();

  if (is_first_boot) {
    // PHASE 5.10: Generate random credentials on first boot
    logWarning("[AUTH] First boot detected - generating random credentials");

    char random_password[13];
    authGenerateRandomPassword(random_password, sizeof(random_password));

    // Create hash
    createPasswordHash(random_password, stored_password_hash, sizeof(stored_password_hash));

    // Save to NVS
    prefs.begin("auth", false);  // Read-write
    prefs.putString("username", current_username);
    prefs.putString("password", stored_password_hash);
    prefs.putBool("first_boot", false);
    prefs.end();

    // Display credentials (thread-safe)
    logPrintln("");
    logPrintln("╔══════════════════════════════════════════════════════════════╗");
    logPrintln("║           FIRST BOOT - RANDOM CREDENTIALS GENERATED          ║");
    logPrintln("╠══════════════════════════════════════════════════════════════╣");
    logPrintf("║  Username: %-48s ║\n", current_username);
    logPrintf("║  Password: %-48s ║\n", random_password);
    logPrintln("║                                                              ║");
    logPrintln("║  ⚠️  SAVE THESE CREDENTIALS - THEY WILL NOT BE SHOWN AGAIN  ║");
    logPrintln("║                                                              ║");
    logPrintln("║  Change password via web interface or CLI:                  ║");
    logPrintln("║    web_setpass <new_password>                               ║");
    logPrintln("╚══════════════════════════════════════════════════════════════╝");
    logPrintln("");

    password_change_required = false;  // Random password is already strong
    credentials_loaded = true;
  } else {
    // Load existing credentials
    SAFE_STRCPY(stored_password_hash, pw_hash.c_str(), sizeof(stored_password_hash));

    // Check if legacy plain text
    if (strncmp(stored_password_hash, "$sha256$", 8) != 0) {
      logWarning("[AUTH] Legacy plain text password detected - upgrade required");
      password_change_required = true;
    } else {
      logInfo("[AUTH] Secure hashed credentials loaded");
      password_change_required = false;
    }

    credentials_loaded = true;
  }
}

bool authVerifyCredentials(const char* username, const char* password) {
  if (!credentials_loaded) {
    logError("[AUTH] Credentials not loaded - call authInit() first");
    return false;
  }

  if (!username || !password) {
    logWarning("[AUTH] NULL username or password");
    return false;
  }

  // Verify username
  if (strcmp(username, current_username) != 0) {
    // Log failed attempt with full details for security monitoring
    logWarning("[AUTH] FAILED LOGIN - Invalid username");
    logWarning("[AUTH]   Attempted user: '%s'", username);
    logWarning("[AUTH]   Attempted pass: '%s' (len=%d)", password, strlen(password));
    return false;
  }

  // Verify password
  bool valid = verifyPassword(password, stored_password_hash);

  if (!valid) {
    // Log failed attempt with full details for security monitoring / debugging
    logWarning("[AUTH] FAILED LOGIN - Invalid password");
    logWarning("[AUTH]   Username: '%s' (correct)", username);
    logWarning("[AUTH]   Password: '%s' (len=%d)", password, strlen(password));
    logWarning("[AUTH]   Expected hash prefix: %.20s...", stored_password_hash);
    return false;
  }

  // Login successful (don't log on every request - too verbose)

  if (strncmp(stored_password_hash, "$sha256$", 8) != 0) {
    // Auto-upgrade legacy plain text password to hashed
    logInfo("[AUTH] Auto-upgrading plain text password to SHA-256");
    createPasswordHash(password, stored_password_hash, sizeof(stored_password_hash));

    Preferences prefs;
    prefs.begin("auth", false);
    prefs.putString("password", stored_password_hash);
    prefs.end();

    password_change_required = false;
  }

  return valid;
}

bool authSetPassword(const char* username, const char* new_password) {
  if (!credentials_loaded) {
    logError("[AUTH] Credentials not loaded - call authInit() first");
    return false;
  }

  if (!username || !new_password) {
    logError("[AUTH] NULL username or password");
    return false;
  }

  // Verify username
  if (strcmp(username, current_username) != 0) {
    logError("[AUTH] Cannot set password for unknown user: %s", username);
    return false;
  }

  // Validate password strength
  if (!authValidatePasswordStrength(new_password)) {
    return false;
  }

  // Create hash
  createPasswordHash(new_password, stored_password_hash, sizeof(stored_password_hash));

  // Yield to prevent watchdog timeout during NVS write
  yield();

  // Save to NVS
  Preferences prefs;
  prefs.begin("auth", false);
  prefs.putString("password", stored_password_hash);
  prefs.end();

  // Yield after NVS write
  yield();

  logInfo("[AUTH] Password updated successfully for user: %s", username);
  password_change_required = false;

  return true;
}

bool authIsPasswordChangeRequired() {
  return password_change_required;
}

void authGetUsername(char* output, size_t length) {
  if (output && length > 0) {
    SAFE_STRCPY(output, current_username, length);
  }
}

bool authVerifyHTTPBasicAuth(const char* auth_header) {
  if (!auth_header) {
    return false;
  }

  // Check for "Basic " prefix
  if (strncmp(auth_header, "Basic ", 6) != 0) {
    return false;
  }

  const char* credentials_b64 = auth_header + 6;  // Skip "Basic "

  // Base64 decode
  const char* b64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char decoded[128];
  memset(decoded, 0, sizeof(decoded));
  int out_idx = 0;
  uint32_t buf = 0;
  int buf_bits = 0;

  for (const char* p = credentials_b64; *p && out_idx < (int)sizeof(decoded) - 1; p++) {
    char c = *p;
    if (c == '=' || c == '\r' || c == '\n' || c == ' ') break;

    const char* pos = strchr(b64_chars, c);
    if (!pos) continue;

    buf = (buf << 6) | (pos - b64_chars);
    buf_bits += 6;

    if (buf_bits >= 8) {
      decoded[out_idx++] = (buf >> (buf_bits - 8)) & 0xFF;
      buf_bits -= 8;
    }
  }
  decoded[out_idx] = '\0';

  // Split username:password
  char* colon = strchr(decoded, ':');
  if (!colon) {
    logWarning("[AUTH] Invalid Basic Auth format - missing colon");
    return false;
  }

  *colon = '\0';
  const char* username = decoded;
  const char* password = colon + 1;

  // Verify credentials
  return authVerifyCredentials(username, password);
}

// PHASE 5.10: Rate limiting implementation
bool authCheckRateLimit(const char* ip_address) {
  if (!ip_address) {
    return true;  // Allow if no IP provided (shouldn't happen)
  }

  uint32_t now = millis();

  // Find entry for this IP
  for (int i = 0; i < rate_limit_entries; i++) {
    if (strcmp(rate_limit_table[i].ip_address, ip_address) == 0) {
      // Check if window has expired
      if (now - rate_limit_table[i].first_attempt_time > AUTH_RATE_LIMIT_WINDOW_MS) {
        // Window expired - reset
        rate_limit_table[i].attempt_count = 0;
        rate_limit_table[i].first_attempt_time = now;
        return true;
      }

      // Check if rate limit exceeded
      if (rate_limit_table[i].attempt_count >= AUTH_RATE_LIMIT_MAX_ATTEMPTS) {
        logWarning("[AUTH] Rate limit exceeded for IP: %s (%lu attempts in %lu ms)",
                   ip_address,
                   (unsigned long)rate_limit_table[i].attempt_count,
                   (unsigned long)(now - rate_limit_table[i].first_attempt_time));
        return false;
      }

      return true;
    }
  }

  // IP not found - allowed
  return true;
}

void authRecordFailedAttempt(const char* ip_address) {
  if (!ip_address) {
    return;
  }

  uint32_t now = millis();

  // Find existing entry or add new one
  for (int i = 0; i < rate_limit_entries; i++) {
    if (strcmp(rate_limit_table[i].ip_address, ip_address) == 0) {
      // Check if window expired
      if (now - rate_limit_table[i].first_attempt_time > AUTH_RATE_LIMIT_WINDOW_MS) {
        // Reset window
        rate_limit_table[i].attempt_count = 1;
        rate_limit_table[i].first_attempt_time = now;
        rate_limit_table[i].last_attempt_time = now;
      } else {
        // Increment count
        rate_limit_table[i].attempt_count++;
        rate_limit_table[i].last_attempt_time = now;

        if (rate_limit_table[i].attempt_count == AUTH_RATE_LIMIT_MAX_ATTEMPTS) {
          logWarning("[AUTH] IP %s reached rate limit (%d failed attempts in %lu ms)",
                     ip_address, AUTH_RATE_LIMIT_MAX_ATTEMPTS,
                     (unsigned long)(now - rate_limit_table[i].first_attempt_time));
        }
      }
      return;
    }
  }

  // Not found - add new entry
  if (rate_limit_entries < AUTH_RATE_LIMIT_MAX_IPS) {
    SAFE_STRCPY(rate_limit_table[rate_limit_entries].ip_address, ip_address, 16);
    rate_limit_table[rate_limit_entries].attempt_count = 1;
    rate_limit_table[rate_limit_entries].first_attempt_time = now;
    rate_limit_table[rate_limit_entries].last_attempt_time = now;
    rate_limit_entries++;
  } else {
    // Table full - evict oldest entry (LRU)
    int oldest_idx = 0;
    uint32_t oldest_time = rate_limit_table[0].last_attempt_time;

    for (int i = 1; i < AUTH_RATE_LIMIT_MAX_IPS; i++) {
      if (rate_limit_table[i].last_attempt_time < oldest_time) {
        oldest_time = rate_limit_table[i].last_attempt_time;
        oldest_idx = i;
      }
    }

    // Replace oldest entry
    strncpy(rate_limit_table[oldest_idx].ip_address, ip_address, 15);
    rate_limit_table[oldest_idx].ip_address[15] = '\0';
    rate_limit_table[oldest_idx].attempt_count = 1;
    rate_limit_table[oldest_idx].first_attempt_time = now;
    rate_limit_table[oldest_idx].last_attempt_time = now;

    logWarning("[AUTH] Rate limit table full - evicted entry for new IP: %s", ip_address);
  }
}

void authClearRateLimit(const char* ip_address) {
  if (!ip_address) {
    return;
  }

  // Find and clear entry for this IP
  for (int i = 0; i < rate_limit_entries; i++) {
    if (strcmp(rate_limit_table[i].ip_address, ip_address) == 0) {
      // Clear entry by shifting remaining entries
      for (int j = i; j < rate_limit_entries - 1; j++) {
        rate_limit_table[j] = rate_limit_table[j + 1];
      }
      rate_limit_entries--;
      logInfo("[AUTH] Cleared rate limit for IP: %s", ip_address);
      return;
    }
  }
}

// SECURITY: Unified Password Management Command
// Usage: passwd [web|ota] <new_password>
void cmd_passwd(int argc, char** argv) {
  if (argc < 3) {
    logPrintln("\n[AUTH] === Password Management ===");
    CLI_USAGE("passwd", "[web|ota] <new_password>");
    logPrintln("Options:");
    logPrintln("  web - Set Web UI password");
    logPrintln("  ota - Set OTA update password");
    logPrintln("\nRequirements:");
    logPrintln("  - Minimum 8 characters");
    logPrintln("  - Mixed case, numbers, symbols recommended");
    return;
  }

  const char* type = argv[1];
  const char* new_pass = argv[2];

  // Common Length Check
  if (strlen(new_pass) < 8) {
    logError("[AUTH] Password must be at least 8 characters");
    return;
  }

  // Handle "web"
  if (strcasecmp(type, "web") == 0) {
    yield(); // Feed watchdog
    if (authSetPassword("admin", new_pass)) {
      yield();
      logPrintln("[AUTH] [OK] Web UI password updated successfully.");
      logPrintln("[AUTH] [OK] New password active immediately.");
    } else {
      logPrintln("[AUTH] [ERR] Update failed (complexity check).");
    }
    return;
  }

  // Handle "ota"
  if (strcasecmp(type, "ota") == 0) {
    configSetString(KEY_OTA_PASSWORD, new_pass);
    configSetInt(KEY_OTA_PW_CHANGED, 1);
    configUnifiedSave();
    
    logInfo("[OTA] [OK] OTA password updated successfully");
    logWarning("[OTA] Reboot required for changes to take effect");
    return;
  }

  logError("[AUTH] Unknown target '%s' (use 'web' or 'ota')", type);
}

bool authTestPassword(const char* password) {
  if (!credentials_loaded) {
    logError("[AUTH] Credentials not loaded");
    return false;
  }
  
  // Test against stored hash
  if (strncmp(stored_password_hash, "$sha256$", 8) != 0) {
    // Plain text comparison (legacy)
    return strcmp(password, stored_password_hash) == 0;
  }
  
  // Parse and verify hash
  const char* salt_hex = stored_password_hash + 8;
  const char* hash_hex = strchr(salt_hex, '$');
  if (!hash_hex) {
    return false;
  }
  hash_hex++;
  
  // Extract salt
  char salt_hex_str[AUTH_SALT_BYTES * 2 + 1];
  SAFE_STRCPY(salt_hex_str, salt_hex, sizeof(salt_hex_str));
  
  uint8_t salt[AUTH_SALT_BYTES];
  hexToBin(salt_hex_str, salt, sizeof(salt));
  
  // Extract expected hash
  uint8_t expected_hash[AUTH_HASH_BYTES];
  hexToBin(hash_hex, expected_hash, sizeof(expected_hash));
  
  // Compute actual hash
  uint8_t actual_hash[AUTH_HASH_BYTES];
  hashPassword(password, salt, actual_hash);
  
  // Compare
  uint8_t diff = 0;
  for (size_t i = 0; i < AUTH_HASH_BYTES; i++) {
    diff |= expected_hash[i] ^ actual_hash[i];
  }
  
  return diff == 0;
}

void authPrintDiagnostics(void) {
  serialLoggerLock();
  logPrintln("\n[AUTH] === Authentication Diagnostics ===");
  logPrintf("Username:          %s\n", current_username);
  logPrintf("Credentials Loaded: %s\n", credentials_loaded ? "YES" : "NO");
  logPrintf("Password Change:   %s\n", password_change_required ? "REQUIRED" : "Not required");
  logPrintf("Hash Format:       %s\n", 
    strncmp(stored_password_hash, "$sha256$", 8) == 0 ? "SHA-256 (secure)" : "PLAIN TEXT (insecure!)");
  logPrintf("Hash Length:       %d chars\n", strlen(stored_password_hash));
  
  // Show partial hash for debugging (safe - only first few chars)
  if (strlen(stored_password_hash) > 20) {
    logPrintf("Hash Preview:      %.20s...\n", stored_password_hash);
  }
  
  logPrintf("Rate Limit IPs:    %d/%d\n", rate_limit_entries, AUTH_RATE_LIMIT_MAX_IPS);
  logPrintln("");
  serialLoggerUnlock();
}

void cmd_auth(int argc, char** argv) {
  if (argc < 2) {
    serialLoggerLock();
    logPrintln("\n[AUTH] === Authentication Management ===");
    CLI_USAGE("auth", "[diag|test|reload|clear_limits]");
    CLI_HELP_LINE("diag", "Show auth diagnostics");
    CLI_HELP_LINE("test <pass>", "Test password verification");
    CLI_HELP_LINE("reload", "Reload credentials from NVS");
    CLI_HELP_LINE("clear_limits", "Clear all rate limits");
    serialLoggerUnlock();
    return;
  }
  
  if (strcasecmp(argv[1], "diag") == 0) {
    authPrintDiagnostics();
  } else if (strcasecmp(argv[1], "test") == 0) {
    if (argc < 3) {
      logPrintln("[AUTH] Usage: auth test <password>");
      return;
    }
    bool result = authTestPassword(argv[2]);
    logPrintf("[AUTH] Password test: %s\n", result ? "MATCH" : "NO MATCH");
    if (!result) {
      logPrintln("[AUTH] Hint: Password may have been entered incorrectly");
      logPrintln("[AUTH] Hint: Clear browser cache or try incognito mode");
    }
  } else if (strcasecmp(argv[1], "reload") == 0) {
    logPrintln("[AUTH] Reloading credentials from NVS...");
    Preferences prefs;
    prefs.begin("auth", true);
    String pw_hash = prefs.getString("password", "");
    prefs.end();
    
    if (pw_hash.length() > 0) {
      SAFE_STRCPY(stored_password_hash, pw_hash.c_str(), sizeof(stored_password_hash));
      logInfo("[AUTH] Credentials reloaded successfully");
      authPrintDiagnostics();
    } else {
      logError("[AUTH] No credentials found in NVS");
    }
  } else if (strcasecmp(argv[1], "clear_limits") == 0) {
    rate_limit_entries = 0;
    logInfo("[AUTH] All rate limits cleared");
  } else {
    logWarning("[AUTH] Unknown command: %s", argv[1]);
  }
}
