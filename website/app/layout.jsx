import { Link, signal, onMount } from "@opentf/web";

const theme = signal("system");

export default function RootLayout({ children }) {
  onMount(() => {
    const savedTheme = localStorage.getItem("otf-theme") || "system";
    theme.value = savedTheme;
    applyTheme(savedTheme);
  });

  const applyTheme = (val) => {
    const root = document.documentElement;
    if (val === "dark") {
      root.classList.add("dark");
      root.classList.remove("light");
    } else if (val === "light") {
      root.classList.add("light");
      root.classList.remove("dark");
    } else {
      root.classList.remove("dark", "light");
    }
  };

  const toggleTheme = () => {
    const next = theme.value === "dark" ? "light" : "dark";
    theme.value = next;
    localStorage.setItem("otf-theme", next);
    applyTheme(next);
  };

  return (
    <div className="min-h-screen font-sans selection:bg-orange-500/30 transition-colors duration-300" style="background-color: var(--background); color: var(--foreground);">
      {/* Navbar */}
      <nav className="fixed top-0 left-0 right-0 h-14 border-b z-50 transition-colors duration-300" style="background-color: var(--background); border-color: var(--border); backdrop-filter: blur(20px);">
        <div className="max-w-7xl mx-auto h-full px-6 flex items-center justify-between">
          <Link href="/" className="flex items-center gap-3 group">
            <div className="w-8 h-8 transition-transform group-hover:scale-105 duration-300">
              <img src="/logo.svg" alt="Logo" className="w-full h-full" />
            </div>
            <span className="text-xl font-bold tracking-tight">OTF <span style="color: var(--accent);">Browser</span></span>
          </Link>

          <div className="flex items-center gap-6">
            <div className="hidden md:flex items-center gap-8 text-sm font-medium" style="color: var(--muted);">
              <Link href="/protection" className="hover:text-[var(--foreground)] transition-colors">Protection Tests</Link>
            </div>

            <button 
              onClick={toggleTheme}
              className="p-2 rounded-lg hover:bg-neutral-500/10 transition-colors"
              title="Toggle Theme"
            >
              {theme.value === "dark" ? (
                <svg className="w-5 h-5 text-amber-400" fill="none" viewBox="0 0 24 24" stroke="currentColor" strokeWidth="2">
                  <path strokeLinecap="round" strokeLinejoin="round" d="M12 3v1m0 16v1m9-9h-1M4 12H3m15.364-6.364l-.707.707M6.343 17.657l-.707.707m12.728 0l-.707-.707M6.343 6.343l-.707.707m12.728 12.728L5.121 5.121M19 12a7 7 0 11-14 0 7 7 0 0114 0z" />
                </svg>
              ) : (
                <svg className="w-5 h-5 text-neutral-600" fill="none" viewBox="0 0 24 24" stroke="currentColor" strokeWidth="2">
                  <path strokeLinecap="round" strokeLinejoin="round" d="M20.354 15.354A9 9 0 018.646 3.646 9.003 9.003 0 0012 21a9.003 9.003 0 008.354-5.646z" />
                </svg>
              )}
            </button>
          </div>
        </div>
      </nav>

      {/* Main Content */}
      <main className="pt-14">
        {children}
      </main>

      {/* Footer */}
      <footer className="py-10 border-t mt-10 transition-colors duration-300" style="background-color: #020617; border-color: #1e293b;">
        <div className="max-w-7xl mx-auto px-6 flex flex-col md:flex-row justify-between items-center gap-6 text-[10px] font-bold uppercase tracking-widest" style="color: #94a3b8;">
          <div className="flex flex-col gap-4 items-center md:items-start">
            <p>© 2026 Open Tech Foundation</p>
            <div className="flex items-center gap-2 text-[10px] font-black">
              <span className="text-white/40 uppercase tracking-widest">Built with</span>
              <a href="https://web.opentechf.org/" target="_blank" rel="noopener noreferrer" className="px-3 py-1 rounded-full border bg-white/5 border-white/10 text-white shadow-xl shadow-orange-500/10 transition-all hover:border-orange-500/30 group">
                <span className="bg-gradient-to-r from-orange-400 to-amber-300 bg-clip-text text-transparent">OTF Web Framework</span>
              </a>
            </div>
          </div>
          <a href="https://github.com/Open-Tech-Foundation/Web-Browser" className="hover:text-[var(--accent)] transition-colors">GitHub</a>
        </div>
      </footer>
    </div>
  );
}
