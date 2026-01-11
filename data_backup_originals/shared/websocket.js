/**
 * @file shared/websocket.js
 * @brief Shared WebSocket connection for all pages
 * @details Single connection reused across all modules with exponential backoff reconnection
 */

class SharedWebSocket {
    static ws = null;
    static isConnected = false;
    static reconnectAttempts = 0;
    static maxReconnectAttempts = 10;
    static baseReconnectDelay = 1000;  // Start at 1 second
    static maxReconnectDelay = 30000;  // Cap at 30 seconds
    static currentReconnectDelay = 1000;
    static reconnectTimer = null;
    static listeners = [];
    static packetsSent = 0;
    static packetsReceived = 0;
    static dataReceivedBytes = 0;
    static latency = 0;
    static lastPingTime = 0;
    static pingInterval = null;

    static connect() {
        // Clear any pending reconnect timer
        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }

        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            return this.ws;
        }

        // Close existing connection if in bad state
        if (this.ws) {
            try { this.ws.close(); } catch (e) { }
            this.ws = null;
        }

        try {
            const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
            const wsUrl = `${protocol}//${window.location.hostname}:${window.location.port}/ws`;

            console.log('[WS] Connecting to', wsUrl);
            this.ws = new WebSocket(wsUrl);

            this.ws.onopen = () => {
                this.isConnected = true;
                this.reconnectAttempts = 0;
                this.currentReconnectDelay = this.baseReconnectDelay; // Reset delay on success
                console.log('[WS] Connected');
                this.broadcast('ws-connected');

                // Show reconnection success if we had been reconnecting
                if (typeof AlertManager !== 'undefined' && this.reconnectAttempts > 0) {
                    AlertManager.add('WebSocket reconnected', 'success', 2000);
                }

                // Start pinging for latency
                this.startLatencyTracking();
            };

            this.ws.onmessage = (event) => {
                this.packetsReceived++;
                if (event.data) this.dataReceivedBytes += event.data.length;
                try {
                    const data = JSON.parse(event.data);

                    // Handle pong response
                    if (data.type === 'pong') {
                        if (this.lastPingTime > 0) {
                            this.latency = Date.now() - this.lastPingTime;
                        }
                        return;
                    }

                    this.broadcast('telemetry', data);
                } catch (e) {
                    console.error('[WS] Parse error:', e);
                }
            };

            this.ws.onerror = (error) => {
                console.error('[WS] Error:', error);
                this.broadcast('ws-error', error);
            };

            this.ws.onclose = (event) => {
                this.isConnected = false;
                console.log('[WS] Disconnected (code:', event.code, ')');
                this.broadcast('ws-disconnected');
                this.scheduleReconnect();
            };

            return this.ws;
        } catch (e) {
            console.error('[WS] Connection failed:', e);
            this.scheduleReconnect();
            return null;
        }
    }

    /**
     * Schedule reconnection with exponential backoff
     * Delay doubles each attempt: 1s, 2s, 4s, 8s, 16s, 30s (capped)
     */
    static scheduleReconnect() {
        if (this.reconnectAttempts >= this.maxReconnectAttempts) {
            console.error('[WS] Max reconnection attempts reached');
            this.broadcast('ws-failed');

            // Show persistent error to user
            if (typeof AlertManager !== 'undefined') {
                AlertManager.add('WebSocket connection failed. Refresh page to retry.', 'error', 10000);
            }
            return;
        }

        this.reconnectAttempts++;

        // Calculate exponential backoff delay
        this.currentReconnectDelay = Math.min(
            this.baseReconnectDelay * Math.pow(2, this.reconnectAttempts - 1),
            this.maxReconnectDelay
        );

        console.log(`[WS] Reconnecting in ${this.currentReconnectDelay}ms (attempt ${this.reconnectAttempts}/${this.maxReconnectAttempts})`);

        // Update UI if available
        this.broadcast('ws-reconnecting', {
            attempt: this.reconnectAttempts,
            maxAttempts: this.maxReconnectAttempts,
            delay: this.currentReconnectDelay
        });

        this.reconnectTimer = setTimeout(() => {
            this.reconnectTimer = null;
            this.connect();
        }, this.currentReconnectDelay);
    }

    /**
     * Force immediate reconnection (resets backoff)
     */
    static forceReconnect() {
        console.log('[WS] Force reconnect requested');
        this.reconnectAttempts = 0;
        this.currentReconnectDelay = this.baseReconnectDelay;

        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }

        if (this.ws) {
            try { this.ws.close(); } catch (e) { }
            this.ws = null;
        }

        this.connect();
    }

    static send(message) {
        if (this.ws && this.isConnected && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify(message));
            this.packetsSent++;
            return true;
        }
        console.warn('[WS] Not connected, message not sent');
        return false;
    }

    static subscribe(callback) {
        this.listeners.push(callback);
        return () => {
            this.listeners = this.listeners.filter(l => l !== callback);
        };
    }

    static broadcast(event, data = null) {
        window.dispatchEvent(new CustomEvent(event, { detail: data }));
    }

    static disconnect() {
        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
        this.isConnected = false;
        this.stopLatencyTracking();
    }

    static startLatencyTracking() {
        this.stopLatencyTracking();
        this.pingInterval = setInterval(() => {
            if (this.isConnected && this.ws.readyState === WebSocket.OPEN) {
                this.ping();
            }
        }, 5000); // Check latency every 5 seconds
    }

    static ping() {
        if (this.isConnected && this.ws.readyState === WebSocket.OPEN) {
            this.lastPingTime = Date.now();
            this.ws.send(JSON.stringify({ type: 'ping' }));
        }
    }

    static stopLatencyTracking() {
        if (this.pingInterval) {
            clearInterval(this.pingInterval);
            this.pingInterval = null;
        }
    }

    /**
     * Get connection status for UI display
     */
    static getStatus() {
        return {
            connected: this.isConnected,
            reconnecting: this.reconnectTimer !== null,
            attempts: this.reconnectAttempts,
            maxAttempts: this.maxReconnectAttempts,
            nextRetryMs: this.currentReconnectDelay
        };
    }
}

// Auto-connect on load (skip for file:// protocol)
if (window.location.protocol !== 'file:') {
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', () => SharedWebSocket.connect());
    } else {
        SharedWebSocket.connect();
    }
}
