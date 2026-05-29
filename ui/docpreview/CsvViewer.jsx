import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';

const ROW_HEIGHT = 44;
const HEADER_HEIGHT = 48;
const BUFFER_ROWS = 5;
const MIN_COL_WIDTH = 80;
const MAX_COL_WIDTH = 600;
const DEFAULT_COL_WIDTH = 160;

function parseCSV(text) {
  const lines = [];
  let currentLine = [];
  let currentField = '';
  let inQuotes = false;
  let i = 0;

  while (i < text.length) {
    const char = text[i];
    const nextChar = text[i + 1];

    if (inQuotes) {
      if (char === '"') {
        if (nextChar === '"') {
          currentField += '"';
          i += 2;
          continue;
        } else {
          inQuotes = false;
        }
      } else {
        currentField += char;
      }
    } else {
      if (char === '"') {
        inQuotes = true;
      } else if (char === ',') {
        currentLine.push(currentField);
        currentField = '';
      } else if (char === '\n') {
        if (currentField !== '' || currentLine.length > 0) {
          currentLine.push(currentField);
          lines.push(currentLine);
          currentLine = [];
          currentField = '';
        }
      } else if (char === '\r') {
        // Handled with following \n
      } else {
        currentField += char;
      }
    }
    i++;
  }

  if (currentField !== '' || currentLine.length > 0 || inQuotes) {
    currentLine.push(currentField);
    lines.push(currentLine);
  }

  return lines;
}

function normalizeRows(rows) {
  if (rows.length === 0) return [];
  const maxCols = Math.max(...rows.map((r) => r.length));
  return rows.map((r) => {
    const padded = [...r];
    while (padded.length < maxCols) padded.push('');
    return padded;
  });
}

function measureTextWidth(text, font = '13px Inter, system-ui, sans-serif') {
  const canvas = document.createElement('canvas');
  const ctx = canvas.getContext('2d');
  if (!ctx) return text.length * 8;
  ctx.font = font;
  return ctx.measureText(text).width;
}

function CsvViewer({ content, fileName }) {
  const containerRef = useRef(null);
  const [scrollTop, setScrollTop] = useState(0);
  const [containerHeight, setContainerHeight] = useState(600);
  const [sortCol, setSortCol] = useState(null);
  const [sortDir, setSortDir] = useState('asc');
  const [searchQuery, setSearchQuery] = useState('');
  const [colWidths, setColWidths] = useState([]);
  const [isDark, setIsDark] = useState(false);
  const [appearanceMode, setAppearanceMode] = useState('auto');
  const resizeState = useRef({ active: false, colIndex: -1, startX: 0, startWidth: 0 });

  // Theme integration
  const applyTheme = useCallback(() => {
    // The doc-preview overlay shell is always dark; match it regardless of the
    // global appearance setting so the table never renders light-on-dark.
    document.documentElement.classList.add('dark');
    setIsDark(true);
  }, []);

  useEffect(() => {
    applyTheme(appearanceMode);
  }, [appearanceMode, applyTheme]);

  useEffect(() => {
    const mq = window.matchMedia('(prefers-color-scheme: dark)');
    const onChange = () => { if (appearanceMode === 'auto') applyTheme('auto'); };
    mq.addEventListener('change', onChange);
    return () => mq.removeEventListener('change', onChange);
  }, [appearanceMode, applyTheme]);

  useEffect(() => {
    if (!window.cefQuery) return;
    window.cefQuery({
      request: 'get-settings',
      onSuccess: (response) => {
        try {
          const settings = JSON.parse(response);
          setAppearanceMode(settings.appearanceMode || 'auto');
        } catch { /* ignore */ }
      },
    });
  }, []);

  // Parse CSV
  const { headers, allRows, commentLines } = useMemo(() => {
    if (!content) return { headers: [], allRows: [], commentLines: [] };
    const parsed = parseCSV(content);
    if (parsed.length === 0) return { headers: [], allRows: [], commentLines: [] };

    const normalized = normalizeRows(parsed);
    const comments = [];
    let headerIndex = 0;

    while (
      headerIndex < normalized.length &&
      normalized[headerIndex].length > 0 &&
      normalized[headerIndex][0].trim().startsWith('#')
    ) {
      comments.push(normalized[headerIndex][0].trim());
      headerIndex++;
    }

    if (headerIndex >= normalized.length) {
      return { headers: [], allRows: [], commentLines: comments };
    }

    const rawHeaders = normalized[headerIndex];
    const dataRows = normalized
      .slice(headerIndex + 1)
      .filter((r) => r.some((c) => c.trim() !== ''));

    return { headers: rawHeaders, allRows: dataRows, commentLines: comments };
  }, [content]);

  // Initialize column widths
  useEffect(() => {
    if (headers.length === 0 || colWidths.length === headers.length) return;

    const saved = localStorage.getItem(`csv-col-widths:${fileName}`);
    if (saved) {
      try {
        const parsed = JSON.parse(saved);
        if (Array.isArray(parsed) && parsed.length === headers.length) {
          setColWidths(parsed.map((w) => Math.max(MIN_COL_WIDTH, Math.min(MAX_COL_WIDTH, w))));
          return;
        }
      } catch { /* ignore */ }
    }

    // Auto-calculate widths based on content
    const widths = headers.map((h, i) => {
      const headerWidth = measureTextWidth(h) + 48;
      let maxContentWidth = headerWidth;
      for (let row = 0; row < Math.min(allRows.length, 50); row++) {
        const cell = allRows[row][i] || '';
        const cw = measureTextWidth(cell) + 32;
        if (cw > maxContentWidth) maxContentWidth = cw;
      }
      return Math.max(MIN_COL_WIDTH, Math.min(MAX_COL_WIDTH, maxContentWidth));
    });

    setColWidths(widths);
  }, [headers, allRows, fileName, colWidths.length]);

  // Persist widths
  useEffect(() => {
    if (colWidths.length > 0 && fileName) {
      localStorage.setItem(`csv-col-widths:${fileName}`, JSON.stringify(colWidths));
    }
  }, [colWidths, fileName]);

  // Resize handlers
  const startResize = useCallback((e, colIndex) => {
    e.preventDefault();
    e.stopPropagation();
    resizeState.current = {
      active: true,
      colIndex,
      startX: e.clientX,
      startWidth: colWidths[colIndex] || DEFAULT_COL_WIDTH,
    };
    document.body.style.cursor = 'col-resize';
    document.body.style.userSelect = 'none';
  }, [colWidths]);

  useEffect(() => {
    const onMove = (e) => {
      if (!resizeState.current.active) return;
      const { colIndex, startX, startWidth } = resizeState.current;
      const delta = e.clientX - startX;
      const newWidth = Math.max(MIN_COL_WIDTH, Math.min(MAX_COL_WIDTH, startWidth + delta));
      setColWidths((prev) => {
        const next = [...prev];
        next[colIndex] = newWidth;
        return next;
      });
    };
    const onUp = () => {
      if (resizeState.current.active) {
        resizeState.current.active = false;
        document.body.style.cursor = '';
        document.body.style.userSelect = '';
      }
    };
    window.addEventListener('mousemove', onMove);
    window.addEventListener('mouseup', onUp);
    return () => {
      window.removeEventListener('mousemove', onMove);
      window.removeEventListener('mouseup', onUp);
    };
  }, []);

  // Auto-fit on double-click
  const autoFitColumn = useCallback((colIndex) => {
    const headerWidth = measureTextWidth(headers[colIndex]) + 48;
    let maxContentWidth = headerWidth;
    for (let row = 0; row < allRows.length; row++) {
      const cell = allRows[row][colIndex] || '';
      const cw = measureTextWidth(cell) + 32;
      if (cw > maxContentWidth) maxContentWidth = cw;
    }
    setColWidths((prev) => {
      const next = [...prev];
      next[colIndex] = Math.max(MIN_COL_WIDTH, Math.min(MAX_COL_WIDTH, maxContentWidth));
      return next;
    });
  }, [headers, allRows]);

  // Filter & sort
  const filteredRows = useMemo(() => {
    if (!searchQuery.trim()) return allRows;
    const q = searchQuery.toLowerCase();
    return allRows.filter((row) => row.some((cell) => cell.toLowerCase().includes(q)));
  }, [allRows, searchQuery]);

  const sortedRows = useMemo(() => {
    if (sortCol === null) return filteredRows;
    const dir = sortDir === 'asc' ? 1 : -1;
    return [...filteredRows].sort((a, b) => {
      const av = a[sortCol] || '';
      const bv = b[sortCol] || '';
      const an = parseFloat(av.replace(/[$,%]/g, ''));
      const bn = parseFloat(bv.replace(/[$,%]/g, ''));
      if (!isNaN(an) && !isNaN(bn) && av.trim() !== '' && bv.trim() !== '') {
        return (an - bn) * dir;
      }
      return av.localeCompare(bv) * dir;
    });
  }, [filteredRows, sortCol, sortDir]);

  // Virtualization
  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;
    const ro = new ResizeObserver((entries) => {
      for (const entry of entries) {
        setContainerHeight(entry.contentRect.height);
      }
    });
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  const totalHeight = sortedRows.length * ROW_HEIGHT;
  const visibleStart = Math.max(0, Math.floor(scrollTop / ROW_HEIGHT) - BUFFER_ROWS);
  const visibleEnd = Math.min(
    sortedRows.length,
    Math.ceil((scrollTop + containerHeight) / ROW_HEIGHT) + BUFFER_ROWS
  );
  const visibleRows = sortedRows.slice(visibleStart, visibleEnd);

  const handleScroll = useCallback((e) => setScrollTop(e.target.scrollTop), []);

  const handleSort = useCallback((colIndex) => {
    if (sortCol === colIndex) {
      setSortDir((d) => (d === 'asc' ? 'desc' : 'asc'));
    } else {
      setSortCol(colIndex);
      setSortDir('asc');
    }
  }, [sortCol]);

  const totalWidth = colWidths.reduce((a, b) => a + b, 0);

  if (headers.length === 0) {
    return (
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', height: '100%', color: C.muted, fontSize: 13, background: C.bg }}>
        Empty or invalid CSV
      </div>
    );
  }

  return (
    <div style={S.root}>
      {/* Scoped styles for things inline can't express: scrollbar + hover. */}
      <style>{`
        .otf-csv-scroll::-webkit-scrollbar { width: 12px; height: 12px; }
        .otf-csv-scroll::-webkit-scrollbar-track { background: rgba(148,163,184,0.08); }
        .otf-csv-scroll::-webkit-scrollbar-thumb { background: rgba(148,163,184,0.45); border-radius: 8px; border: 2px solid transparent; background-clip: padding-box; }
        .otf-csv-scroll::-webkit-scrollbar-thumb:hover { background: rgba(148,163,184,0.7); background-clip: padding-box; }
        .otf-csv-scroll::-webkit-scrollbar-corner { background: transparent; }
        .otf-csv-hcell:hover { color: #e2e8f0 !important; background: rgba(255,255,255,0.04); }
        .otf-csv-hcell:hover .otf-csv-resize { opacity: 1; }
        .otf-csv-row:hover { background: rgba(255,122,0,0.07) !important; }
        .otf-csv-input::placeholder { color: rgba(148,163,184,0.6); }
        .otf-csv-input:focus { border-color: rgba(255,122,0,0.5); }
      `}</style>

      {/* Toolbar */}
      <div style={S.toolbar}>
        <div style={S.searchWrap}>
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" style={S.searchIcon}>
            <circle cx="11" cy="11" r="8" />
            <line x1="21" y1="21" x2="16.65" y2="16.65" />
          </svg>
          <input
            type="text"
            placeholder="Search rows..."
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
            className="otf-csv-input"
            style={S.searchInput}
          />
          {searchQuery && (
            <button onClick={() => setSearchQuery('')} title="Clear search" style={S.searchClear}>
              <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5">
                <line x1="18" y1="6" x2="6" y2="18" />
                <line x1="6" y1="6" x2="18" y2="18" />
              </svg>
            </button>
          )}
        </div>
      </div>

      {/* Table container */}
      <div style={S.tableOuter}>
        <div style={S.tableCard}>
          <div style={{ flex: 1, minHeight: 0, overflow: 'hidden', position: 'relative' }}>
            <div ref={containerRef} className="otf-csv-scroll" style={{ position: 'absolute', inset: 0, overflow: 'auto' }} onScroll={handleScroll}>
              <div style={{ height: totalHeight + HEADER_HEIGHT, width: Math.max(totalWidth, 0) || '100%', minWidth: '100%', position: 'relative' }}>
                {/* Sticky Header */}
                <div style={{ ...S.headerRow, height: HEADER_HEIGHT }}>
                  {headers.map((h, i) => (
                    <div
                      key={i}
                      className="otf-csv-hcell"
                      style={{ ...S.headerCell, width: colWidths[i] || DEFAULT_COL_WIDTH }}
                      onClick={() => handleSort(i)}
                      title={`Sort by ${h}`}
                    >
                      <span style={S.ellipsis}>{h}</span>
                      {sortCol === i && (
                        <span style={{ marginLeft: 6, color: C.accent, fontSize: 10 }}>
                          {sortDir === 'asc' ? '▲' : '▼'}
                        </span>
                      )}
                      <div
                        className="otf-csv-resize"
                        style={S.resizeHandle}
                        onMouseDown={(e) => startResize(e, i)}
                        onDoubleClick={(e) => { e.stopPropagation(); autoFitColumn(i); }}
                        title="Drag to resize, double-click to auto-fit"
                      >
                        <div style={{ width: 1, height: '100%', background: 'rgba(148,163,184,0.35)' }} />
                      </div>
                    </div>
                  ))}
                </div>

                {/* Virtualized rows */}
                {visibleRows.map((row, idx) => {
                  const actualIndex = visibleStart + idx;
                  const isEven = actualIndex % 2 === 0;
                  return (
                    <div
                      key={actualIndex}
                      className="otf-csv-row"
                      style={{
                        position: 'absolute', left: 0, right: 0, display: 'flex', alignItems: 'center',
                        borderBottom: `1px solid ${C.borderSoft}`,
                        background: isEven ? 'rgba(255,255,255,0.015)' : 'transparent',
                        top: HEADER_HEIGHT + actualIndex * ROW_HEIGHT, height: ROW_HEIGHT,
                      }}
                    >
                      {row.map((cell, ci) => (
                        <div
                          key={ci}
                          style={{ ...S.dataCell, width: colWidths[ci] || DEFAULT_COL_WIDTH }}
                          title={cell}
                        >
                          {cell}
                        </div>
                      ))}
                    </div>
                  );
                })}
              </div>
            </div>
          </div>
        </div>
      </div>

      {/* Status bar */}
      <div style={S.statusBar}>
        <span style={S.statusItem}>
          <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke={C.accent} strokeWidth="2">
            <rect x="3" y="3" width="18" height="18" rx="2" ry="2" />
            <line x1="3" y1="9" x2="21" y2="9" />
            <line x1="9" y1="21" x2="9" y2="9" />
          </svg>
          {headers.length} columns
        </span>
        <span style={S.statusItem}>
          <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke={C.accent} strokeWidth="2">
            <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" />
            <polyline points="14 2 14 8 20 8" />
          </svg>
          {sortedRows.length.toLocaleString()} rows
        </span>
        {filteredRows.length !== allRows.length && (
          <span style={{ color: C.accent, marginLeft: 4 }}>
            Showing {filteredRows.length.toLocaleString()} of {allRows.length.toLocaleString()} rows
          </span>
        )}
      </div>
    </div>
  );
}

// Dark palette + inline style objects. The doc-preview overlay does not reliably
// receive Tailwind's generated utilities (they aren't scanned for this entry),
// so CsvViewer styles itself inline like the sibling DocPreview shell.
const C = {
  bg: '#1a1a2e',
  card: '#16213e',
  header: '#0f1729',
  text: '#e2e8f0',
  muted: '#94a3b8',
  border: 'rgba(255,255,255,0.10)',
  borderSoft: 'rgba(255,255,255,0.06)',
  accent: '#FF7A00',
};

const S = {
  root: { display: 'flex', flexDirection: 'column', height: '100%', width: '100%', background: C.bg, color: C.text, overflow: 'hidden' },
  toolbar: { display: 'flex', alignItems: 'center', justifyContent: 'flex-end', gap: 16, padding: '12px 24px', borderBottom: `1px solid ${C.border}`, background: 'rgba(15,23,42,0.6)', flexShrink: 0 },
  searchWrap: { position: 'relative', width: 320, maxWidth: '100%' },
  searchIcon: { position: 'absolute', left: 14, top: '50%', transform: 'translateY(-50%)', color: C.muted, pointerEvents: 'none' },
  searchInput: { width: '100%', padding: '8px 34px 8px 38px', borderRadius: 9999, background: C.header, border: `1px solid ${C.border}`, color: C.text, fontSize: 13, outline: 'none', boxSizing: 'border-box' },
  searchClear: { position: 'absolute', right: 10, top: '50%', transform: 'translateY(-50%)', display: 'flex', alignItems: 'center', justifyContent: 'center', padding: 2, borderRadius: 9999, border: 'none', background: 'transparent', color: C.muted, cursor: 'pointer' },
  tableOuter: { flex: 1, minHeight: 0, padding: 24 },
  tableCard: { height: '100%', borderRadius: 12, border: `1px solid ${C.border}`, background: 'rgba(15,23,42,0.4)', overflow: 'hidden', display: 'flex', flexDirection: 'column' },
  headerRow: { position: 'sticky', top: 0, zIndex: 20, display: 'flex', alignItems: 'center', background: C.header, borderBottom: `2px solid rgba(255,122,0,0.3)` },
  headerCell: { position: 'relative', display: 'flex', alignItems: 'center', padding: '0 12px', height: '100%', fontSize: 11, fontWeight: 700, letterSpacing: '0.05em', textTransform: 'uppercase', color: C.muted, userSelect: 'none', cursor: 'pointer', flexShrink: 0 },
  ellipsis: { flex: 1, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' },
  resizeHandle: { position: 'absolute', right: 0, top: '50%', transform: 'translateY(-50%)', height: '66%', width: 6, cursor: 'col-resize', display: 'flex', alignItems: 'center', justifyContent: 'center', opacity: 0, transition: 'opacity 0.15s ease' },
  dataCell: { padding: '0 12px', fontSize: 13, color: 'rgba(226,232,240,0.9)', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', flexShrink: 0 },
  statusBar: { flexShrink: 0, display: 'flex', alignItems: 'center', justifyContent: 'flex-end', padding: '10px 24px', borderTop: `1px solid ${C.border}`, background: 'rgba(15,23,42,0.6)', color: C.muted, fontSize: 11, fontWeight: 500, gap: 16 },
  statusItem: { display: 'flex', alignItems: 'center', gap: 6 },
};

export default CsvViewer;
