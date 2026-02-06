/**
 * BISSO E350 Controller - Service Worker
 * Provides basic offline caching for core assets.
 */

const CACHE_NAME = 'bisso-pwa-v1';
const ASSETS = [
    '/',
    '/index.html',
    '/core.js',
    '/manifest.json',
    '/pwa-icon.png',
    '/posipro_logo.svg'
];

self.addEventListener('install', (event) => {
    event.waitUntil(
        caches.open(CACHE_NAME).then((cache) => {
            return cache.addAll(ASSETS);
        })
    );
});

self.addEventListener('fetch', (event) => {
    event.respondWith(
        caches.match(event.request).then((response) => {
            return response || fetch(event.request);
        })
    );
});
