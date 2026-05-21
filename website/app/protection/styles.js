// CSS extracted from page.jsx. Exported as a template string so the page
// can drop it into a <style> tag.
export const protectionStyles = `
#protection-container {
  --bg: #f4efe6;
  --ink: #17130d;
  --muted: #675f52;
  --panel: #fffaf0;
  --line: #d8ccb8;
  --good: #0f7a43;
  --bad: #a93424;
  --warn: #9a6500;
  --info: #1a5a8f;
  --chip: #ece0cb;
  background:
    radial-gradient(circle at 10% 10%, rgba(255, 214, 139, 0.55), transparent 32rem),
    linear-gradient(135deg, #f4efe6 0%, #e7dbc7 100%);
  color: var(--ink);
  font: 15px/1.5 ui-monospace, "SFMono-Regular", "Menlo", "Consolas", monospace;
  min-height: calc(100vh - 80px);
  padding: 32px 24px 64px;
}
#protection-container main { max-width: 1100px; margin: 0 auto; }
#protection-container header { margin-bottom: 28px; }
#protection-container .eyebrow {
  text-transform: uppercase; letter-spacing: 0.12em; font-size: 11px;
  color: var(--muted); margin-bottom: 8px;
}
#protection-container h1 { font-size: 28px; margin: 0 0 8px; }
#protection-container header p { color: var(--muted); margin: 0 0 16px; max-width: 70ch; }
#protection-container .header-actions { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; }
#protection-container button {
  background: var(--ink); color: var(--panel);
  border: none; border-radius: 4px;
  padding: 10px 16px; font: inherit; cursor: pointer;
}
#protection-container button:hover:not(:disabled) { background: #2a2418; }
#protection-container button:disabled,
#protection-container button.is-disabled { opacity: 0.5; cursor: not-allowed; }
#protection-container button.secondary {
  background: var(--panel); color: var(--ink); border: 1px solid var(--line);
}

#protection-container .reload-banner {
  background: #fff4d1; border: 1px solid #d8be62; padding: 12px 16px;
  border-radius: 6px; margin: 16px 0; display: flex; gap: 12px;
  align-items: center; justify-content: space-between; flex-wrap: wrap;
}
#protection-container .reload-banner strong { display: block; }
#protection-container .reload-banner small { color: var(--muted); }
#protection-container .reload-banner .banner-actions { display: flex; gap: 8px; }

#protection-container .score-card {
  background: var(--panel); border: 1px solid var(--line);
  border-radius: 6px; padding: 18px 20px; margin-bottom: 20px;
  display: grid; grid-template-columns: auto 1fr; gap: 20px; align-items: center;
}
#protection-container .score-number { font-size: 44px; font-weight: 700; }
#protection-container .score-number small { font-size: 16px; color: var(--muted); font-weight: 400; }
#protection-container .score-bar {
  height: 8px; background: var(--chip); border-radius: 4px; overflow: hidden;
  margin: 8px 0 6px;
}
#protection-container .score-bar > div { height: 100%; background: var(--good); transition: width 250ms; }
#protection-container .score-label { color: var(--muted); font-size: 13px; }

#protection-container .filter-bar {
  display: flex; gap: 6px; flex-wrap: wrap; margin: 0 0 16px;
}
#protection-container .filter-bar button {
  background: var(--panel); color: var(--ink); border: 1px solid var(--line);
  padding: 6px 12px; font-size: 13px; border-radius: 999px;
}
#protection-container .filter-bar button.active {
  background: var(--ink); color: var(--panel); border-color: var(--ink);
}

#protection-container .row-list {
  background: var(--panel); border: 1px solid var(--line); border-radius: 6px;
  overflow: hidden;
}
#protection-container .row {
  border-top: 1px solid var(--line);
}
#protection-container .row:first-child { border-top: none; }
#protection-container .row-head {
  display: grid; grid-template-columns: 28px 1fr auto auto; gap: 12px;
  padding: 12px 16px; cursor: pointer; align-items: center;
}
#protection-container .row-head:hover { background: rgba(0,0,0,0.02); }
#protection-container .row-icon {
  width: 22px; height: 22px; border-radius: 50%; display: inline-flex;
  align-items: center; justify-content: center; font-size: 12px; font-weight: 700;
  color: var(--panel);
}
#protection-container .row-icon.ok { background: var(--good); }
#protection-container .row-icon.warn { background: var(--warn); }
#protection-container .row-icon.fail { background: var(--bad); }
#protection-container .row-icon.idle { background: var(--chip); color: var(--muted); }
#protection-container .row-icon.running { background: var(--info); animation: pulse 1.4s ease-in-out infinite; }
#protection-container .row-icon.pending-gesture { background: var(--info); }
#protection-container .row-icon.pending-reload { background: var(--info); }
@keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.55; } }

#protection-container .row-meta { min-width: 0; }
#protection-container .row-label { font-weight: 600; }
#protection-container .row-summary { color: var(--muted); font-size: 13px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
#protection-container .row-tags { display: flex; gap: 4px; flex-shrink: 0; }
#protection-container .tag {
  font-size: 10px; text-transform: uppercase; letter-spacing: 0.05em;
  padding: 2px 8px; border-radius: 3px; background: var(--chip); color: var(--ink);
}
#protection-container .tag.entropy-high { background: #f5d3c4; color: #5e1f10; }
#protection-container .tag.entropy-medium { background: #ffe9b8; color: #5e4a10; }
#protection-container .tag.entropy-low { background: #d6e9d0; color: #234118; }
#protection-container .tag.entropy-security { background: #d8d6f5; color: #2a2570; }
#protection-container .row-toggle {
  width: 20px; text-align: center; color: var(--muted); font-family: inherit;
}

#protection-container .row-detail {
  padding: 0 16px 16px 56px; border-top: 1px dashed var(--line); background: #fbf6e8;
}
#protection-container .row-detail .detail-text {
  margin: 10px 0; color: var(--muted); font-size: 13px;
}
#protection-container .row-detail dl {
  font-size: 12px; margin: 8px 0 0;
}
#protection-container .row-detail .dl-pair {
  display: grid; grid-template-columns: minmax(220px, auto) 1fr;
  gap: 4px 12px; padding: 2px 0;
}
#protection-container .row-detail dt { color: var(--muted); }
#protection-container .row-detail dd { margin: 0; word-break: break-word; }
#protection-container .row-detail .preview {
  margin: 12px 0; padding: 8px; background: #fff; border: 1px solid var(--line); border-radius: 4px;
}
#protection-container .row-detail .preview canvas { display: block; }
#protection-container .row-detail .speaker-test-header {
  display: flex; flex-direction: column; gap: 4px; margin-bottom: 10px;
}
#protection-container .row-detail .speaker-test-note { color: var(--muted); font-size: 12px; }
#protection-container .row-detail .speaker-test-result {
  margin-top: 10px; font-size: 12px;
}
#protection-container .row-detail .speaker-test-result.ok { color: var(--good); }
#protection-container .row-detail .speaker-test-result.fail { color: var(--bad); }
#protection-container .row-detail .speaker-test-result.playing { color: var(--info); }
#protection-container .row-rerun {
  margin-top: 10px; font-size: 12px; padding: 5px 10px;
}

#protection-container textarea#export-json-output {
  width: 100%; min-height: 220px; margin-top: 12px;
  font: 12px/1.4 ui-monospace, monospace;
  background: var(--panel); border: 1px solid var(--line);
  padding: 10px; border-radius: 4px;
}

@media (max-width: 720px) {
  #protection-container .score-card { grid-template-columns: 1fr; }
  #protection-container .row-head { grid-template-columns: 28px 1fr auto; }
  #protection-container .row-tags { display: none; }
  #protection-container .row-detail { padding-left: 16px; }
}
`;
