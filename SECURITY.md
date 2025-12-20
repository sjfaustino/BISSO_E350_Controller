# Security Policy

## Overview

The BISSO E350 Controller firmware implements several security measures to protect against unauthorized access and ensure safe operation in industrial environments.

## Credential Management

### Storage Method

**All credentials are stored in NVS (Non-Volatile Storage)**, not in source code:

- ✅ WiFi SSID and password
- ✅ Web server username and password
- ✅ OTA (Over-The-Air) update password

**Benefits:**
- Credentials never committed to version control
- Can be changed at runtime without recompiling
- Secure storage in ESP32 flash memory
- No risk of credential exposure in public repositories

### Default Credentials

The system ships with default credentials that **MUST** be changed before deployment:

| Service | Default Username | Default Password | Change Command |
|---------|------------------|------------------|----------------|
| Web UI  | `admin` | `password` | Web UI or `web_setpass` |
| OTA Updates | N/A | `bisso-ota` | `ota_setpass <new_password>` |
| WiFi | N/A | (set via WiFiManager) | Web UI or `wifi connect` |

### Password Requirements

- **Web UI password**: Minimum 4 characters
- **OTA password**: Minimum 8 characters (recommended 12+)
- **WiFi password**: Per WPA2 requirements (8+ characters)

### Password Change Enforcement

The system tracks whether default passwords have been changed:

- On first boot with default web password, a warning is displayed
- On first boot with default OTA password, a warning is displayed
- Administrators should change all default passwords immediately after deployment

## Changing Credentials

### Via CLI (Serial/Telnet)

```bash
# Change web server password
web_setpass <new_password>

# Change OTA password (requires reboot)
ota_setpass <new_password>
reboot

# Connect to WiFi
wifi connect <ssid> <password>
```

### Via Web UI

1. Navigate to Settings > Security
2. Enter current and new passwords
3. Submit and log in with new credentials

## Security Best Practices

### For Developers

1. **Never hardcode credentials** in source files
   - Use NVS storage via `config_unified.h` API
   - Default values only for initial setup

2. **Secrets files are gitignored**
   - `.gitignore` blocks `secrets.h`, `credentials.h`, `*_secrets.h`
   - If creating temporary credential files, use these names

3. **Avoid logging sensitive data**
   - Never log passwords in plain text
   - Redact credentials in diagnostic output

4. **Code review checklist**
   - Search for hardcoded passwords: `git grep -i "password.*=.*\""`
   - Check for credential exposure in error messages
   - Verify NVS usage for new credential types

### For System Administrators

1. **Change all default passwords immediately**
   ```bash
   ota_setpass <strong_password>
   web_setpass <strong_password>
   ```

2. **Use strong passwords**
   - Minimum 12 characters
   - Mix of uppercase, lowercase, numbers, symbols
   - Avoid dictionary words
   - Use a password manager

3. **Restrict network access**
   - Place controller on isolated VLAN if possible
   - Use firewall rules to limit access
   - Disable Telnet if not needed (port 23)

4. **Regular updates**
   - Apply firmware updates via OTA when available
   - Monitor security advisories
   - Keep web interface credentials confidential

5. **Physical security**
   - Secure physical access to the controller
   - Serial console access = full system access
   - Consider disabling USB/serial in production

## Security Features

### Authentication

- **Web UI**: HTTP Basic Authentication (username + password)
- **OTA Updates**: Password-protected firmware uploads
- **API Endpoints**: All protected by web authentication
- **File Upload**: Restricted file extensions (.nc, .gcode, .txt only)

### Rate Limiting

- API endpoints have rate limiting to prevent brute force
- Configurable limits per endpoint type
- Automatic lockout on excessive failed attempts

### Session Management

- WebSocket connections require valid authentication
- Telnet sessions are single-client only
- Automatic timeout on idle connections

## Known Limitations

1. **HTTP (not HTTPS)**
   - ESP32 HTTPS support adds significant overhead
   - Use VPN or isolated network for sensitive deployments
   - HTTPS may be added in future versions

2. **Telnet (unencrypted)**
   - Port 23 telnet is plaintext
   - Credentials transmitted in clear text over network
   - Recommend SSH/VPN for remote access
   - Consider disabling telnet in production builds

3. **No user roles**
   - Single admin account only
   - All authenticated users have full access
   - Role-based access control planned for future release

## Reporting Security Issues

If you discover a security vulnerability:

1. **DO NOT** create a public GitHub issue
2. Email security details to: [CONTACT EMAIL]
3. Include:
   - Description of the vulnerability
   - Steps to reproduce
   - Potential impact assessment
   - Suggested fix (if available)

We will acknowledge receipt within 48 hours and provide a timeline for fix deployment.

## Security Audit History

| Date | Auditor | Findings | Status |
|------|---------|----------|--------|
| 2025-01-XX | Gemini AI | Hardcoded OTA password | ✅ Fixed |
| 2025-01-XX | Gemini AI | Memory management (heap fragmentation) | ✅ Fixed |
| 2025-01-XX | Gemini AI | Sensor validation (NaN checks) | ✅ Fixed |

## Compliance

This firmware is designed for industrial control applications. Ensure compliance with:

- Local electrical safety regulations
- Industrial control system security standards (IEC 62443)
- OSHA safety requirements for machine control
- Local data protection regulations (if logging operational data)

---

**Last Updated**: 2025-01-XX
**Version**: 1.0
**Maintained by**: BISSO Development Team
