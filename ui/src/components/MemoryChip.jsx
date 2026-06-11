import React from 'react';

const rangeLabel = (bytes) => {
  if (bytes < 0) return '';
  const mb = bytes / (1024 * 1024);
  if (mb <= 100) return 'Low';
  if (mb < 500) return 'Medium';
  return 'High';
};

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
    if (mb <= 100) return 'bg-emerald-50 text-emerald-700 dark:bg-emerald-500/10 dark:text-emerald-400';
    if (mb < 500) return 'bg-amber-50 text-amber-700 dark:bg-amber-500/10 dark:text-amber-400';
    return 'bg-red-50 text-red-700 dark:bg-red-500/10 dark:text-red-400';
};

const MemoryChip = ({ memoryBytes }) => {
  const label = formatMemory(memoryBytes);
  if (!label) return null;

  return (
    <span
      className={`ml-2 flex items-center gap-1.5 px-2 py-0.5 rounded-md text-[11px] font-semibold tabular-nums shrink-0 select-none transition-colors ${getColor(memoryBytes)}`}
      title={`RAM usage: ${label} (${rangeLabel(memoryBytes)})`}
    >
      <svg width="18" height="18" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
        <path d="M4 14a8 8 0 0 1 16 0" stroke="currentColor" strokeWidth="2" strokeLinecap="round"/>
        <path d="M12 14l4-5" stroke="currentColor" strokeWidth="2" strokeLinecap="round"/>
        <circle cx="12" cy="14" r="1.5" fill="currentColor"/>
        <path d="M6.5 14h-2M19.5 14h-2M7.8 8.8 6.4 7.4M16.2 8.8l1.4-1.4M12 6V4" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round"/>
      </svg>
      {label}
    </span>
  );
};

export default MemoryChip;
