import React, { useRef, useState } from 'react';
import Editor from '@monaco-editor/react';

const guessLanguage = (mimeType, fileName) => {
  if (mimeType === 'application/json') return 'json';
  if (mimeType === 'application/xml' || mimeType === 'text/xml') return 'xml';
  if (mimeType === 'text/html') return 'html';
  if (mimeType === 'text/css') return 'css';
  if (mimeType === 'text/javascript') return 'javascript';
  if (mimeType === 'text/typescript') return 'typescript';
  if (mimeType === 'text/x-python') return 'python';
  if (mimeType === 'text/x-shellscript') return 'shell';
  if (mimeType === 'text/x-sql') return 'sql';
  if (mimeType === 'text/x-c') return 'c';
  if (mimeType === 'text/x-rust') return 'rust';
  if (mimeType === 'text/x-go') return 'go';
  if (mimeType === 'text/x-java') return 'java';
  if (mimeType === 'text/x-ruby') return 'ruby';
  if (mimeType === 'text/x-lua') return 'lua';
  if (mimeType === 'text/x-php') return 'php';
  if (mimeType === 'text/x-tex') return 'latex';
  if (mimeType === 'text/markdown') return 'markdown';
  if (mimeType === 'text/yaml') return 'yaml';
  if (mimeType === 'text/csv') return 'plaintext';
  const ext = (fileName || '').split('.').pop().toLowerCase();
  const extMap = {
    json: 'json', jsonl: 'json', xml: 'xml', html: 'html', htm: 'html', css: 'css',
    js: 'javascript', mjs: 'javascript', jsx: 'javascript',
    ts: 'typescript', tsx: 'typescript',
    py: 'python', sh: 'shell', bash: 'shell', zsh: 'shell',
    sql: 'sql', c: 'c', cpp: 'cpp', h: 'c', hpp: 'cpp',
    rs: 'rust', go: 'go', java: 'java', rb: 'ruby',
    lua: 'lua', php: 'php', tex: 'latex', md: 'markdown',
    yaml: 'yaml', yml: 'yaml', toml: 'ini', ini: 'ini',
    cfg: 'ini', conf: 'ini', log: 'plaintext', txt: 'plaintext',
    csv: 'plaintext',
  };
  return extMap[ext] || 'plaintext';
};

function LoadingFallback() {
  return (
    <div style={{
      width: '100%', height: '100%',
      display: 'flex', alignItems: 'center', justifyContent: 'center',
      background: '#1e1e1e', color: '#858585', fontSize: '13px',
    }}>
      Loading editor...
    </div>
  );
}

export default function TextViewer({ content, mimeType, fileName }) {
  const editorRef = useRef(null);
  const [loadError, setLoadError] = useState(null);
  const language = guessLanguage(mimeType, fileName);

  const handleMount = (editor, monaco) => {
    editorRef.current = editor;
    editor.focus();
  };

  const handleError = (error) => {
    console.error('[DOCPREVIEW] Monaco load error:', error);
    setLoadError(error?.message || String(error));
  };

  if (loadError) {
    return (
      <div style={{
        width: '100%', height: '100%', padding: '16px',
        background: '#1e1e1e', color: '#e2e8f0',
        fontFamily: 'monospace', fontSize: '13px', whiteSpace: 'pre-wrap',
        overflow: 'auto',
      }}>
        <div style={{ color: '#f44', marginBottom: '8px', fontWeight: 'bold' }}>
          Editor failed to load:
        </div>
        {loadError}
        <div style={{ marginTop: '16px', color: '#888', borderTop: '1px solid #333', paddingTop: '12px' }}>
          Raw content:
        </div>
        <pre style={{ margin: 0, whiteSpace: 'pre-wrap', wordBreak: 'break-all' }}>{content}</pre>
      </div>
    );
  }

  return (
    <div style={{ width: '100%', height: '100%' }}>
      <Editor
        language={language}
        value={content || ''}
        theme="vs-dark"
        loading={<LoadingFallback />}
        beforeMount={(monaco) => {
          // Suppress worker errors in CEF's custom scheme environment
          try {
            const origGetWorker = monaco?.editor?.getWorker;
            if (origGetWorker) {
              // Workers are optional — editor still works without them
            }
          } catch (_) {}
        }}
        options={{
          readOnly: true,
          automaticLayout: true,
          fontSize: 13,
          fontFamily: "'JetBrains Mono', 'Fira Code', 'Cascadia Code', Consolas, monospace",
          lineNumbers: 'on',
          minimap: { enabled: true, maxColumn: 80, renderCharacters: false },
          scrollBeyondLastLine: false,
          wordWrap: 'on',
          renderLineHighlight: 'gutter',
          overviewRulerBorder: false,
          scrollbar: {
            vertical: 'auto',
            horizontal: 'auto',
            verticalScrollbarSize: 10,
            horizontalScrollbarSize: 10,
          },
          padding: { top: 12, bottom: 12 },
          guides: { indentation: true, bracketPairs: true },
          renderWhitespace: 'none',
          folding: true,
          lineDecorationsWidth: 8,
          lineNumbersMinChars: 4,
          smoothScrolling: true,
          cursorBlinking: 'smooth',
          contextmenu: true,
          mouseWheelZoom: true,
        }}
        onMount={handleMount}
      />
    </div>
  );
}
