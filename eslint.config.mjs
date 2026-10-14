// ESLint (flat config) for the browser-side JS in platform/web/. Emscripten provides the globals below.
import globals from 'globals';
import js from '@eslint/js';

export default [
  { ignores: ['gen/**', 'build/**', 'analysis/**', 'third_party/**', 'node_modules/**', 'logs/**'] },
  js.configs.recommended,
  {
    files: ['platform/web/**/*.js'],
    languageOptions: {
      ecmaVersion: 2022,
      sourceType: 'script',
      globals: {
        ...globals.browser,
        ...globals.worker,
        Module: 'writable', FS: 'readonly', HEAPU8: 'readonly',
        addRunDependency: 'readonly', removeRunDependency: 'readonly', ENV: 'readonly',
        AudioWorkletProcessor: 'readonly', registerProcessor: 'readonly',
      },
    },
    rules: {
      'max-lines': ['error', { max: 300, skipBlankLines: true, skipComments: true }],
      'max-lines-per-function': ['error', { max: 80, skipBlankLines: true, skipComments: true }],
      complexity: ['error', 20],
      'no-var': 'error',
      'prefer-const': 'error',
      eqeqeq: ['error', 'always'],
    },
  },
];
