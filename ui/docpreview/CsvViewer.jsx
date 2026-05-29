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
  const applyTheme = useCallback((mode) => {
    const root = document.documentElement;
    if (mode === 'light') {
      root.classList.remove('dark');
      setIsDark(false);
    } else if (mode === 'dark') {
      root.classList.add('dark');
      setIsDark(true);
    } else {
      const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
      if (prefersDark) {
        root.classList.add('dark');
        setIsDark(true);
      } else {
        root.classList.remove('dark');
        setIsDark(false);
      }
    }
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
      <div className="flex items-center justify-center h-full text-text-muted text-sm">
        Empty or invalid CSV
      </div>
    );
  }

  return (
    <div className="flex flex-col h-full w-full bg-bg-main text-text-main overflow-hidden">
      {/* Toolbar */}
      <div className="flex items-center justify-end gap-4 px-6 py-3 border-b border-border-main bg-bg-card/50 backdrop-blur-sm shrink-0">
        {/* Search */}
        <div className="relative w-80">
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" className="absolute left-4 top-1/2 -translate-y-1/2 text-text-muted pointer-events-none">
            <circle cx="11" cy="11" r="8" />
            <line x1="21" y1="21" x2="16.65" y2="16.65" />
          </svg>
          <input
            type="text"
            placeholder="Search rows..."
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
            className="w-full pl-10 pr-8 py-2 rounded-full bg-bg-main border border-border-main text-text-main text-sm placeholder:text-text-muted/60 focus:outline-none focus:border-brand-orange/40 focus:ring-1 focus:ring-brand-orange/20 transition-all"
          />
          {searchQuery && (
            <button
              onClick={() => setSearchQuery('')}
              className="absolute right-3 top-1/2 -translate-y-1/2 p-1 rounded-full text-text-muted hover:text-text-main hover:bg-border-main transition-colors"
              title="Clear search"
            >
              <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5">
                <line x1="18" y1="6" x2="6" y2="18" />
                <line x1="6" y1="6" x2="18" y2="18" />
              </svg>
            </button>
          )}
        </div>
      </div>

      {/* Table container */}
      <div className="flex-1 min-h-0 p-6">
        <div className="h-full rounded-xl border border-border-main bg-bg-card overflow-hidden shadow-sm flex flex-col">
          <div className="flex-1 min-h-0 overflow-hidden relative">
            <div ref={containerRef} className="absolute inset-0 overflow-auto custom-scrollbar" onScroll={handleScroll}>
              <div style={{ height: totalHeight + HEADER_HEIGHT, width: Math.max(totalWidth, '100%'), position: 'relative' }}>
                {/* Sticky Header */}
                <div
                  className="sticky top-0 z-20 flex items-center bg-bg-card border-b-2 border-brand-orange/30"
                  style={{ height: HEADER_HEIGHT }}
                >
                  {headers.map((h, i) => (
                    <div
                      key={i}
                      className="relative flex items-center px-3 h-full text-[11px] font-bold tracking-wider uppercase text-text-muted select-none cursor-pointer hover:text-text-main hover:bg-bg-main/50 transition-colors group"
                      style={{ width: colWidths[i] || DEFAULT_COL_WIDTH, flexShrink: 0 }}
                      onClick={() => handleSort(i)}
                      title={`Sort by ${h}`}
                    >
                      <span className="truncate flex-1">{h}</span>
                      {sortCol === i && (
                        <span className="ml-1.5 text-brand-orange text-[10px]">
                          {sortDir === 'asc' ? '▲' : '▼'}
                        </span>
                      )}
                      {/* Resize handle */}
                      <div
                        className="absolute right-0 top-1/2 -translate-y-1/2 h-2/3 w-1.5 cursor-col-resize flex items-center justify-center opacity-0 group-hover:opacity-100 transition-opacity"
                        onMouseDown={(e) => startResize(e, i)}
                        onDoubleClick={(e) => { e.stopPropagation(); autoFitColumn(i); }}
                        title="Drag to resize, double-click to auto-fit"
                      >
                        <div className="w-px h-full bg-text-muted/30" />
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
                      className={`absolute left-0 right-0 flex items-center border-b border-border-main/50 ${
                        isEven ? 'bg-bg-main' : 'bg-bg-card/30'
                      } hover:bg-brand-orange/5 transition-colors`}
                      style={{ top: HEADER_HEIGHT + actualIndex * ROW_HEIGHT, height: ROW_HEIGHT }}
                    >
                      {row.map((cell, ci) => (
                        <div
                          key={ci}
                          className="px-3 text-[13px] text-text-main/90 truncate"
                          style={{ width: colWidths[ci] || DEFAULT_COL_WIDTH, flexShrink: 0 }}
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
      <div className="shrink-0 flex items-center justify-end px-6 py-2.5 border-t border-border-main bg-bg-card/60 text-text-muted text-[11px] font-medium gap-4">
        <span className="flex items-center gap-1.5">
          <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" className="text-brand-orange">
            <rect x="3" y="3" width="18" height="18" rx="2" ry="2" />
            <line x1="3" y1="9" x2="21" y2="9" />
            <line x1="9" y1="21" x2="9" y2="9" />
          </svg>
          {headers.length} columns
        </span>
        <span className="flex items-center gap-1.5">
          <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" className="text-brand-orange">
            <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" />
            <polyline points="14 2 14 8 20 8" />
          </svg>
          {sortedRows.length.toLocaleString()} rows
        </span>
        {filteredRows.length !== allRows.length && (
          <span className="text-brand-orange ml-1">
            Showing {filteredRows.length.toLocaleString()} of {allRows.length.toLocaleString()} rows
          </span>
        )}
      </div>
    </div>
  );
}

export default CsvViewer;
