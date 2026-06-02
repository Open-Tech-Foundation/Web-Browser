import React from 'react';

const formatMemory = (bytes) => {
  if (bytes < 0) return null;
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(0)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
  return `${(bytes / (1024 * 1024 * 1024)).toFixed(2)} GB`;
};

const getColor = (bytes) => {
  if (bytes < 0) return 'bg-slate-100 text-slate-500 dark:bg-white/5 dark:text-slate-400';
  const mb = bytes / (1024 * 1024);
  if (mb < 100) return 'bg-emerald-50 text-emerald-700 dark:bg-emerald-500/10 dark:text-emerald-400';
  if (mb < 200) return 'bg-amber-50 text-amber-700 dark:bg-amber-500/10 dark:text-amber-400';
  return 'bg-red-50 text-red-700 dark:bg-red-500/10 dark:text-red-400';
};

const MemoryChip = ({ memoryBytes }) => {
  const label = formatMemory(memoryBytes);
  if (!label) return null;

  return (
    <span
      className={`ml-2 flex items-center gap-1.5 px-2 py-0.5 rounded-md text-[11px] font-semibold tabular-nums shrink-0 select-none transition-colors ${getColor(memoryBytes)}`}
      title={`RAM usage: ${label}`}
    >
      <svg xmlns="http://www.w3.org/2000/svg" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <path d="M6 19v-3"/><path d="M10 19v-3"/><path d="M14 19v-3"/><path d="M18 19v-3"/>
        <path d="M3 7h18a1 1 0 0 1 1 1v8a1 1 0 0 1-1 1H3a1 1 0 0 1-1-1V8a1 1 0 0 1 1-1z"/>
        <path d="M8 7V5a1 1 0 0 1 1-1h6a1 1 0 0 1 1 1v2"/>
      </svg>
      {label}
    </span>
  );
};

export default MemoryChip;
