import React, { useEffect, useState } from 'react';
import { nativeRequest } from '../src/shared/nativeRequest';

const S = {
  wrapper: {
    padding: '8px',
    width: '100%',
    height: '100%',
    boxSizing: 'border-box',
    background: 'transparent',
  },
  panel: {
    display: 'flex',
    flexDirection: 'column',
    height: '100%',
    background: 'var(--bg, #fff)',
    border: '1px solid var(--sep)',
    borderRadius: 16,
    boxShadow: '0 14px 34px rgba(15,23,42,0.12)',
    overflow: 'hidden',
    fontFamily: "'Inter', system-ui, sans-serif",
  },
  header: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    padding: '12px 14px 10px',
    borderBottom: '1px solid var(--sep, #e2e8f0)',
    color: 'var(--fg, #0f172a)',
  },
  body: {
    flex: 1,
    display: 'flex',
    flexDirection: 'column',
    padding: '14px',
    background: 'var(--surface, #f8fafc)',
  },
  label: {
    fontSize: 11,
    fontWeight: 700,
    color: 'var(--muted, #64748b)',
    textTransform: 'uppercase',
    letterSpacing: '0.05em',
    marginBottom: 6,
  },
  input: {
    width: '100%',
    padding: '8px 12px',
    background: 'var(--bg, #fff)',
    border: '1px solid var(--sep, #e2e8f0)',
    borderRadius: 8,
    fontSize: 13,
    color: 'var(--fg, #0f172a)',
    outline: 'none',
  },
  footer: {
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginTop: 'auto',
    paddingTop: 14,
  },
  removeBtn: {
    padding: '6px 12px',
    background: 'transparent',
    border: 'none',
    color: '#ef4444',
    fontSize: 12,
    fontWeight: 700,
    cursor: 'pointer',
    borderRadius: 8,
  },
  doneBtn: {
    padding: '6px 16px',
    background: 'var(--accent, #FF7A00)',
    border: 'none',
    color: '#fff',
    fontSize: 12,
    fontWeight: 700,
    cursor: 'pointer',
    borderRadius: 8,
    boxShadow: '0 2px 8px rgba(255, 122, 0, 0.3)',
  },
  loading: {
    flex: 1,
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    fontSize: 12,
    color: 'var(--muted, #64748b)',
  }
};

const kTrackingParams = new Set([
  'utm_source', 'utm_medium', 'utm_campaign', 'utm_term', 'utm_content',
  'fbclid', 'gclid', 'gbraid', 'wbraid', 'msclkid', 'twclid', 'igshid',
  'mc_cid', 'mc_eid', '_ga', '_gl', 'yclid', 'dclid',
]);

const normalizeBookmarkUrl = (url) => {
  try {
    const u = new URL(url);
    const params = new URLSearchParams(u.search);
    for (const key of params.keys()) {
      if (kTrackingParams.has(key)) {
        params.delete(key);
      }
    }
    u.search = params.toString();
    if (u.pathname.length > 1 && u.pathname.endsWith('/')) {
      u.pathname = u.pathname.replace(/\/+$/, '');
    }
    return u.toString();
  } catch {
    return url;
  }
};

const BookmarkBar = () => {
  const [bookmark, setBookmark] = useState(null);
  const [title, setTitle] = useState('');
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    let mounted = true;

    const fetchInfo = () => {
      Promise.all([
        nativeRequest({ method: 'tabs.active' }),
        nativeRequest({ method: 'tabs.list' }),
      ]).then(([tabId, tabs]) => {
        if (!mounted) return;
        const activeTab = Array.isArray(tabs) ? tabs.find(t => t.id === tabId) : null;
        if (!activeTab) {
          setLoading(false);
          return;
        }

        const currentUrl = activeTab.url || activeTab.schemeUrl;
        window.cefQuery({
          request: 'get-bookmarks',
          onSuccess: (bmsJson) => {
            if (!mounted) return;
            try {
              const bookmarks = JSON.parse(bmsJson);
              const cleanUrl = normalizeBookmarkUrl(currentUrl);
              const activeBm = bookmarks.find(b => normalizeBookmarkUrl(b.url) === cleanUrl);
              if (activeBm) {
                setBookmark(activeBm);
                setTitle(activeBm.title);
              }
              setLoading(false);
            } catch (e) { setLoading(false); }
          },
          onFailure: () => {
            if (mounted) setLoading(false);
          },
        });
      }).catch(() => {
        if (mounted) setLoading(false);
      });
    };

    fetchInfo();

    const sub = window.cefQuery({
      request: 'bookmark-subscribe',
      persistent: true,
      onSuccess: (json) => {
        try {
          const ev = JSON.parse(json);
          if (ev.key === 'bookmark-refresh') {
            fetchInfo();
          }
        } catch (_) {}
      },
    });

    const onKeyDown = (event) => {
      if (event.key === 'Escape' && window.cefQuery) {
        event.preventDefault();
        window.cefQuery({ request: 'hide-bookmarkbar' });
      }
    };
    const onBlur = () => {
      if (window.cefQuery) {
        window.cefQuery({ request: 'hide-bookmarkbar' });
      }
    };
    window.addEventListener('keydown', onKeyDown);
    window.addEventListener('blur', onBlur);

    return () => {
      mounted = false;
      window.removeEventListener('keydown', onKeyDown);
      window.removeEventListener('blur', onBlur);
      if (sub && typeof sub.cancel === 'function') sub.cancel();
    };
  }, []);

  const handleSave = (e) => {
    e.preventDefault();
    if (!bookmark || !window.cefQuery) return;
    const cleanUrl = bookmark.url.length + ":" + bookmark.url;
    const cleanTitle = title.length + ":" + title;
    window.cefQuery({
      request: `update-bookmark:${bookmark.id}:${cleanUrl}:${cleanTitle}`,
      onSuccess: () => {
        window.cefQuery({ request: 'hide-bookmarkbar' });
      }
    });
  };

  const handleRemove = () => {
    if (!bookmark || !window.cefQuery) return;
    setBookmark(null);
    setTitle('');
    window.cefQuery({
      request: `remove-bookmark:${bookmark.id}`,
      onSuccess: () => {
        window.cefQuery({ request: 'hide-bookmarkbar' });
      }
    });
  };

  return (
    <div style={S.wrapper}>
      <div style={S.panel}>
        <div style={S.header}>
          <div style={{ fontSize: 13, fontWeight: 800 }}>Bookmark</div>
        </div>
        
        {loading ? (
          <div style={S.loading}>Loading...</div>
        ) : bookmark ? (
          <form onSubmit={handleSave} style={S.body}>
            <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
              <label style={S.label}>Name</label>
              <input
                type="text"
                value={title}
                onChange={(e) => setTitle(e.target.value)}
                style={S.input}
                autoFocus
              />
            </div>
            
            <div style={S.footer}>
              <button
                type="button"
                onClick={handleRemove}
                style={S.removeBtn}
                onMouseEnter={e => e.target.style.background = 'rgba(239,68,68,0.1)'}
                onMouseLeave={e => e.target.style.background = 'transparent'}
              >
                Remove
              </button>
              <button
                type="submit"
                style={S.doneBtn}
                onMouseEnter={e => e.target.style.transform = 'scale(1.02)'}
                onMouseLeave={e => e.target.style.transform = 'scale(1)'}
              >
                Done
              </button>
            </div>
          </form>
        ) : (
          <div style={S.loading}>Bookmark not found.</div>
        )}
      </div>
    </div>
  );
};

export default BookmarkBar;
