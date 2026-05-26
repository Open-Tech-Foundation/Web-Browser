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

function encodeQuery(query) {
  return encodeURIComponent(query).replace(/%20/g, '+');
}

export function buildSearchUrl(engine, query, customEngines) {
  const baseUrl = engineMap[engine];
  if (baseUrl) {
    return baseUrl + encodeQuery(query);
  }
  if (Array.isArray(customEngines)) {
    const custom = customEngines.find(e => e.id === engine);
    if (custom && custom.url) {
      if (custom.url.includes('%s')) {
        return custom.url.replace('%s', encodeQuery(query));
      }
      return custom.url + encodeQuery(query);
    }
  }
  return BROWSER_SCHEME.SETTINGS;
}

const explicitSchemePattern = /^[a-zA-Z][a-zA-Z\d+.-]*:\/\//;
const localhostPattern = /^localhost(?::\d{1,5})?(?:[/?#]|$)/i;
const ipv4Pattern = /^(?:\d{1,3}\.){3}\d{1,3}(?::\d{1,5})?(?:[/?#]|$)/;
const domainPattern = /^(?=.{1,253}$)(?!-)(?:[a-zA-Z\d-]{1,63}\.)+[a-zA-Z]{2,63}(?::\d{1,5})?(?:[/?#]|$)/;

export function looksLikeDirectUrl(input) {
  return localhostPattern.test(input) || ipv4Pattern.test(input) || domainPattern.test(input);
}

export function resolveUrl(input, searchEngine, customEngines) {
  const trimmed = input.trim();

  if (trimmed.startsWith('browser://')) return trimmed;

  if (trimmed.startsWith('//')) return trimmed;

  if (explicitSchemePattern.test(trimmed)) {
    const scheme = trimmed.split('://')[0].toLowerCase();
    const dangerousSchemes = ['javascript', 'data', 'file', 'vbscript', 'blob'];
    if (dangerousSchemes.includes(scheme)) {
      return buildSearchUrl(searchEngine, trimmed, customEngines);
    }
    return trimmed;
  }

  if (!trimmed.includes(' ') && localhostPattern.test(trimmed)) {
    return `http://${trimmed}`;
  }

  if (!trimmed.includes(' ') && ipv4Pattern.test(trimmed)) {
    return `http://${trimmed}`;
  }

  if (!trimmed.includes(' ') && looksLikeDirectUrl(trimmed)) {
    return `https://${trimmed}`;
  }

  return buildSearchUrl(searchEngine, trimmed, customEngines);
}
