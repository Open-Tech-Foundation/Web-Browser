import React from 'react';

const SecurityIconButton = ({ insecure, onClick }) => {
  return (
    <button
      type="button"
      onMouseDown={(e) => e.preventDefault()}
      onClick={(e) => {
        e.preventDefault();
        e.stopPropagation();
        onClick?.();
      }}
      className={`absolute left-3 top-1/2 -translate-y-1/2 z-30 flex h-5 w-5 cursor-pointer items-center justify-center rounded-full transition-transform active:scale-90 hover:opacity-80 ${
        insecure ? 'text-red-500' : 'text-green-500'
      }`}
      aria-label={insecure ? 'Security warning' : 'Secure connection'}
      title={insecure ? 'Security warning' : 'Secure connection'}
    >
      <svg
        xmlns="http://www.w3.org/2000/svg"
        width="14"
        height="14"
        viewBox="0 0 24 24"
        fill="none"
        stroke="currentColor"
        strokeWidth="2.5"
        strokeLinecap="round"
        strokeLinejoin="round"
      >
        <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z" />
        {insecure ? (
          <>
            <line x1="12" y1="8" x2="12" y2="12" />
            <line x1="12" y1="16" x2="12.01" y2="16" />
          </>
        ) : (
          <polyline points="9 12 11 14 15 10" />
        )}
      </svg>
    </button>
  );
};

export default SecurityIconButton;
