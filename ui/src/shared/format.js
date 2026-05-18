// Format a byte count for display next to a site's storage usage.
// Decimal (1 MB = 1_000_000) to match the units Chrome / Firefox surface
// in their site-data panels — closer to user expectations than the
// binary 1 MiB convention.
export const formatBytes = (bytes) => {
  if (bytes == null || !Number.isFinite(bytes)) return '—';
  if (bytes < 1) return '0 B';
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let i = 0;
  let n = bytes;
  while (n >= 1000 && i < units.length - 1) {
    n /= 1000;
    i += 1;
  }
  const precision = n >= 100 || i === 0 ? 0 : n >= 10 ? 1 : 2;
  return `${n.toFixed(precision)} ${units[i]}`;
};
