import { Link } from "@opentf/web";

export default function NotFound() {
  return (
    <div className="flex items-center justify-center min-h-[70vh] px-6">
      <div className="relative max-w-lg w-full text-center">
        {/* Decorative Background Elements */}
        <div className="absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 w-64 h-64 blur-[120px] rounded-full pointer-events-none opacity-20" style={{ backgroundColor: 'var(--accent)' }}></div>
        
        <div className="relative z-10">
          <h1 className="text-9xl font-black mb-4 tracking-tighter opacity-10 select-none">
            404
          </h1>
          
          <div className="p-8 md:p-12 rounded-[40px] border backdrop-blur-xl transition-all duration-500 shadow-2xl" style={{ backgroundColor: 'var(--bg-card)', borderColor: 'var(--border)' }}>
            <div className="w-16 h-16 rounded-3xl bg-orange-500/10 flex items-center justify-center mx-auto mb-8 text-orange-500">
              <svg className="w-8 h-8" fill="none" viewBox="0 0 24 24" stroke="currentColor" strokeWidth="2.5">
                <path strokeLinecap="round" strokeLinejoin="round" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
              </svg>
            </div>
            
            <h2 className="text-3xl font-bold mb-4 tracking-tight" style={{ color: 'var(--foreground)' }}>
              Page Not Found
            </h2>
            
            <p className="text-lg leading-relaxed mb-10" style={{ color: 'var(--muted)' }}>
              The page you're looking for has moved, been deleted, or never existed in the first place.
            </p>
            
            <Link 
              href="/"
              className="inline-flex items-center gap-3 px-8 py-4 rounded-2xl font-bold text-white transition-all duration-300 hover:scale-[1.02] active:scale-[0.98] shadow-xl shadow-orange-500/20"
              style={{ backgroundColor: 'var(--accent)' }}
            >
              <span>Back to Home</span>
              <svg className="w-5 h-5" fill="none" viewBox="0 0 24 24" stroke="currentColor" strokeWidth="3">
                <path strokeLinecap="round" strokeLinejoin="round" d="M13 7l5 5m0 0l-5 5m5-5H6" />
              </svg>
            </Link>
          </div>
          
          <p className="mt-8 text-[10px] font-black uppercase tracking-[0.2em] opacity-40" style={{ color: 'var(--muted)' }}>
            OTF Browser Diagnostic Core
          </p>
        </div>
      </div>
    </div>
  );
}
