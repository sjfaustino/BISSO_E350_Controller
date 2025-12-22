# Web Interface Documentation

This directory contains architectural and implementation documentation for the BISSO E350 Controller web interface.

## Documents

### 1. [ARCHITECTURE.md](ARCHITECTURE.md)
**Purpose:** High-level architectural design of the web interface
- Technology stack (AsyncWebServer, SPIFFS, WebSocket)
- Component architecture (frontend routing, API design)
- Security model (authentication, authorization)
- Performance considerations

**Key Topics:**
- RESTful API endpoint design
- WebSocket real-time communication
- Single-Page Application (SPA) architecture
- Static file serving from SPIFFS

---

### 2. [IMPLEMENTATION_GUIDE.md](IMPLEMENTATION_GUIDE.md)
**Purpose:** Step-by-step implementation guide for web features
- How to add new API endpoints
- How to create new web pages
- How to integrate with backend subsystems
- Common patterns and best practices

**Key Topics:**
- Adding authenticated endpoints
- JSON API request/response patterns
- WebSocket message handling
- Frontend JavaScript module structure

---

### 3. [README.md](README.md)
**Purpose:** Overview and quick-start guide for web interface development
- Project structure overview
- Setup and development workflow
- Testing procedures
- Common issues and solutions

**Key Topics:**
- File organization (`data/` folder structure)
- SPIFFS filesystem upload
- Local development vs production builds
- Browser compatibility

---

### 4. [WEB_INTERFACE_MOCKUP.md](WEB_INTERFACE_MOCKUP.md)
**Purpose:** Visual mockups and UI/UX specifications
- Dashboard layout and components
- Motion control interface
- Diagnostic screens
- Settings pages

**Key Topics:**
- Page layouts (HTML structure)
- CSS styling patterns
- Responsive design breakpoints
- User interaction flows

---

## History

**Original Location:** `spiffs/` directory (filesystem source folder)

**Moved:** 2025-12-22 (PHASE 5.9)
- Reason: `spiffs/` renamed to `data/` per PlatformIO convention
- These documentation files don't belong in the web root (not served to users)
- Preserved in `docs/web/` for developer reference

**Total Documentation:** ~62 KB (1,906 lines of architectural knowledge)

---

## Related Documentation

- [../CURSOR_AI_SECURITY_ARCHITECTURE.md](../CURSOR_AI_SECURITY_ARCHITECTURE.md) - Web security improvements
- [../TESTING_CHECKLIST.md](../TESTING_CHECKLIST.md) - Web interface testing procedures
- [../../README.md](../../README.md) - Main project README (Section 2.3 Security)

---

## Quick Links

**For New Developers:**
1. Read [README.md](README.md) first for overview
2. Review [ARCHITECTURE.md](ARCHITECTURE.md) for design understanding
3. Use [IMPLEMENTATION_GUIDE.md](IMPLEMENTATION_GUIDE.md) when adding features
4. Refer to [WEB_INTERFACE_MOCKUP.md](WEB_INTERFACE_MOCKUP.md) for UI specs

**For Security Review:**
- [ARCHITECTURE.md](ARCHITECTURE.md) - Section on authentication
- [../CURSOR_AI_SECURITY_ARCHITECTURE.md](../CURSOR_AI_SECURITY_ARCHITECTURE.md) - Security improvements

**For API Integration:**
- [IMPLEMENTATION_GUIDE.md](IMPLEMENTATION_GUIDE.md) - API endpoint patterns
- [ARCHITECTURE.md](ARCHITECTURE.md) - RESTful API design

---

**Last Updated:** 2025-12-22
**Maintained by:** BISSO Development Team
