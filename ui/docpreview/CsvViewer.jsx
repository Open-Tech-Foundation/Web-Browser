import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';

const ROW_HEIGHT = 40;
const HEADER_HEIGHT = 44;
const BUFFER_ROWS = 5;

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
        if (currentField !== '' || currentLine.length > 0 || (char === '\n' && text[i - 1] === '\r')) {
          currentLine.push(currentField);
          lines.push(currentLine);
          currentLine = [];
          currentField = '';
        }
      } else if (char === '\r') {
        // Will be handled with the following \n
      } else {
        currentField += char;
      }
    }
    i++;
  }

  // Push the last field and line
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

function CsvViewer({ content, fileName }) {
  const containerRef = useRef(null);
  const [scrollTop, setScrollTop] = useState(0);
  const [containerHeight, setContainerHeight] = useState(600);
  const [sortCol, setSortCol] = useState(null);
  const [sortDir, setSortDir] = useState('asc');
  const [searchQuery, setSearchQuery] = useState('');

  const { headers, rows, allRows } = useMemo(() => {
    if (!content) return { headers: [], rows: [], allRows: [] };
    const parsed = parseCSV(content);
    if (parsed.length === 0) return { headers: [], rows: [], allRows: [] };

    const normalized = normalizeRows(parsed);
    const rawHeaders = normalized[0];
    const dataRows = normalized.slice(1).filter((r) => r.some((c) => c.trim() !== ''));

    return { headers: rawHeaders, rows: dataRows, allRows: dataRows };
  }, [content]);

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
      // Try numeric sort first
      const an = parseFloat(av);
      const bn = parseFloat(bv);
      if (!isNaN(an) && !isNaN(bn) && av.trim() !== '' && bv.trim() !== '') {
        return (an - bn) * dir;
      }
      return av.localeCompare(bv) * dir;
    });
  }, [filteredRows, sortCol, sortDir]);

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
  const visibleEnd = Math.min(sortedRows.length, Math.ceil((scrollTop + containerHeight) / ROW_HEIGHT) + BUFFER_ROWS);
  const visibleRows = sortedRows.slice(visibleStart, visibleEnd);

  const handleScroll = useCallback((e) => {
    setScrollTop(e.target.scrollTop);
  }, []);

  const handleSort = useCallback((colIndex) => {
    if (sortCol === colIndex) {
      setSortDir((d) => (d === 'asc' ? 'desc' : 'asc'));
    } else {
      setSortCol(colIndex);
      setSortDir('asc');
    }
  }, [sortCol]);

  const colCount = headers.length || 1;

  if (headers.length === 0) {
    return (
      <div style={emptyStyle}>
        <div style={{ fontSize: '16px', fontWeight: '600', color: 'rgba(255,255,255,0.6)' }}>
          Empty or invalid CSV
        </div>
      </div>
    );
  }

  return (
    <div style={wrapperStyle}>
      {/* Toolbar */}
      <div style={toolbarStyle}>
        <div style={statsStyle}>
          <span style={statsBadgeStyle}>
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" style={{ marginRight: '6px' }}>
              <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" />
              <polyline points="14 2 14 8 20 8" />
              <line x1="16" y1="13" x2="8" y2="13" />
              <line x1="16" y1="17" x2="8" y2="17" />
              <polyline points="10 9 9 9 8 9" />
            </svg>
            {sortedRows.length.toLocaleString()} rows
          </span>
          <span style={statsBadgeStyle}>
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" style={{ marginRight: '6px' }}>
              <rect x="3" y="3" width="18" height="18" rx="2" ry="2" />
              <line x1="3" y1="9" x2="21" y2="9" />
              <line x1="9" y1="21" x2="9" y2="9" />
            </svg>
            {headers.length} columns
          </span>
        </div>
        <div style={searchWrapStyle}>
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" style={{ position: 'absolute', left: '10px', top: '50%', transform: 'translateY(-50%)', color: 'rgba(255,255,255,0.3)', pointerEvents: 'none' }}>
            <circle cx="11" cy="11" r="8" />
            <line x1="21" y1="21" x2="16.65" y2="16.65" />
          </svg>
          <input
            type="text"
            placeholder="Search rows..."
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
            style={searchInputStyle}
          />
          {searchQuery && (
            <button onClick={() => setSearchQuery('')} style={searchClearStyle} title="Clear search">
              <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5">
                <line x1="18" y1="6" x2="6" y2="18" />
                <line x1="6" y1="6" x2="18" y2="18" />
              </svg>
            </button>
          )}
        </div>
      </div>

      {/* Table */}
      <div ref={containerRef} style={tableContainerStyle} onScroll={handleScroll}>
        <div style={{ height: totalHeight + HEADER_HEIGHT, position: 'relative' }}>
          {/* Header */}
          <div style={headerRowStyle}>
            {headers.map((h, i) => (
              <div
                key={i}
                style={{ ...headerCellStyle, minWidth: `${Math.max(120, 100 / colCount)}%`, flex: 1 }}
                onClick={() => handleSort(i)}
                title="Click to sort"
              >
                <span style={{ flex: 1, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{h}</span>
                {sortCol === i && (
                  <span style={{ marginLeft: '6px', fontSize: '10px', opacity: 0.8 }}>
                    {sortDir === 'asc' ? '▲' : '▼'}
                  </span>
                )}
              </div>
            ))}
          </div>

          {/* Virtualized rows */}
          {visibleRows.map((row, idx) => {
            const actualIndex = visibleStart + idx;
            return (
              <div
                key={actualIndex}
                style={{
                  ...rowStyle,
                  top: HEADER_HEIGHT + actualIndex * ROW_HEIGHT,
                  position: 'absolute',
                  left: 0,
                  right: 0,
                }}
              >
                {row.map((cell, ci) => (
                  <div
                    key={ci}
                    style={{
                      ...cellStyle,
                      minWidth: `${Math.max(120, 100 / colCount)}%`,
                      flex: 1,
                    }}
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
  );
}

const wrapperStyle = {
  width: '100%',
  height: '100%',
  display: 'flex',
  flexDirection: 'column',
  background: '#0f172a',
  overflow: 'hidden',
};

const toolbarStyle = {
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'space-between',
  padding: '10px 16px',
  borderBottom: '1px solid rgba(255, 255, 255, 0.06)',
  background: 'rgba(15, 23, 42, 0.95)',
  flexShrink: 0,
  gap: '12px',
};

const statsStyle = {
  display: 'flex',
  alignItems: 'center',
  gap: '8px',
};

const statsBadgeStyle = {
  display: 'inline-flex',
  alignItems: 'center',
  padding: '4px 10px',
  borderRadius: '6px',
  background: 'rgba(255, 255, 255, 0.05)',
  border: '1px solid rgba(255, 255, 255, 0.08)',
  color: 'rgba(255, 255, 255, 0.6)',
  fontSize: '11px',
  fontWeight: '600',
  letterSpacing: '0.3px',
};

const searchWrapStyle = {
  position: 'relative',
  flexShrink: 0,
};

const searchInputStyle = {
  background: 'rgba(255, 255, 255, 0.04)',
  border: '1px solid rgba(255, 255, 255, 0.08)',
  borderRadius: '8px',
  padding: '6px 28px 6px 30px',
  color: 'rgba(255, 255, 255, 0.9)',
  fontSize: '12px',
  width: '200px',
  outline: 'none',
  transition: 'border-color 0.2s',
};

const searchClearStyle = {
  position: 'absolute',
  right: '6px',
  top: '50%',
  transform: 'translateY(-50%)',
  background: 'none',
  border: 'none',
  color: 'rgba(255,255,255,0.4)',
  cursor: 'pointer',
  padding: '2px',
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
};

const tableContainerStyle = {
  flex: 1,
  overflow: 'auto',
  position: 'relative',
};

const headerRowStyle = {
  display: 'flex',
  alignItems: 'center',
  height: `${HEADER_HEIGHT}px`,
  background: 'rgba(15, 23, 42, 0.98)',
  borderBottom: '2px solid rgba(255, 122, 0, 0.3)',
  position: 'sticky',
  top: 0,
  zIndex: 10,
  paddingLeft: '16px',
  paddingRight: '16px',
};

const headerCellStyle = {
  display: 'flex',
  alignItems: 'center',
  padding: '0 12px',
  color: 'rgba(255, 255, 255, 0.85)',
  fontSize: '12px',
  fontWeight: '700',
  letterSpacing: '0.4px',
  textTransform: 'uppercase',
  cursor: 'pointer',
  userSelect: 'none',
  height: '100%',
  borderRight: '1px solid rgba(255, 255, 255, 0.04)',
};

const rowStyle = {
  display: 'flex',
  alignItems: 'center',
  height: `${ROW_HEIGHT}px`,
  borderBottom: '1px solid rgba(255, 255, 255, 0.03)',
  paddingLeft: '16px',
  paddingRight: '16px',
  transition: 'background 0.1s',
};

const cellStyle = {
  padding: '0 12px',
  color: 'rgba(255, 255, 255, 0.75)',
  fontSize: '13px',
  fontFamily: "'Inter', system-ui, sans-serif",
  overflow: 'hidden',
  textOverflow: 'ellipsis',
  whiteSpace: 'nowrap',
  borderRight: '1px solid rgba(255, 255, 255, 0.03)',
  height: '100%',
  display: 'flex',
  alignItems: 'center',
};

const emptyStyle = {
  width: '100%',
  height: '100%',
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  color: 'rgba(255,255,255,0.5)',
  fontSize: '14px',
};

export default CsvViewer;
