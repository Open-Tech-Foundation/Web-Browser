import { resolve } from 'path';
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import tailwindcss from '@tailwindcss/vite';

export default defineConfig({
  plugins: [react(), tailwindcss()],
  // Emit relative asset URLs (./assets/...) instead of root-absolute ones
  // (/assets/...). The packaged browser loads pages via file:// (e.g.
  // file:///opt/otf-browser/ui/index.html); a leading slash resolves to
  // the filesystem root and the bundled JS/CSS fails to load, leaving the
  // window empty. Relative paths resolve against the HTML's directory,
  // which works for both file:// and the dev server.
  base: './',
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
        bookmarkbar: resolve(__dirname, 'bookmarkbar.html'),
        imagepreview: resolve(__dirname, 'imagepreview.html'),
        downloads: resolve(__dirname, 'downloads.html'),
        appmenu: resolve(__dirname, 'appmenu.html'),
        security: resolve(__dirname, 'security.html'),
        'insecure-blocked': resolve(__dirname, 'insecure-blocked.html'),
        certificate: resolve(__dirname, 'certificate.html'),
        cleardata: resolve(__dirname, 'cleardata.html'),
        sitedata: resolve(__dirname, 'sitedata.html'),
        workspace: resolve(__dirname, 'workspace.html'),
        qr: resolve(__dirname, 'qr.html'),
        blockedpopup: resolve(__dirname, 'blockedpopup.html'),
        downloadrequest: resolve(__dirname, 'downloadrequest.html'),
        linkpreview: resolve(__dirname, 'linkpreview.html'),
        console: resolve(__dirname, 'console.html')
      }
    }
  }
});
