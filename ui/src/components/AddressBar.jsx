import React, { useState } from 'react';

const AddressBar = ({ url: initialUrl, onNavigate }) => {
  const [url, setUrl] = useState(initialUrl);

  // Update local state when active tab changes
  React.useEffect(() => {
    setUrl(initialUrl);
  }, [initialUrl]);

  const handleKeyPress = (e) => {
    if (e.key === 'Enter') {
      onNavigate(url);
    }
  };

  return (
    <div className="flex flex-1 items-center h-7 bg-input-light dark:bg-input-dark rounded px-3 border border-transparent focus-within:border-brand-orange transition-all duration-200 mx-2">
      <input
        type="text"
        className="w-full bg-transparent border-none outline-none text-slate-900 dark:text-slate-100 text-xs placeholder-slate-400"
        value={url}
        onChange={(e) => setUrl(e.target.value)}
        onKeyPress={handleKeyPress}
        placeholder="Search or enter address..."
      />
    </div>
  );
};

export default AddressBar;
