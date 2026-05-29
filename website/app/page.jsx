import pkg from "../../package.json" with { type: "json" };

const GH_REPO = "Open-Tech-Foundation/Web-Browser";
const GH_TAG = "v" + pkg.version;

const downloadPlatforms = ["windows", "macos", "linux"];

const LINUX_INSTALL_SCRIPT_URL = `https://raw.githubusercontent.com/${GH_REPO}/main/scripts/install.sh`;
const ONE_LINER = `curl -fsSL ${LINUX_INSTALL_SCRIPT_URL} | sh`;

function copyOneLiner(e) {
  if (typeof navigator !== "undefined" && navigator.clipboard) {
    navigator.clipboard.writeText(ONE_LINER).then(() => {
      if (e?.target) {
        const btn = e.target;
        const orig = btn.textContent;
        btn.textContent = 'Copied!';
        setTimeout(() => { btn.textContent = orig; }, 2000);
      }
    }).catch(() => {
      if (e?.target) {
        const btn = e.target;
        const orig = btn.textContent;
        btn.textContent = 'Failed';
        setTimeout(() => { btn.textContent = orig; }, 2000);
      }
    });
  }
}



export default function HomePage() {
  let activeTab = $state("windows");
  return (
    <div className="flex flex-col">
      <section className="relative pt-20 pb-16 px-6 overflow-hidden">
        <div className="absolute top-0 left-1/2 -translate-x-1/2 w-[1000px] h-[600px] blur-[120px] rounded-full pointer-events-none opacity-10" style="background-color: var(--accent);"></div>
        <div className="absolute -bottom-32 right-0 w-[520px] h-[520px] blur-[100px] rounded-full pointer-events-none opacity-5" style="background-color: var(--accent);"></div>

        <div className="max-w-7xl mx-auto relative z-10 text-center">
          <div className="inline-flex items-center gap-2 px-4 py-2 rounded-full border text-xs font-bold uppercase tracking-widest mb-8" style="background-color: color-mix(in srgb, var(--accent) 10%, transparent); border-color: color-mix(in srgb, var(--accent) 20%, transparent); color: var(--accent);">
            Engineered for Privacy & Security
          </div>

          <h1 className="text-3xl md:text-5xl font-extrabold tracking-tight mb-8 leading-tight flex items-center justify-center gap-4" style="color: var(--foreground);">
            OTF <span className="bg-clip-text text-transparent" style="background-image: linear-gradient(to right, var(--accent), #fbbf24);">Browser</span>
            <span className="inline-flex px-2 py-0.5 rounded-lg border text-[10px] font-black uppercase tracking-[0.2em] translate-y-1" style="background-color: #a855f71a; border-color: #a855f74d; color: #a855f7;">Alpha</span>
          </h1>

          <p className="text-xl md:text-2xl max-w-3xl mx-auto mb-12 leading-relaxed" style="color: var(--muted);">
            A fast, privacy-focused browser with hardened security, built on top of the Chromium Embedded Framework.
          </p>

        </div>
      </section>

      {/* ── Download Section ──────────────────────────────────────────────── */}
      <section className="py-20 px-6 max-w-7xl mx-auto w-full">
        <div className="text-center mb-12">
          <h2 className="text-4xl font-extrabold mb-4 tracking-tight" style="color: var(--foreground);">Download OTF Browser</h2>
          <p className="max-w-2xl mx-auto text-lg leading-relaxed" style="color: var(--muted);">
            Choose your platform and get the latest build.
          </p>
        </div>

        {/* Tabs */}
        <div className="flex justify-center gap-2 mb-10">
          {["windows", "macos", "linux"].map((p) => (
              <button
                key={p}
                onClick={() => activeTab = p}
                className={`flex items-center gap-2.5 px-6 py-3 rounded-xl border text-sm font-bold transition-all duration-200 cursor-pointer ${activeTab === p ? "shadow-lg scale-105" : "opacity-60 hover:opacity-90"}`}
                style={{
                  backgroundColor: activeTab === p ? "color-mix(in srgb, var(--accent) 14%, transparent)" : "var(--bg-card)",
                  borderColor: activeTab === p ? "color-mix(in srgb, var(--accent) 40%, transparent)" : "var(--border)",
                  color: activeTab === p ? "var(--accent)" : "var(--foreground)",
                }}
              >
                {p === "windows" ? (
                  <svg className="w-5 h-5" viewBox="0 0 24 24" fill="currentColor"><path d="M0 3.449L9.75 2.1v9.451H0m10.25-9.602L24 0v11.4H10.25M0 12.6h9.75v9.451L0 20.699M10.25 12.6H24V24l-13.75-1.949" /></svg>
                ) : p === "macos" ? (
                  <svg className="w-5 h-5" viewBox="0 0 24 24" fill="currentColor"><path d="M17.05 20.28c-.98.95-2.05.8-3.08.35-1.09-.46-2.09-.48-3.24 0-1.44.62-2.2.44-3.06-.35C2.79 15.25 3.51 7.59 9.05 7.31c1.35.07 2.29.74 3.08.8 1.18-.24 2.31-.93 3.57-.84 1.51.12 2.65.72 3.4 1.8-3.12 1.87-2.38 5.98.48 7.13-.57 1.5-1.31 2.99-2.54 4.09zM12.03 7.25c-.15-2.23 1.66-4.07 3.74-4.25.29 2.58-2.34 4.5-3.74 4.25z" /></svg>
                ) : (
                  <svg className="w-5 h-5" viewBox="55 60 90 125" fill="currentColor" xmlns="http://www.w3.org/2000/svg"><ellipse cx="100" cy="115" rx="42" ry="55" fill="#000000"/><ellipse cx="100" cy="122" rx="28" ry="38" fill="#ffffff"/><circle cx="84" cy="78" r="7" fill="#ffffff"/><circle cx="116" cy="78" r="7" fill="#ffffff"/><circle cx="84" cy="78" r="3" fill="#000000"/><circle cx="116" cy="78" r="3" fill="#000000"/><polygon points="100,88 90,100 110,100" fill="#f59e0b"/><ellipse cx="78" cy="170" rx="18" ry="8" fill="#f59e0b"/><ellipse cx="122" cy="170" rx="18" ry="8" fill="#f59e0b"/></svg>
                )}
                {p === "windows" ? "Windows" : p === "macos" ? "macOS" : "Linux"}
              </button>
            ))}
        </div>

        {/* Tab Content */}
        <div className="max-w-2xl mx-auto">
          {/* Windows */}
          {activeTab === "windows" && (
            <div className="text-center p-10 rounded-[32px] border" style="background-color: var(--bg-card); border-color: var(--border);">
              <div className="w-16 h-16 mx-auto mb-6 rounded-2xl flex items-center justify-center" style="background-color: color-mix(in srgb, var(--accent) 12%, transparent);">
                <svg className="w-8 h-8" viewBox="0 0 24 24" fill="currentColor" style="color: var(--accent);"><path d="M0 3.449L9.75 2.1v9.451H0m10.25-9.602L24 0v11.4H10.25M0 12.6h9.75v9.451L0 20.699M10.25 12.6H24V24l-13.75-1.949" /></svg>
              </div>
              <h3 className="text-2xl font-bold mb-3" style="color: var(--foreground);">Windows</h3>
              <p className="mb-8 leading-relaxed" style="color: var(--muted);">
                Portable ZIP archive. Extract and run <code className="px-2 py-0.5 rounded text-sm font-mono" style="background-color: color-mix(in srgb, var(--foreground) 8%, transparent);">otf-browser.exe</code>.
              </p>
              <a
                href={`https://github.com/${GH_REPO}/releases/download/${GH_TAG}/otf-browser-windows-x64-${GH_TAG}.zip`}
                className="inline-flex items-center gap-2.5 px-8 py-3.5 rounded-xl text-base font-bold text-white transition-all duration-200 hover:-translate-y-0.5 hover:shadow-xl active:scale-[0.98]"
                style="background: linear-gradient(135deg, var(--accent), #f59e0b);"
              >
                <svg className="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4" /><polyline points="7 10 12 15 17 10" /><line x1="12" y1="15" x2="12" y2="3" /></svg>
                Download for Windows
              </a>
            </div>
          )}

          {/* macOS */}
          {activeTab === "macos" && (
            <div className="text-center p-10 rounded-[32px] border" style="background-color: var(--bg-card); border-color: var(--border);">
              <div className="w-16 h-16 mx-auto mb-6 rounded-2xl flex items-center justify-center" style="background-color: color-mix(in srgb, var(--accent) 12%, transparent);">
                <svg className="w-8 h-8" viewBox="0 0 24 24" fill="currentColor" style="color: var(--accent);"><path d="M17.05 20.28c-.98.95-2.05.8-3.08.35-1.09-.46-2.09-.48-3.24 0-1.44.62-2.2.44-3.06-.35C2.79 15.25 3.51 7.59 9.05 7.31c1.35.07 2.29.74 3.08.8 1.18-.24 2.31-.93 3.57-.84 1.51.12 2.65.72 3.4 1.8-3.12 1.87-2.38 5.98.48 7.13-.57 1.5-1.31 2.99-2.54 4.09zM12.03 7.25c-.15-2.23 1.66-4.07 3.74-4.25.29 2.58-2.34 4.5-3.74 4.25z" /></svg>
              </div>
              <h3 className="text-2xl font-bold mb-3" style="color: var(--foreground);">macOS</h3>
              <p className="mb-8 leading-relaxed" style="color: var(--muted);">
                macOS support is coming soon. Follow the project on GitHub for updates.
              </p>
              <div className="inline-flex items-center gap-2.5 px-8 py-3.5 rounded-xl text-base font-bold opacity-60 cursor-not-allowed" style="background-color: color-mix(in srgb, var(--foreground) 8%, transparent); color: var(--muted);">
                <svg className="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><rect x="3" y="11" width="18" height="11" rx="2" ry="2" /><path d="M7 11V7a5 5 0 0 1 10 0v4" /></svg>
                Coming Soon
              </div>
            </div>
          )}

          {/* Linux */}
          {activeTab === "linux" && (
            <div className="text-center p-10 rounded-[32px] border" style="background-color: var(--bg-card); border-color: var(--border);">
              <div className="w-16 h-16 mx-auto mb-6 rounded-2xl flex items-center justify-center" style="background-color: color-mix(in srgb, var(--accent) 12%, transparent);">
                <svg className="w-8 h-8" viewBox="55 60 90 125" fill="currentColor" style="color: var(--accent);" xmlns="http://www.w3.org/2000/svg"><ellipse cx="100" cy="115" rx="42" ry="55" fill="#000000"/><ellipse cx="100" cy="122" rx="28" ry="38" fill="#ffffff"/><circle cx="84" cy="78" r="7" fill="#ffffff"/><circle cx="116" cy="78" r="7" fill="#ffffff"/><circle cx="84" cy="78" r="3" fill="#000000"/><circle cx="116" cy="78" r="3" fill="#000000"/><polygon points="100,88 90,100 110,100" fill="#f59e0b"/><ellipse cx="78" cy="170" rx="18" ry="8" fill="#f59e0b"/><ellipse cx="122" cy="170" rx="18" ry="8" fill="#f59e0b"/></svg>
              </div>
              <h3 className="text-2xl font-bold mb-3" style="color: var(--foreground);">Linux</h3>

              {/* Option 1 — One-liner install */}
              <div className="mb-8 text-left max-w-xl mx-auto">
                <div className="flex items-center justify-between mb-2">
                  <span className="text-xs font-bold uppercase tracking-widest" style="color: var(--muted);">Quick Install (one-liner)</span>
                  <button
                    onClick={copyOneLiner}
                    className="text-[10px] font-bold uppercase tracking-wider px-2.5 py-1 rounded-lg border transition-colors cursor-pointer"
                    style="border-color: var(--border); color: var(--muted);"
                  >
                    Copy
                  </button>
                </div>
                <div className="rounded-xl border p-4 font-mono text-sm leading-relaxed overflow-x-auto" style="background-color: #0f172a; border-color: #1e293b; color: #e2e8f0;">
                  <span style="color: #22c55e;">$</span> curl -fsSL <span style="color: #f59e0b;">{LINUX_INSTALL_SCRIPT_URL}</span> | sh
                </div>
                <p className="mt-2 text-xs leading-relaxed" style="color: var(--muted);">
                  Auto-detects your distro and installs the correct package using apt, dnf, pacman, or falls back to AppImage.
                </p>
              </div>

              <div className="flex items-center gap-4 mb-8 max-w-md mx-auto">
                <div className="flex-1 h-px" style="background-color: var(--border);"></div>
                <span className="text-xs font-bold uppercase tracking-widest" style="color: var(--muted);">or download directly</span>
                <div className="flex-1 h-px" style="background-color: var(--border);"></div>
              </div>

              {/* Option 2 — Package links */}
              <div className="flex flex-wrap justify-center gap-3">
                <a
                  href={`https://github.com/${GH_REPO}/releases/download/${GH_TAG}/otf-browser_${pkg.version}_amd64.deb`}
                  className="inline-flex items-center gap-2 px-5 py-2.5 rounded-xl border text-sm font-bold transition-all hover:-translate-y-0.5"
                  style="background-color: color-mix(in srgb, var(--accent) 8%, transparent); border-color: color-mix(in srgb, var(--accent) 24%, transparent); color: var(--accent);"
                >
                  <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>
                  Debian / Ubuntu (.deb)
                </a>
                <a
                  href={`https://github.com/${GH_REPO}/releases/download/${GH_TAG}/otf-browser-${pkg.version}-1.x86_64.rpm`}
                  className="inline-flex items-center gap-2 px-5 py-2.5 rounded-xl border text-sm font-bold transition-all hover:-translate-y-0.5"
                  style="background-color: color-mix(in srgb, var(--accent) 8%, transparent); border-color: color-mix(in srgb, var(--accent) 24%, transparent); color: var(--accent);"
                >
                  <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>
                  Fedora / RHEL (.rpm)
                </a>
                <a
                  href={`https://github.com/${GH_REPO}/releases/download/${GH_TAG}/otf-browser-linux-x64-${GH_TAG}.AppImage`}
                  className="inline-flex items-center gap-2 px-5 py-2.5 rounded-xl border text-sm font-bold transition-all hover:-translate-y-0.5"
                  style="background-color: color-mix(in srgb, var(--accent) 8%, transparent); border-color: color-mix(in srgb, var(--accent) 24%, transparent); color: var(--accent);"
                >
                  <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>
                  AppImage
                </a>
                <a
                  href={`https://github.com/${GH_REPO}/releases/download/${GH_TAG}/otf-browser-linux-x64-${GH_TAG}.tar.gz`}
                  className="inline-flex items-center gap-2 px-5 py-2.5 rounded-xl border text-sm font-bold transition-all hover:-translate-y-0.5"
                  style="background-color: color-mix(in srgb, var(--accent) 8%, transparent); border-color: color-mix(in srgb, var(--accent) 24%, transparent); color: var(--accent);"
                >
                  <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>
                  Arch / Manjaro (tar.gz)
                </a>
              </div>

              <p className="mt-6 text-xs" style="color: var(--muted);">
                <a href={`https://github.com/${GH_REPO}/releases`} className="underline hover:text-[var(--accent)]">View all releases on GitHub</a>
              </p>
            </div>
          )}
        </div>
      </section>

      <section className="py-24 px-6 max-w-7xl mx-auto w-full">
        <div className="text-center mb-16">
          <h2 className="text-4xl font-extrabold mb-4 tracking-tight" style="color: var(--foreground);">Unique Features</h2>
          <p className="max-w-2xl mx-auto text-lg leading-relaxed" style="color: var(--muted);">
            Discover the innovative tools built exclusively for OTF Browser.
          </p>
        </div>

        <div className="grid grid-cols-1 md:grid-cols-3 gap-8">
          {[
            {
              title: "Intelligent Tab Strip",
              desc: "Real-time counters and attention animations for hidden tabs provide the most intuitive multi-tab navigation experience.",
              icon: <svg className="w-6 h-6" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M2 12h20M2 5h20M2 19h20" /></svg>
            },
            {
              title: "WebGPU Compute Protection",
              desc: "Prevents GPU resource abuse by automatically blocking non-graphical WebGPU compute workloads, stopping stealth crypto mining and hardware strain.",
              icon: <svg className="w-6 h-6" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><rect x="2" y="4" width="20" height="16" rx="2" /><path d="M7 8h10M7 12h10M7 16h10" /></svg>
            },
            {
              title: "Native Image Preview",
              desc: "Supports common image formats, including TIFF and multi-page TIFFs.",
              icon: <svg className="w-6 h-6" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"></rect><circle cx="8.5" cy="8.5" r="1.5"></circle><polyline points="21 15 16 10 5 21"></polyline></svg>,
              badges: ["TIFF", "Multi-page TIFF"],
              emphasize: true,
            },
            {
              title: "Built-in Document Viewer",
              desc: "Open PDFs, CSVs, JSON, Markdown, and code files directly in the browser with syntax highlighting and interactive table previews.",
              icon: <svg className="w-6 h-6" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" /><polyline points="14 2 14 8 20 8" /><line x1="16" y1="13" x2="8" y2="13" /><line x1="16" y1="17" x2="8" y2="17" /><polyline points="10 9 9 9 8 9" /></svg>,
              badges: ["PDF", "CSV", "JSON", "Markdown", "Code"],
              emphasize: true,
            },
            {
              title: "Privacy-First QR Sharing",
              desc: "Generate QR codes for any page directly from the address bar. UTM tracking parameters are automatically stripped before the code is generated, so you share clean links.",
              icon: <svg className="w-6 h-6" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><rect x="3" y="3" width="7" height="7" rx="1" /><rect x="14" y="3" width="7" height="7" rx="1" /><rect x="3" y="14" width="7" height="7" rx="1" /><rect x="14" y="14" width="3" height="3" /><path d="M14 17h3v4" /><path d="M20 14v3" /></svg>,
              badges: ["UTM stripped", "Copy & Download"],
            }
          ].map((feature) => (
            <div
              key={feature.title}
              className={`p-8 rounded-[32px] border transition-all duration-500 hover:-translate-y-2 group overflow-hidden relative ${feature.emphasize ? "hover:border-orange-400/50 shadow-[0_24px_80px_rgba(249,115,22,0.10)]" : "hover:border-orange-500/30"
                }`}
              style="background-color: var(--bg-card); border-color: var(--border);"
            >
              {feature.emphasize && (
                <div
                  className="absolute inset-0 opacity-60 pointer-events-none"
                  style="background: radial-gradient(circle at top right, rgba(249,115,22,0.18), transparent 45%), radial-gradient(circle at bottom left, rgba(251,191,36,0.10), transparent 38%);"
                ></div>
              )}
              <div className="relative z-10">
                <div className={`w-12 h-12 rounded-2xl flex items-center justify-center mb-6 text-orange-500 transition-transform ${feature.emphasize ? "bg-orange-500/15 shadow-[0_0_0_1px_rgba(249,115,22,0.18)] group-hover:scale-110" : "bg-orange-500/10 group-hover:scale-110"}`}>
                  {feature.icon}
                </div>
                <h3 className="text-xl font-bold mb-4" style="color: var(--foreground);">{feature.title}</h3>
                <p className="text-sm leading-relaxed mb-5" style="color: var(--muted);">{feature.desc}</p>

                {feature.badges && (
                  <div className="flex flex-wrap gap-2 mb-5">
                    {feature.badges.map((badge) => (
                      <span
                        key={badge}
                        className="inline-flex items-center rounded-full border px-3 py-1 text-[10px] font-black uppercase tracking-[0.18em]"
                        style="background-color: color-mix(in srgb, var(--accent) 10%, transparent); border-color: color-mix(in srgb, var(--accent) 24%, transparent); color: var(--accent);"
                      >
                        {badge}
                      </span>
                    ))}
                  </div>
                )}
              </div>
            </div>
          ))}
        </div>
      </section>

      <section className="py-28 px-6 max-w-7xl mx-auto w-full">
        <div className="mb-16">
          <h2 className="text-4xl font-bold mb-6" style="color: var(--foreground);">Privacy & Security Features in the Browser</h2>
          <p className="max-w-3xl leading-relaxed text-lg" style="color: var(--muted);">
            OTF Browser bakes privacy and security into every layer — from kernel-level scheme blocking and WebGPU compute prevention to per-session fingerprint rotation and full certificate transparency. No extensions required, no toggles to hunt for.
          </p>
        </div>

        {/* ── Always-On Privacy Protections ──────────────────────────────── */}
        <div className="mb-16">
          <div className="flex items-center gap-3 mb-10">
            <div className="w-10 h-10 rounded-2xl flex items-center justify-center" style="background-color: color-mix(in srgb, var(--accent) 12%, transparent);">
              <svg className="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style="color: var(--accent);"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/></svg>
            </div>
            <h3 className="text-2xl font-bold" style="color: var(--foreground);">Always-On Privacy Protections</h3>
          </div>

          <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
            {[
              {
                name: "Canvas Fingerprint Protection",
                plain: "Prevents websites from generating a unique device ID by reading subtle differences in how your browser renders graphics.",
                tech: "Injects per-session noise into canvas read methods: getImageData, toDataURL, toBlob, OffscreenCanvas.convertToBlob, and transferToImageBitmap. Each page load produces a fresh canvas fingerprint, making cross-session tracking impossible.",
              },
              {
                name: "WebGL Identity Protection",
                plain: "Hides your GPU model, driver version, and graphics capabilities so trackers cannot identify your hardware.",
                tech: "Normalizes 40+ WebGL parameters (renderer, vendor, shader precision, max texture size) to fixed generic values. Blocks WEBGL_debug_renderer_info extension. Perturbs readPixels output with per-session noise.",
              },
              {
                name: "Font Probing Protection",
                plain: "Stops websites from scanning your system fonts to build a unique device fingerprint.",
                tech: "Limits document.fonts.check(), load(), and forEach() to 4 allowed fonts (Arial, Helvetica, Times New Roman, Courier New). Normalizes canvas measureText widths. Applies per-session noise to DOM font metrics (getBoundingClientRect, offsetWidth/Height).",
              },
              {
                name: "Audio Fingerprint Protection",
                plain: "Prevents websites from using subtle differences in your audio stack to identify your device.",
                tech: "Perturbs AudioBuffer.getChannelData, AnalyserNode.getFloatFrequencyData and getByteFrequencyData with per-origin deterministic noise. Audio output still works — only the fingerprint signal is disrupted.",
              },
              {
                name: "Math Function Fingerprinting Protection",
                plain: "Blocks a sophisticated technique where websites use tiny rounding errors in math functions to identify your CPU and browser.",
                tech: "Adds per-(session, function, input) deterministic noise (~1e-13) to 22 Math functions including sin, cos, tan, log, sqrt, pow, and atan2. Values rotate across sessions to prevent cross-session correlation.",
              },
              {
                name: "Screen & Hardware Profile Normalization",
                plain: "Reports a standard screen size, CPU count, and memory amount instead of your real device specs.",
                tech: "Fixes screen dimensions to a common profile (e.g., 1920x1080@1x), hardwareConcurrency to 6, deviceMemory to 16 GB, navigator.platform to 'Linux x86_64', and screen.colorDepth to 24. Sec-CH-UA-Platform forced to 'Linux' at the CEF level.",
              },
              {
                name: "Media Devices Spoofing",
                plain: "Shows a fixed set of generic microphone, camera, and speaker devices instead of your actual hardware labels.",
                tech: "Returns exactly 3 fixed devices (audioinput, audiooutput, videoinput) with standardized groupIds and capabilities. Real device labels are never exposed. Speaker output still works — verified by the built-in test tone.",
              },
              {
                name: "API Surface Reduction",
                plain: "Removes or disables browser APIs that are commonly abused for tracking, such as battery status, network type, and keyboard layout.",
                tech: "Completely removes navigator.getBattery and BatteryManager, navigator.connection and prefixed variants, navigator.keyboard and KeyboardLayoutMap. Also removes navigator.serviceWorker at the engine level and via page policy. speechSynthesis.getVoices() returns an empty array.",
              },
            ].map((feature) => (
              <div
                key={feature.name}
                className="p-6 rounded-2xl border transition-all duration-300"
                style="background-color: var(--bg-card); border-color: var(--border);"
              >
                <div className="flex items-center gap-3 mb-3">
                  <span className="text-[10px] font-black uppercase tracking-widest px-2 py-0.5 rounded-md" style="background-color: color-mix(in srgb, var(--accent) 10%, transparent); color: var(--accent);">Always On</span>
                </div>
                <h4 className="text-lg font-bold mb-2" style="color: var(--foreground);">{feature.name}</h4>
                <p className="text-sm leading-relaxed mb-3" style="color: var(--muted);">{feature.plain}</p>
                <details className="group">
                  <summary className="text-[11px] font-bold uppercase tracking-wider cursor-pointer transition-colors" style="color: var(--accent);">Technical detail</summary>
                  <p className="mt-3 text-xs leading-relaxed" style="color: var(--muted);">{feature.tech}</p>
                </details>
              </div>
            ))}
          </div>
        </div>

        {/* ── Security Protections ──────────────────────────────────────── */}
        <div className="mb-16">
          <div className="flex items-center gap-3 mb-10">
            <div className="w-10 h-10 rounded-2xl flex items-center justify-center" style="background-color: color-mix(in srgb, var(--accent) 12%, transparent);">
              <svg className="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style="color: var(--accent);"><rect x="3" y="11" width="18" height="11" rx="2" ry="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/></svg>
            </div>
            <h3 className="text-2xl font-bold" style="color: var(--foreground);">Security Protections</h3>
          </div>

          <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
            {[
              {
                name: "Dangerous Scheme Blocking",
                plain: "Blocks navigation URLs that can expose local files, internal browser pages, or execute scripts from the address bar.",
                tech: "Blocks chrome://, chrome-devtools://, chrome-extension://, chrome-search://, chrome-untrusted://, devtools://, javascript:, data:, and file:// (except trusted UI shell paths). Also blocks javascript: and data: URLs in iframe src attributes and setAttribute calls.",
              },
              {
                name: "WebGPU Compute Pipeline Blocking",
                plain: "Allows modern graphics features while blocking crypto-mining and GPU abuse through compute pipelines.",
                tech: "WebGPU graphics pipeline is enabled for compatibility. Compute pipeline creation throws with 'compute pipelines are disabled'. Policy is verified via __otfWebGPUPolicyState, __otfGPUPolicy, and __otfWebGPUComputePolicy markers on prototype chains.",
              },
              {
                name: "HTTPS-Only Mode",
                plain: "Every navigation is upgraded to HTTPS automatically. If a site does not support HTTPS, you are warned before loading.",
                tech: "Upgrades all navigations to HTTPS via CEF request handler. HTTP navigations are blocked and redirected to browser://insecure-blocked with the original URL shown. Blocked page warns about password, message, and credit card exposure risks.",
              },
              {
                name: "Pop-Up & Download Manager",
                plain: "Pop-ups and automatic downloads require your permission before they can open or save files.",
                tech: "Pop-up attempts fire a popup-blocked event showing the requesting origin with Block / Allow once / Always allow options. Download requests show a similar prompt with the filename and origin. Permissions are persisted per origin.",
              },
              {
                name: "Service Workers Disabled",
                plain: "Prevents background scripts that can persist across sessions, cache sensitive data, and enable push-based tracking.",
                tech: "Service Worker API disabled at the CEF engine level via --disable-features=ServiceWorker and reinforced by page policy removal of navigator.serviceWorker. Constructor interface objects on Window.prototype remain present but are inert.",
              },
            ].map((feature) => (
              <div
                key={feature.name}
                className="p-6 rounded-2xl border transition-all duration-300"
                style="background-color: var(--bg-card); border-color: var(--border);"
              >
                <div className="flex items-center gap-3 mb-3">
                  <span className="text-[10px] font-black uppercase tracking-widest px-2 py-0.5 rounded-md" style="background-color: color-mix(in srgb, #22c55e 12%, transparent); color: #22c55e;">Always On</span>
                </div>
                <h4 className="text-lg font-bold mb-2" style="color: var(--foreground);">{feature.name}</h4>
                <p className="text-sm leading-relaxed mb-3" style="color: var(--muted);">{feature.plain}</p>
                <details className="group">
                  <summary className="text-[11px] font-bold uppercase tracking-wider cursor-pointer transition-colors" style="color: var(--accent);">Technical detail</summary>
                  <p className="mt-3 text-xs leading-relaxed" style="color: var(--muted);">{feature.tech}</p>
                </details>
              </div>
            ))}
          </div>
        </div>

        {/* ── User-Controlled Privacy ─────────────────────────────────────── */}
        <div>
          <div className="flex items-center gap-3 mb-10">
            <div className="w-10 h-10 rounded-2xl flex items-center justify-center" style="background-color: color-mix(in srgb, var(--accent) 12%, transparent);">
              <svg className="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style="color: var(--accent);"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg>
            </div>
            <h3 className="text-2xl font-bold" style="color: var(--foreground);">User-Controlled Privacy</h3>
          </div>

          <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
            {[
              {
                name: "Browsing & Download History Toggles",
                plain: "Turn history recording on or off. When disabled, your browsing and download activity is not persisted between sessions.",
                tech: "Settings stored in browser JSON config. History persistence follows the toggle: when disabled, web and download history are session-only and cleared on exit. Accessible from browser://settings Privacy panel.",
              },
              {
                name: "Private Tabs & Guest Sessions",
                plain: "Browse without leaving any trace. Private tabs use ephemeral storage. Guest sessions fully isolate bookmarks, history, and permissions.",
                tech: "Private tabs use a separate CEF request context with in-memory-only storage. Guest sessions lock bookmarks, browsing history, downloads, workspace switching, and site permissions. Closing the last guest tab ends the session automatically.",
              },
              {
                name: "Per-Site Data Manager",
                plain: "See exactly what cookies, storage, and permissions a website has — and clear them individually.",
                tech: "browser://sitedata shows a full cookie table (name, value, domain, path, secure, httponly), storage usage breakdown by type, permission toggles (pop-ups, downloads, images, JavaScript), and a cross-origin resource list showing third-party trackers the site loaded.",
              },
              {
                name: "TLS Certificate Viewer",
                plain: "Inspect the security certificate of any website: who issued it, who it was issued to, and when it expires.",
                tech: "Opens as a side panel from the address bar security icon. Shows Common Name, Organization, Issuer CA, validity start and expiry dates. Green banner for valid certificates, red warning for invalid or untrusted ones. Data fetched via C++ get-certificate-by-tab-id query.",
              },
              {
                name: "Browser Data Reset",
                plain: "Clear selected categories of your browsing data — from history and cookies to SSL exceptions and cached files — in one go.",
                tech: "Reset dialog in browser://settings lets you selectively clear: settings, history, bookmarks, SSL exceptions, HTTP auth, connections, cache, cookies, service workers, site permissions, and web storage. After reset, a restart button appears.",
              },
              {
                name: "Protection Diagnostics Portal",
                plain: "Verify your browser's privacy defenses are working with a built-in test center that checks every protected surface.",
                tech: "browser://settings links to browser.opentechf.org/protection — a comprehensive test suite that probes all 41+ protected surfaces (canvas, WebGL, fonts, audio, realm coverage, etc.) and scores them from 0-100. Results can be exported as a JSON report.",
              },
            ].map((feature) => (
              <div
                key={feature.name}
                className="p-6 rounded-2xl border transition-all duration-300"
                style="background-color: var(--bg-card); border-color: var(--border);"
              >
                <div className="flex items-center gap-3 mb-3">
                  <span className="text-[10px] font-black uppercase tracking-widest px-2 py-0.5 rounded-md" style="background-color: color-mix(in srgb, #3b82f6 12%, transparent); color: #3b82f6;">User Controlled</span>
                </div>
                <h4 className="text-lg font-bold mb-2" style="color: var(--foreground);">{feature.name}</h4>
                <p className="text-sm leading-relaxed mb-3" style="color: var(--muted);">{feature.plain}</p>
                <details className="group">
                  <summary className="text-[11px] font-bold uppercase tracking-wider cursor-pointer transition-colors" style="color: var(--accent);">Technical detail</summary>
                  <p className="mt-3 text-xs leading-relaxed" style="color: var(--muted);">{feature.tech}</p>
                </details>
              </div>
            ))}
          </div>
        </div>
      </section>

    </div>
  );
}
