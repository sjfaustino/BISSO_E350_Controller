/**
 * @file shared/websocket.js
 * @brief Shared WebSocket connection for all pages
 * @details Single connection reused across all modules
 */

class SharedWebSocket {
    static ws = null;
    static isConnected = false;
    static reconnectAttempts = 0;
    static maxReconnectAttempts = 5;
    static reconnectDelay = 3000;
    static listeners = [];

    static connect() {
        if (this.ws) return this.ws;

        try {
            const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
            const wsUrl = `${protocol}//${window.location.hostname}:${window.location.port}/ws`;

            this.ws = new WebSocket(wsUrl);

            this.ws.onopen = () => {
                this.isConnected = true;
                this.reconnectAttempts = 0;
                console.log('[WS] Connected');
                this.broadcast('ws-connected');
            };

            this.ws.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    this.broadcast('telemetry', data);
                } catch (e) {
                    console.error('[WS] Parse error:', e);
                }
            };

            this.ws.onerror = (error) => {
                console.error('[WS] Error:', error);
                this.broadcast('ws-error', error);
            };

            this.ws.onclose = () => {
                this.isConnected = false;
                console.log('[WS] Disconnected');
                this.broadcast('ws-disconnected');
                this.attemptReconnect();
            };

            return this.ws;
        } catch (e) {
            console.error('[WS] Connection failed:', e);
            this.attemptReconnect();
            return null;
        }
    }

    static attemptReconnect() {
        if (this.reconnectAttempts >= this.maxReconnectAttempts) {
            console.error('[WS] Max reconnection attempts reached');
            this.broadcast('ws-failed');
            return;
        }

        this.reconnectAttempts++;
        console.log(`[WS] Reconnecting in ${this.reconnectDelay}ms (attempt ${this.reconnectAttempts}/${this.maxReconnectAttempts})`);

        setTimeout(() => this.connect(), this.reconnectDelay);
    }

    static send(message) {
        if (this.ws && this.isConnected) {
            this.ws.send(JSON.stringify(message));
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
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
    }
}

// Auto-connect on load
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => SharedWebSocket.connect());
} else {
    SharedWebSocket.connect();
}
