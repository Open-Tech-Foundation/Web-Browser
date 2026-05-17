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

const featureRows = [
  { feature: "Tabs", status: "Complete", notes: "Native tab strip, tab lifecycle, and active-tab state." },
  { feature: "New tab page", status: "Complete", notes: "Browser-owned new tab experience with search entry." },
  { feature: "Keyboard shortcuts", status: "Complete", notes: "Browser shortcuts and page shortcuts are wired." },
  { feature: "Find in page", status: "Complete", notes: "Browser-owned find UI with per-tab behavior." },
  { feature: "Search engine selection", status: "Complete", notes: "User-selectable search engine support." },
  { feature: "Address-bar URL resolution", status: "Complete", notes: "DNS-backed URL/search resolver with clean query encoding." },
  { feature: "Selected-text search", status: "Complete", notes: "Context menu search uses the current search engine and selected text." },
  { feature: "Native image preview", status: "Complete", notes: "Tab-unique interactive preview overlay with full zoom, rotation, panning, copying, and download integration." },
  { feature: "Bookmarks", status: "Complete", notes: "Bookmark storage, state, and UI integration." },
  { feature: "History", status: "Complete", notes: "History page and persistence controls follow privacy settings." },
  { feature: "Downloads", status: "Complete", notes: "Download tracking and downloads page." },
  { feature: "On-startup pages", status: "Complete", notes: "User-configurable startup behavior with URL validation." },
  { feature: "Security settings", status: "Complete", notes: "Security controls surfaced in settings." },
  { feature: "Privacy settings", status: "Complete", notes: "History/download defaults and reset behavior." },
  { feature: "Certificate viewer", status: "Complete", notes: "Current-tab SSL certificate API and viewer support." },
  { feature: "Protection test center", status: "Complete", notes: "The website protection test center reports privacy and security policy coverage." },
  { feature: "HTTP insecure blocking", status: "Complete", notes: "HTTP block page and red security state." },
  { feature: "Reset browser data", status: "Complete", notes: ["Settings", "History", "Bookmarks", "SSL exceptions", "HTTP auth", "Connections", "Cache", "Cookies"] },
  { feature: "Target blank handling", status: "Complete", notes: "Opens target=_blank navigations in a new tab." },
  { feature: "Password manager", status: "Planned", notes: "No built-in password storage or sync manager yet." },
  { feature: "Sync", status: "Planned", notes: "No account-based sync layer yet." },
  { feature: "Extensions", status: "Not supported", notes: "Extension platform is intentionally not exposed yet." },
  { feature: "Profiles", status: "Planned", notes: "Profile isolation is not fully implemented." },
  { feature: "Workspaces", status: "Planned", notes: "Workspace behavior is planned/in progress." },
  { feature: "Bookmarks bar", status: "Partial", notes: "Bookmark viewing exists; full bar parity is not complete." },
  { feature: "Multi-window", status: "Partial", notes: "Core windows exist; workflow parity is still evolving." },
];

const securityHighlights = [
  { title: "WebGPU compute blocked", desc: "Keeps WebGPU graphics usable while reducing compute abuse, crypto-mining, and high-signal GPU fingerprinting." },
  { title: "WebGL normalized", desc: "Reduces vendor, renderer, extension, and capability variance that can become a stable device hash." },
  { title: "Canvas protected", desc: "Protects readback/export paths that fingerprint scripts use to compare subtle rendering differences." },
  { title: "Dangerous schemes blocked", desc: "Prevents web navigation into internal engine pages, local files, script URLs, data URLs, and disabled extension surfaces." },
  { title: "Diagnostics included", desc: "The website protection test center makes browser behavior visible and testable." },
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

function StatusBadge({ status }) {
  const color =
    status === "Complete"
      ? "bg-emerald-500/10 text-emerald-600 dark:text-emerald-400 border-emerald-500/20"
      : status === "Partial"
        ? "bg-amber-500/10 text-amber-600 dark:text-amber-400 border-amber-500/20"
        : status === "Planned"
          ? "bg-indigo-500/10 text-indigo-600 dark:text-indigo-400 border-indigo-500/20"
          : "bg-neutral-500/10 text-neutral-600 dark:text-neutral-400 border-neutral-500/20";

  return (
    <span className={`inline-flex rounded-full border px-3 py-1 text-[10px] font-black uppercase tracking-widest ${color}`}>
      {status}
    </span>
  );
}

export default function HomePage() {
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
              icon: <svg className="w-6 h-6" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M2 12h20M2 5h20M2 19h20"/></svg>
            },
            {
              title: "WebGPU Compute Protection",
              desc: "Prevents GPU resource abuse by automatically blocking non-graphical WebGPU compute workloads, stopping stealth crypto mining and hardware strain.",
              icon: <svg className="w-6 h-6" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><rect x="2" y="4" width="20" height="16" rx="2"/><path d="M7 8h10M7 12h10M7 16h10"/></svg>
            },
            {
              title: "Native Image Preview",
              desc: "Right-click any image to open it in a beautiful interactive overlay. Zoom, drag, rotate, copy, or natively download with complete tab-unique preservation.",
              icon: <svg className="w-6 h-6" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"></rect><circle cx="8.5" cy="8.5" r="1.5"></circle><polyline points="21 15 16 10 5 21"></polyline></svg>
            }
          ].map((feature) => (
            <div key={feature.title} className="p-8 rounded-[32px] border transition-all duration-500 hover:border-orange-500/30 hover:-translate-y-2 group" style="background-color: var(--bg-card); border-color: var(--border);">
              <div className="w-12 h-12 rounded-2xl bg-orange-500/10 flex items-center justify-center mb-6 text-orange-500 group-hover:scale-110 transition-transform">
                {feature.icon}
              </div>
              <h3 className="text-xl font-bold mb-4" style="color: var(--foreground);">{feature.title}</h3>
              <p className="text-sm leading-relaxed" style="color: var(--muted);">{feature.desc}</p>
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

      <section className="py-28 px-6 bg-white/[0.04]">
        <div className="max-w-7xl mx-auto">
          <div className="mb-12">
            <h2 className="text-4xl font-bold mb-6" style="color: var(--foreground);">Feature Parity</h2>
            <p className="max-w-3xl leading-relaxed" style="color: var(--muted);">
              Current browser feature coverage, including completed browser fundamentals and areas still intentionally unsupported or in progress.
            </p>
          </div>

          <div className="overflow-x-auto rounded-3xl border transition-colors" style="background-color: var(--bg-card); border-color: var(--border);">
            <table className="w-full min-w-[900px] text-left border-collapse">
              <thead>
                <tr className="border-b" style="background-color: color-mix(in srgb, var(--foreground) 3%, transparent); border-color: var(--border);">
                  <th className="p-5 text-[11px] font-black uppercase tracking-widest" style="color: var(--muted);">Feature</th>
                  <th className="p-5 text-[11px] font-black uppercase tracking-widest" style="color: var(--muted);">Status</th>
                  <th className="p-5 text-[11px] font-black uppercase tracking-widest" style="color: var(--muted);">Notes</th>
                </tr>
              </thead>
              <tbody className="text-sm">
                {featureRows.map((row) => (
                  <tr key={row.feature} className="border-b transition-colors hover:bg-neutral-500/5" style="border-color: var(--border);">
                    <td className="p-5 font-bold" style="color: var(--foreground);">{row.feature}</td>
                    <td className="p-5"><StatusBadge status={row.status} /></td>
                    <td className="p-5" style="color: var(--muted);">{renderCellContent(row.notes)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </div>
      </section>

      <section className="py-28 px-6 max-w-7xl mx-auto w-full">
        <div className="grid grid-cols-1 lg:grid-cols-2 gap-16 items-start">
          <div>
            <h2 className="text-4xl font-extrabold mb-8 tracking-tight" style="color: var(--foreground);">Policy, Not Promises</h2>
            <p className="leading-relaxed mb-10" style="color: var(--muted);">
              OTF Browser treats privacy and security as enforceable browser behavior. The policy is designed to reduce sensitive persistence, block unsafe navigation surfaces, and make fingerprint protections inspectable.
            </p>

            <div className="space-y-6">
              {securityHighlights.map((item) => (
                <div key={item.title} className="flex gap-4">
                  <div className="w-1.5 h-1.5 rounded-full mt-2 shrink-0" style="background-color: var(--accent);"></div>
                  <div>
                    <h4 className="font-bold" style="color: var(--foreground);">{item.title}</h4>
                    <p className="text-sm leading-relaxed" style="color: var(--muted);">{item.desc}</p>
                  </div>
                </div>
              ))}
            </div>
          </div>

          <div className="p-8 md:p-10 rounded-[40px] border shadow-2xl transition-colors" style="background-color: var(--bg-card); border-color: var(--border);">
            <h3 className="text-xl font-bold mb-8 flex items-center gap-3" style="color: var(--foreground);">
              <span className="w-8 h-8 rounded-lg flex items-center justify-center" style="background-color: color-mix(in srgb, var(--accent) 10%, transparent); color: var(--accent);">
                <svg className="w-4 h-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/></svg>
              </span>
              Protection Coverage
            </h3>
            <div className="space-y-4">
              {[
                ["Canvas", "Protected readback/export"],
                ["WebGL", "Normalized profile"],
                ["WebGPU", "Graphics allowed, compute blocked"],
                ["Frames", "Policy injection tracked"],
                ["Workers", "Classic/module/shared/nested coverage reported"],
                ["UA", "Desktop OTFBrowser identity"],
              ].map(([label, status]) => (
                <div key={label} className="flex items-center justify-between gap-4 p-4 rounded-xl border transition-colors" style="background-color: color-mix(in srgb, var(--foreground) 3%, transparent); border-color: var(--border);">
                  <span className="text-sm font-medium" style="color: var(--muted);">{label}</span>
                  <span className="text-right text-xs font-bold uppercase tracking-widest" style="color: var(--accent);">{status}</span>
                </div>
              ))}
            </div>
            <p className="mt-8 text-[10px] uppercase tracking-widest font-bold text-center" style="color: var(--muted);">
              Diagnostics available in the protection test center
            </p>
          </div>
        </div>
      </section>
    </div>
  );
}
