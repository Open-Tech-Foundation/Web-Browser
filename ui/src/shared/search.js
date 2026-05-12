const BROWSER_SCHEME = {
  SETTINGS: 'browser://settings',
};

const engineMap = {
  google: 'https://www.google.com/search?q=',
  bing: 'https://www.bing.com/search?q=',
  yahoo: 'https://search.yahoo.com/search?p=',
  duckduckgo: 'https://duckduckgo.com/?q=',
  baidu: 'https://www.baidu.com/s?wd=',
  yandex: 'https://yandex.com/search/?text=',
  ecosia: 'https://www.ecosia.org/search?q=',
  naver: 'https://search.naver.com/search.naver?query=',
  startpage: 'https://www.startpage.com/search?q='
};

export function buildSearchUrl(engine, query) {
  const baseUrl = engineMap[engine];
  if (!baseUrl) return BROWSER_SCHEME.SETTINGS;
  return baseUrl + encodeURIComponent(query);
}

const explicitSchemePattern = /^[a-zA-Z][a-zA-Z\d+.-]*:\/\//;
const localhostPattern = /^localhost(?::\d{1,5})?(?:[/?#]|$)/i;
const ipv4Pattern = /^(?:\d{1,3}\.){3}\d{1,3}(?::\d{1,5})?(?:[/?#]|$)/;
const domainPattern = /^(?=.{1,253}$)(?!-)(?:[a-zA-Z\d-]{1,63}\.)+[a-zA-Z]{2,63}(?::\d{1,5})?(?:[/?#]|$)/;

export function looksLikeDirectUrl(input) {
  return localhostPattern.test(input) || ipv4Pattern.test(input) || domainPattern.test(input);
}

export function resolveUrl(input, searchEngine) {
  const trimmed = input.trim();

  // browser:// scheme (internal pages)
  if (trimmed.startsWith('browser://')) return trimmed;

  // Protocol-relative URLs
  if (trimmed.startsWith('//')) return trimmed;

  // Explicit scheme URLs (http://, https://, ftp://, etc.)
  if (explicitSchemePattern.test(trimmed)) return trimmed;

  if (!trimmed.includes(' ') && looksLikeDirectUrl(trimmed)) {
    return `https://${trimmed}`;
  }

  return buildSearchUrl(searchEngine, trimmed);
}
