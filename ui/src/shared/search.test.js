import test from 'node:test';
import assert from 'node:assert/strict';

import { looksLikeDirectUrl, resolveUrl } from './search.js';

test('resolveUrl preserves explicit schemes', () => {
  assert.equal(resolveUrl('https://example.com', 'google'), 'https://example.com');
  assert.equal(resolveUrl('ftp://mirror.example.com', 'google'), 'ftp://mirror.example.com');
});

test('resolveUrl adds https for direct hosts', () => {
  assert.equal(resolveUrl('example.com', 'google'), 'https://example.com');
  assert.equal(resolveUrl('localhost:3000/path', 'google'), 'https://localhost:3000/path');
  assert.equal(resolveUrl('127.0.0.1:8080', 'google'), 'https://127.0.0.1:8080');
});

test('resolveUrl trims input and falls back to search', () => {
  assert.equal(
    resolveUrl('  hello world  ', 'bing'),
    'https://www.bing.com/search?q=hello%20world'
  );
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
