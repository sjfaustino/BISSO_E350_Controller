/**
 * @file auth_manager.h
 * @brief Secure authentication manager with SHA-256 password hashing
 * @details PHASE 5.10: Security hardening - replaces plain text passwords
 */

#ifndef AUTH_MANAGER_H
#define AUTH_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>  // For size_t

// Password hash format: $sha256$<salt_hex>$<hash_hex>
// Example: $sha256$a1b2c3d4e5f67890$1234567890abcdef...
#define AUTH_SALT_BYTES 16
#define AUTH_HASH_BYTES 32
#define AUTH_MAX_STORED_PW_LEN 128  // $sha256$ + 32 hex salt + $ + 64 hex hash

/**
 * @brief Initialize authentication manager
 * @details Loads credentials from NVS, generates random defaults if needed
 */
void authInit();

/**
 * @brief Verify username and password
 * @param username Username to check
 * @param password Plain text password to verify
 * @return true if credentials are valid, false otherwise
 */
bool authVerifyCredentials(const char* username, const char* password);

/**
 * @brief Set new password for user
 * @param username Username (currently only "admin" supported)
 * @param new_password Plain text password (will be hashed)
 * @return true if password was set, false if validation failed
 */
bool authSetPassword(const char* username, const char* new_password);

/**
 * @brief Check if default/weak password is in use
 * @return true if password needs to be changed
 */
bool authIsPasswordChangeRequired();

/**
 * @brief Generate random password for first boot
 * @param output Buffer to store generated password (min 13 bytes)
 * @param length Length of output buffer
 * @details Generates 12-character password: 3 lowercase + 3 uppercase + 3 digits + 3 symbols
 */
void authGenerateRandomPassword(char* output, size_t length);

/**
 * @brief Get current username for display
 * @param output Buffer to store username
 * @param length Length of output buffer
 */
void authGetUsername(char* output, size_t length);

/**
 * @brief Check if password meets security requirements
 * @param password Password to validate
 * @return true if password is strong enough
 */
bool authValidatePasswordStrength(const char* password);

/**
 * @brief Verify HTTP Basic Auth credentials from request header
 * @param auth_header Authorization header value (e.g., "Basic dXNlcjpwYXNz")
 * @return true if credentials are valid, false otherwise
 * @details Decodes Base64 credentials and verifies against SHA-256 hash
 */
bool authVerifyHTTPBasicAuth(const char* auth_header);

/**
 * @brief Check if IP address is rate limited for authentication attempts
 * @param ip_address IP address to check (e.g., "192.168.1.100")
 * @return true if IP is allowed to attempt auth, false if rate limited
 * @details Limits to 5 failed attempts per minute per IP
 */
bool authCheckRateLimit(const char* ip_address);

/**
 * @brief Record failed authentication attempt for IP address
 * @param ip_address IP address that failed auth
 * @details Increments failure count, used for rate limiting
 */
void authRecordFailedAttempt(const char* ip_address);

/**
 * @brief Clear rate limit for IP address (on successful auth)
 * @param ip_address IP address to clear
 */
void authClearRateLimit(const char* ip_address);

/**
 * @brief CLI Command handler for setting web password
 * @param argc Argument count
 * @param argv Argument array
 */
void cmd_web_setpass(int argc, char** argv);

#endif // AUTH_MANAGER_H
