import React from 'react';
import AddressBar from './components/AddressBar';
import './styles/App.css';

const App = () => {
  const handleNavigate = (url) => {
    let targetUrl = url;
    if (!targetUrl.startsWith('http')) {
      targetUrl = 'https://' + targetUrl;
    }

    if (window.cefQuery) {
      window.cefQuery({
        request: targetUrl,
        onSuccess: (response) => console.log("OTF Engine: Navigation started"),
        onFailure: (err) => console.error("OTF Engine Error: " + err)
      });
    } else {
      console.warn("CEF Engine not detected. URL:", targetUrl);
    }
  };

  return (
    <div className="h-[60px] bg-bar-light dark:bg-bar-dark flex items-center px-4 border-b border-slate-200 dark:border-slate-800 antialiased">
      <div className="font-black text-brand-orange text-xl tracking-tighter cursor-default select-none">
        OTF WB
      </div>
      <AddressBar onNavigate={handleNavigate} />
    </div>
  );
};

export default App;
