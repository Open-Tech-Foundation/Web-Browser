import test from 'node:test';
import assert from 'node:assert/strict';

import { looksLikeDirectUrl, resolveUrl } from './search.js';

test('resolveUrl preserves explicit schemes', () => {
  assert.equal(resolveUrl('https://example.com', 'google'), 'https://example.com');
  assert.equal(resolveUrl('ftp://mirror.example.com', 'google'), 'ftp://mirror.example.com');
});

test('resolveUrl adds https for direct hosts', () => {
  assert.equal(resolveUrl('example.com', 'google'), 'https://example.com');
  assert.equal(resolveUrl('localhost:3000/path', 'google'), 'http://localhost:3000/path');
  assert.equal(resolveUrl('127.0.0.1:8080', 'google'), 'http://127.0.0.1:8080');
});

test('resolveUrl trims input and falls back to search', () => {
  assert.equal(
    resolveUrl('  hello world  ', 'bing'),
    'https://www.bing.com/search?q=hello+world'
  );
});

test('resolveUrl preserves direct host syntax in fallback mode', () => {
  assert.equal(resolveUrl('Transformers.js', 'google'), 'https://Transformers.js');
  assert.equal(resolveUrl('React.dev', 'google'), 'https://React.dev');
  assert.equal(resolveUrl('Google.com/search', 'google'), 'https://Google.com/search');
  assert.equal(resolveUrl('Google.com?query=test', 'google'), 'https://Google.com?query=test');
});

test('resolveUrl preserves internal and protocol-relative urls', () => {
  assert.equal(resolveUrl('browser://settings', 'google'), 'browser://settings');
  assert.equal(resolveUrl('//cdn.example.com/lib.js', 'google'), '//cdn.example.com/lib.js');
});

test('looksLikeDirectUrl accepts host formats only', () => {
  assert.equal(looksLikeDirectUrl('example.com'), true);
  assert.equal(looksLikeDirectUrl('localhost:3000'), true);
  assert.equal(looksLikeDirectUrl('search terms'), false);
});
