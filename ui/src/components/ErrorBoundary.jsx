import React from 'react';

class ErrorBoundary extends React.Component {
  constructor(props) {
    super(props);
    this.state = { hasError: false };
  }

  static getDerivedStateFromError() {
    return { hasError: true };
  }

  componentDidCatch(error) {
    console.error('OTF Browser Shell error:', error);
  }

  render() {
    if (this.state.hasError) {
      return (
        <div className="flex items-center justify-center h-[60px] bg-slate-100 dark:bg-slate-950 border-b border-slate-300 dark:border-slate-800">
          <span className="text-[11px] text-slate-500 dark:text-slate-400">
            UI shell error — restart the browser to recover
          </span>
        </div>
      );
    }
    return this.props.children;
  }
}

export default ErrorBoundary;
