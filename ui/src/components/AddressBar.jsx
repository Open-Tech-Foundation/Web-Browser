import React, { useState } from 'react';

const AddressBar = ({ onNavigate }) => {
  const [url, setUrl] = useState('https://www.google.com');

  const handleKeyPress = (e) => {
    if (e.key === 'Enter') {
      onNavigate(url);
    }
  };

  return (
    <div className="flex flex-1 items-center bg-input-light dark:bg-input-dark rounded-lg px-3 py-2 border border-transparent focus-within:border-brand-orange transition-all duration-200 shadow-sm mx-4">
      <input
        type="text"
        className="w-full bg-transparent border-none outline-none text-slate-900 dark:text-slate-100 text-sm placeholder-slate-400"
        value={url}
        onChange={(e) => setUrl(e.target.value)}
        onKeyPress={handleKeyPress}
        placeholder="Navigate with Open Tech Foundation..."
      />
      <button 
        className="bg-brand-orange text-white px-4 py-1.5 rounded-md font-bold text-sm cursor-pointer transition-all hover:brightness-110 active:scale-95" 
        onClick={() => onNavigate(url)}
      >
        Go
      </button>
    </div>
  );
};

export default AddressBar;
