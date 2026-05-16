import { defineConfig } from 'vite'
import { babel } from '@rollup/plugin-babel'
import tailwindcss from '@tailwindcss/vite'

const babelPlugin = babel({
  babelHelpers: 'bundled',
  extensions: ['.js', '.jsx', '.ts', '.tsx'],
  exclude: 'node_modules/**',
  configFile: false,
  plugins: [
    "@babel/plugin-syntax-jsx",
    ["@opentf/web/compiler"]
  ]
})

babelPlugin.enforce = 'pre'

export default defineConfig({
  esbuild: {
    jsx: 'preserve'
  },
  optimizeDeps: {
    rolldownOptions: {
      transform: {
        jsx: 'preserve'
      }
    }
  },
  plugins: [
    tailwindcss(),
    babelPlugin
  ]
})