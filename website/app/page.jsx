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

const privacySecurityRows = [
  {
    area: "Scheme blocking",
    detail: "Unsafe/internal schemes",
    rationale: "Blocks address-bar vectors that can expose local files, internal engine pages, extension APIs, or script/data spoofing.",
    otf: [
      "Blocks chrome://",
      "Blocks chrome-devtools://",
      "Blocks chrome-extension://",
      "Blocks chrome-search://",
      "Blocks chrome-untrusted://",
      "Blocks devtools://",
      "Blocks javascript:",
      "Blocks data:",
      "Blocks file:// except trusted UI shell paths",
    ],
    mainstream: "Blocks or restricts many dangerous schemes, but rules are browser/vendor-specific.",
    modern: "Generally restricted; exact behavior differs across privacy browsers.",
  },
  {
    area: "Search and navigation",
    detail: "Address bar resolver",
    rationale: "Separates URL navigation from search so dotted search terms and real hostnames do not silently go down the wrong path.",
    otf: "Browser-owned resolver with DNS-backed host decisions and clean search query encoding.",
    mainstream: "Mature omnibox behavior, usually tied to vendor defaults.",
    modern: "Usually customizable, but behavior varies by product.",
  },
  {
    area: "History privacy",
    detail: "Persistence defaults",
    rationale: "Browsing history is sensitive and should not persist unless the user explicitly enables it.",
    otf: "Web history persistence follows saved privacy settings and defaults to session-only when disabled.",
    mainstream: "History persistence is usually enabled by default.",
    modern: "Varies; Tor is ephemeral, Brave/others usually persist unless configured.",
  },
  {
    area: "Download privacy",
    detail: "Download history",
    rationale: "Downloaded filenames and origins reveal user activity and should follow explicit user preference.",
    otf: "Download history persistence follows saved privacy settings.",
    mainstream: "Download history is usually persisted by default.",
    modern: "Varies by browser and private mode.",
  },
  {
    area: "Reset controls",
    detail: "Browser data clearing",
    rationale: "Reset must clear selected data categories predictably without exposing implementation noise to lay users.",
    otf: [
      "Settings",
      "History",
      "Bookmarks",
      "SSL exceptions",
      "HTTP auth",
      "Connections",
      "Cache",
      "Cookies",
    ],
    mainstream: "Comprehensive reset and clear-data tools.",
    modern: "Usually strong clear-data tools, with product-specific categories.",
  },
  {
    area: "SSL errors",
    detail: "Security indicator",
    rationale: "The page security icon must reflect the original certificate state, even when a user previously allowed an exception.",
    otf: "Tracks certificate error state per tab and avoids turning known SSL-error pages green after remembered allow actions.",
    mainstream: "Mature certificate-error and exception handling.",
    modern: "Generally mature; privacy browsers may restrict exceptions more aggressively.",
  },
  {
    area: "Certificate viewer",
    detail: "Current tab certificate",
    rationale: "Users should be able to inspect who a certificate is issued to, who issued it, and its validity window.",
    otf: [
      "Issued to",
      "Issued by",
      "Validity period",
      "Certificate-error status",
    ],
    mainstream: "Available in page/security information UI.",
    modern: "Usually available, with different levels of detail.",
  },
  {
    area: "HTTP blocking",
    detail: "Insecure navigation",
    rationale: "Plain HTTP is vulnerable to interception and downgrade attacks.",
    otf: "Blocks insecure HTTP according to browser security policy and marks blocked pages as insecure.",
    mainstream: "Usually warns, upgrades, or blocks depending on HTTPS-only settings.",
    modern: "Often stronger HTTPS-first or HTTPS-only modes.",
  },
  {
    area: "WebGPU",
    detail: "Graphics",
    rationale: "Graphics support preserves modern site compatibility without enabling high-risk compute workloads.",
    otf: "Graphics pipeline enabled.",
    mainstream: "Enabled where hardware and platform support exist.",
    modern: "Varies; often enabled or configurable.",
  },
  {
    area: "WebGPU",
    detail: "Compute",
    rationale: "Compute pipelines can amplify crypto-mining, high-intensity background workloads, and fingerprinting surface.",
    otf: "Compute pipeline access blocked by standard browser policy.",
    mainstream: "Usually enabled where WebGPU is enabled.",
    modern: "Varies; may be enabled, disabled, or protected by policy.",
  },
  {
    area: "WebGL",
    detail: "Vendor and renderer",
    rationale: "Raw GPU vendor/renderer details are high-value fingerprinting signals.",
    otf: "Normalizes exposed WebGL identity values.",
    mainstream: "Often exposes device-specific or engine-specific GPU details.",
    modern: "Often reduced, randomized, or permission-gated depending on product.",
  },
  {
    area: "WebGL",
    detail: "Capability limits",
    rationale: "Large GPU capability matrices create stable hardware fingerprints.",
    otf: "Uses a normalized capability profile for sensitive reported values.",
    mainstream: "Typically exposes real hardware/driver limits.",
    modern: "Varies; some reduce or standardize values.",
  },
  {
    area: "WebGL",
    detail: "Debug extensions",
    rationale: "Debug shader and driver extensions can reveal extra implementation details.",
    otf: "Reduces sensitive WebGL extension exposure as part of fingerprint policy.",
    mainstream: "Often exposes broad extension lists.",
    modern: "Varies; privacy-focused browsers commonly reduce this surface.",
  },
  {
    area: "Canvas",
    detail: "Readback and export",
    rationale: "Canvas readback can produce stable per-device hashes from rendering differences.",
    otf: "Applies canvas fingerprint protection to readback/export paths.",
    mainstream: "Usually exposed directly in normal browsing.",
    modern: "Often protected, randomized, or permission-gated.",
  },
  {
    area: "Frames",
    detail: "Main frame and iframes",
    rationale: "Fingerprint scripts commonly run inside embedded frames to bypass top-level checks.",
    otf: "Policy injection covers page frames and diagnostic reporting tracks coverage.",
    mainstream: "APIs generally available to frames subject to normal permissions.",
    modern: "Varies; stronger browsers restrict more surfaces.",
  },
  {
    area: "Service Workers",
    detail: "Registration and persistent realm",
    rationale: "Service Workers run in a separate realm that bypasses inline page policy injection and can persist cached responses, background sync, and push subscriptions across sessions — amplifying tracking surface beyond the page lifetime.",
    otf: "Service Worker API disabled at the engine level and via page policy. navigator.serviceWorker is removed — no registration path exists. Constructor interface objects on Window.prototype remain present but are inert.",
    mainstream: "Fully enabled by default; essential for PWA and offline-first apps.",
    modern: "Usually enabled; some privacy modes restrict background sync or push but retain the API.",
  },
  {
    area: "Workers",
    detail: [
      "Classic worker",
      "Module worker",
      "Shared worker",
      "Nested worker",
    ],
    rationale: "Workers are a common off-main-thread fingerprinting and compute path.",
    otf: "Policy and diagnostics target worker execution paths and expose remaining coverage gaps.",
    mainstream: "Workers generally receive the same powerful APIs when available.",
    modern: "Varies; some reduce APIs in privacy modes.",
  },
  {
    area: "Worklets",
    detail: [
      "Audio worklet",
      "Paint worklet",
      "Layout worklet",
      "Animation worklet",
    ],
    rationale: "Worklets are separate execution worlds and must be tracked as independent policy surfaces.",
    otf: "Fingerprint diagnostics report worklet capability presence and coverage.",
    mainstream: "Usually available where supported by engine.",
    modern: "Varies by engine and privacy mode.",
  },
  {
    area: "Fingerprint diagnostics",
    detail: "Public test ground",
    rationale: "Privacy claims should be testable and visible rather than hidden in implementation details.",
    otf: [
      "Applied patches",
      "Canvas behavior",
      "WebGL values",
      "WebGPU compute blocking",
      "Frame coverage",
      "Worker coverage",
      "User-agent string",
    ],
    mainstream: "Usually no centralized built-in fingerprint proof page.",
    modern: "Some expose protections, but diagnostics are often partial or external.",
  },
  {
    area: "User agent",
    detail: "Branding and platform",
    rationale: "UA should identify the browser without accidentally pushing sites into mobile or unsupported layouts.",
    otf: [
      "Linux desktop string",
      "Windows desktop string",
      "macOS desktop string",
    ],
    mainstream: "Mature desktop UA and client-hints behavior.",
    modern: "Usually desktop-compatible, with varying anti-fingerprint strategies.",
  },
];

function renderCellContent(value) {
  if (Array.isArray(value)) {
    return (
      <ul className="space-y-1.5">
        {value.map((item) => (
          <li key={item} className="flex gap-2 leading-relaxed">
            <span className="mt-2 h-1 w-1 rounded-full shrink-0" style="background-color: var(--accent); opacity: 0.7;"></span>
            <span>{item}</span>
          </li>
        ))}
      </ul>
    );
  }

  return value;
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
              <div className="w-16 h-16 mx-auto mb-6 rounded-2xl flex items-center justify-center" style="background-color: color-mix(in srgb, #a855f7 12%, transparent);">
                <svg className="w-8 h-8" viewBox="0 0 24 24" fill="currentColor" style="color: #a855f7;"><path d="M17.05 20.28c-.98.95-2.05.8-3.08.35-1.09-.46-2.09-.48-3.24 0-1.44.62-2.2.44-3.06-.35C2.79 15.25 3.51 7.59 9.05 7.31c1.35.07 2.29.74 3.08.8 1.18-.24 2.31-.93 3.57-.84 1.51.12 2.65.72 3.4 1.8-3.12 1.87-2.38 5.98.48 7.13-.57 1.5-1.31 2.99-2.54 4.09zM12.03 7.25c-.15-2.23 1.66-4.07 3.74-4.25.29 2.58-2.34 4.5-3.74 4.25z" /></svg>
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
                  Debian / Ubuntu (.deb)
                </a>
                <a
                  href={`https://github.com/${GH_REPO}/releases/download/${GH_TAG}/otf-browser-${pkg.version}-1.x86_64.rpm`}
                  className="inline-flex items-center gap-2 px-5 py-2.5 rounded-xl border text-sm font-bold transition-all hover:-translate-y-0.5"
                  style="background-color: color-mix(in srgb, var(--accent) 8%, transparent); border-color: color-mix(in srgb, var(--accent) 24%, transparent); color: var(--accent);"
                >
                  Fedora / RHEL (.rpm)
                </a>
                <a
                  href={`https://github.com/${GH_REPO}/releases/download/${GH_TAG}/otf-browser-linux-x64-${GH_TAG}.AppImage`}
                  className="inline-flex items-center gap-2 px-5 py-2.5 rounded-xl border text-sm font-bold transition-all hover:-translate-y-0.5"
                  style="background-color: color-mix(in srgb, var(--accent) 8%, transparent); border-color: color-mix(in srgb, var(--accent) 24%, transparent); color: var(--accent);"
                >
                  AppImage
                </a>
                <a
                  href={`https://github.com/${GH_REPO}/releases/download/${GH_TAG}/otf-browser-linux-x64-${GH_TAG}.tar.gz`}
                  className="inline-flex items-center gap-2 px-5 py-2.5 rounded-xl border text-sm font-bold transition-all hover:-translate-y-0.5"
                  style="background-color: color-mix(in srgb, var(--accent) 8%, transparent); border-color: color-mix(in srgb, var(--accent) 24%, transparent); color: var(--accent);"
                >
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
        <div className="mb-12">
          <h2 className="text-4xl font-bold mb-6" style="color: var(--foreground);">Privacy & Security Comparison</h2>
          <p className="max-w-3xl leading-relaxed" style="color: var(--muted);">
            A practical comparison of OTF Browser against mainstream browsers such as Chrome, Firefox, and Safari, and modern privacy-focused browsers such as Tor, Brave, Zen, Arc, Orion, Sigma, and Vivaldi.
          </p>
        </div>

        <div className="overflow-x-auto rounded-3xl border shadow-2xl transition-colors" style="background-color: var(--bg-card); border-color: var(--border);">
          <table className="w-full min-w-[1180px] text-left border-collapse">
            <thead>
              <tr className="border-b" style="background-color: color-mix(in srgb, var(--foreground) 3%, transparent); border-color: var(--border);">
                <th className="p-5 text-[11px] font-black uppercase tracking-widest" style="color: var(--muted);">Area</th>
                <th className="p-5 text-[11px] font-black uppercase tracking-widest" style="color: var(--muted);">Detail</th>
                <th className="p-5 text-[11px] font-black uppercase tracking-widest" style="color: var(--muted);">Why It Matters</th>
                <th className="p-5 text-[11px] font-black uppercase tracking-widest" style="color: var(--accent);">OTF Browser</th>
                <th className="p-5 text-[11px] font-black uppercase tracking-widest" style="color: var(--muted);">Mainstream Browsers</th>
                <th className="p-5 text-[11px] font-black uppercase tracking-widest" style="color: var(--muted);">Modern Browsers</th>
              </tr>
            </thead>
            <tbody className="text-sm">
              {privacySecurityRows.map((row) => (
                <tr key={`${row.area}-${row.detail}`} className="border-b transition-colors hover:bg-neutral-500/5" style="border-color: var(--border);">
                  <td className="p-5 font-bold" style="color: var(--foreground);">{row.area}</td>
                  <td className="p-5" style="color: var(--foreground);">{renderCellContent(row.detail)}</td>
                  <td className="p-5 leading-relaxed" style="color: var(--muted);">{renderCellContent(row.rationale)}</td>
                  <td className="p-5 leading-relaxed" style="color: var(--foreground); font-weight: 500;">{renderCellContent(row.otf)}</td>
                  <td className="p-5 leading-relaxed" style="color: var(--muted);">{renderCellContent(row.mainstream)}</td>
                  <td className="p-5 leading-relaxed" style="color: var(--muted);">{renderCellContent(row.modern)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </section>

    </div>
  );
}
