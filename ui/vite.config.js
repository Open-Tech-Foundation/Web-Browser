import { resolve } from 'path';
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import tailwindcss from '@tailwindcss/vite';

export default defineConfig({
  plugins: [react(), tailwindcss()],
  build: {
    outDir: '../build/Release/ui',
    emptyOutDir: true,
    rollupOptions: {
      input: {
        main: resolve(__dirname, 'index.html'),
        settings: resolve(__dirname, 'settings.html'),
        newtab: resolve(__dirname, 'newtab.html'),
        history: resolve(__dirname, 'history.html'),
        bookmarks: resolve(__dirname, 'bookmarks.html'),
        findbar: resolve(__dirname, 'findbar.html'),
        zoombar: resolve(__dirname, 'zoombar.html'),
        downloadsbar: resolve(__dirname, 'downloadsbar.html'),
        downloads: resolve(__dirname, 'downloads.html'),
        appmenu: resolve(__dirname, 'appmenu.html'),
        security: resolve(__dirname, 'security.html'),
        'insecure-blocked': resolve(__dirname, 'insecure-blocked.html'),
        certificate: resolve(__dirname, 'certificate.html')
      }
    }
  }
});
