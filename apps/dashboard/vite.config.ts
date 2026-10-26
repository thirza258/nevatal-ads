import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [react()],
  server: {
    port: 5180,
    host: '0.0.0.0',
    proxy: {
      '/api': {
        target: 'http://localhost:8088',
        changeOrigin: true,
      },
      '/delivery': {
        target: 'http://localhost:8088',
        changeOrigin: true,
      },
      '/sdk': {
        target: 'http://localhost:8088',
        changeOrigin: true,
      },
      '/click': {
        target: 'http://localhost:8088',
        changeOrigin: true,
      },
      '/r': {
        target: 'http://localhost:8088',
        changeOrigin: true,
      },
      '/healthz': {
        target: 'http://localhost:8088',
        changeOrigin: true,
      },
      '/sitemap.xml': {
        target: 'http://localhost:8088',
        changeOrigin: true,
      },
      '/robots.txt': {
        target: 'http://localhost:8088',
        changeOrigin: true,
      },
    },
  },
});
