/**
 * BISSO E350 Controller - Service Worker
 * Provides basic offline caching for core assets.
 */

const CACHE_NAME = 'bisso-pwa-v2';
const ASSETS = [
    '/',
    '/index.html',
    '/core.js',
    '/css/bundle.css',
    '/manifest.json',
    '/posipro_logo.svg'
];

self.addEventListener('install', (event) => {
    event.waitUntil(
        caches.open(CACHE_NAME).then((cache) => {
            return cache.addAll(ASSETS);
        })
    );
    self.skipWaiting();
});

self.addEventListener('activate', (event) => {
    event.waitUntil(
        caches.keys().then((keys) => {
            return Promise.all(
                keys.filter((key) => key !== CACHE_NAME)
                    .map((key) => caches.delete(key))
            );
        })
    );
    self.clients.claim();
});

self.addEventListener('fetch', (event) => {
    event.respondWith(
        fetch(event.request).catch(() => {
            return caches.match(event.request);
        })
    );
});
