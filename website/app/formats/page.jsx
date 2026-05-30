const sections = [
  {
    title: "Image Formats",
    desc: "Viewable in a dedicated overlay with zoom, rotate, and download support. Multi-page TIFF files are fully supported.",
    icon: <svg className="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"/><circle cx="8.5" cy="8.5" r="1.5"/><polyline points="21 15 16 10 5 21"/></svg>,
    rows: [
      ['PNG', '.png', 'Portable Network Graphics'],
      ['JPEG', '.jpg, .jpeg', 'Joint Photographic Experts Group'],
      ['GIF', '.gif', 'Graphics Interchange Format'],
      ['WebP', '.webp', 'Web Picture format'],
      ['BMP', '.bmp', 'Bitmap image'],
      ['SVG', '.svg', 'Scalable Vector Graphics'],
      ['AVIF', '.avif', 'AV1 Image Format'],
      ['ICO', '.ico', 'Icon format'],
      ['TIFF', '.tif, .tiff', 'Multi-page TIFF supported'],
    ],
  },
  {
    title: "Document Formats",
    desc: "Opened in a built-in viewer with syntax highlighting via Monaco editor, interactive CSV tables, and Markdown preview.",
    icon: <svg className="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/><line x1="16" y1="13" x2="8" y2="13"/><line x1="16" y1="17" x2="8" y2="17"/></svg>,
    rows: [
      ['PDF', '.pdf', 'Native iframe renderer'],
      ['CSV', '.csv', 'Virtualized interactive table with sorting and resizing'],
      ['Markdown', '.md', 'Split-pane preview with live rendering'],
      ['JSON', '.json, .jsonl', 'Collapsible tree view via Monaco'],
      ['HTML', '.html', 'Syntax-highlighted source view'],
      ['CSS', '.css', 'Syntax-highlighted source view'],
      ['JavaScript', '.js', 'Syntax-highlighted source view'],
      ['TypeScript', '.ts', 'Syntax-highlighted source view'],
      ['XML', '.xml', 'Syntax-highlighted source view'],
      ['YAML', '.yaml, .yml', 'Syntax-highlighted source view'],
      ['TOML', '.toml', 'Syntax-highlighted source view'],
      ['INI / Config', '.ini, .cfg, .conf', 'Syntax-highlighted source view'],
      ['Plain Text', '.txt, .log', 'Plain text view'],
    ],
  },
  {
    title: "Code & Scripting Formats",
    desc: "Full syntax highlighting via Monaco editor with language-specific tokenization.",
    icon: <svg className="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><polyline points="16 18 22 12 16 6"/><polyline points="8 6 2 12 8 18"/></svg>,
    rows: [
      ['Python', '.py', 'Syntax highlighting'],
      ['Shell', '.sh, .bash', 'Syntax highlighting'],
      ['Ruby', '.rb', 'Syntax highlighting'],
      ['Lua', '.lua', 'Syntax highlighting'],
      ['PHP', '.php', 'Syntax highlighting'],
      ['C', '.c, .h', 'Syntax highlighting'],
      ['C++', '.cpp, .hpp', 'Syntax highlighting'],
      ['Rust', '.rs', 'Syntax highlighting'],
      ['Go', '.go', 'Syntax highlighting'],
      ['Java', '.java', 'Syntax highlighting'],
      ['Kotlin', '.kt', 'Syntax highlighting'],
      ['Swift', '.swift', 'Syntax highlighting'],
      ['SQL', '.sql', 'Syntax highlighting'],
      ['LaTeX', '.tex', 'Syntax highlighting'],
      ['R', '.r', 'Syntax highlighting'],
      ['Makefile', 'Makefile (bare)', 'Syntax highlighting'],
    ],
  },
];

export default function FormatsPage() {
  return (
    <div className="flex flex-col">
      <section className="relative pt-24 pb-16 px-6 overflow-hidden">
        <div className="absolute top-0 left-1/2 -translate-x-1/2 w-[1000px] h-[600px] blur-[120px] rounded-full pointer-events-none opacity-10" style="background-color: var(--accent);"></div>
        <div className="max-w-5xl mx-auto relative z-10 text-center">
          <div className="inline-flex items-center gap-2 px-4 py-2 rounded-full border text-xs font-bold uppercase tracking-widest mb-6" style="background-color: color-mix(in srgb, var(--accent) 10%, transparent); border-color: color-mix(in srgb, var(--accent) 20%, transparent); color: var(--accent);">
            Preview Any File
          </div>
          <h1 className="text-4xl md:text-5xl font-extrabold tracking-tight mb-4" style="color: var(--foreground);">
            Supported File Formats
          </h1>
          <p className="max-w-2xl mx-auto text-lg leading-relaxed" style="color: var(--muted);">
            OTF Browser can preview <strong className="text-white">50+ file formats</strong> natively — no extensions, no plugins, no third-party tools required.
          </p>
        </div>
      </section>

      {sections.map((section) => (
        <section key={section.title} className="py-12 px-6 max-w-5xl mx-auto w-full">
          <div className="flex items-center gap-3 mb-8">
            <div className="w-10 h-10 rounded-2xl flex items-center justify-center" style="background-color: color-mix(in srgb, var(--accent) 12%, transparent);">
              <div style="color: var(--accent);">{section.icon}</div>
            </div>
            <div>
              <h2 className="text-2xl font-extrabold tracking-tight" style="color: var(--foreground);">{section.title}</h2>
              <p className="text-sm mt-1" style="color: var(--muted);">{section.desc}</p>
            </div>
          </div>
          <div className="overflow-x-auto rounded-2xl border" style="border-color: var(--border);">
            <table className="w-full text-sm border-collapse">
              <thead>
                <tr className="border-b" style="border-color: var(--border); background-color: color-mix(in srgb, var(--foreground) 4%, transparent);">
                  <th className="text-left py-3 px-5 font-bold" style="color: var(--foreground);">Format</th>
                  <th className="text-left py-3 px-5 font-bold" style="color: var(--foreground);">Extensions</th>
                  <th className="text-left py-3 px-5 font-bold" style="color: var(--foreground);">Notes</th>
                </tr>
              </thead>
              <tbody>
                {section.rows.map(([name, ext, note]) => (
                  <tr key={name} className="border-b last:border-b-0" style="border-color: var(--border);">
                    <td className="py-2.5 px-5 font-semibold whitespace-nowrap" style="color: var(--foreground);">{name}</td>
                    <td className="py-2.5 px-5 font-mono text-xs" style="color: var(--accent);">{ext}</td>
                    <td className="py-2.5 px-5 text-xs" style="color: var(--muted);">{note}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </section>
      ))}

      <section className="py-12 px-6 max-w-5xl mx-auto w-full text-center">
        <div className="p-10 rounded-[32px] border" style="background-color: var(--bg-card); border-color: var(--border);">
          <div className="w-14 h-14 mx-auto mb-5 rounded-2xl flex items-center justify-center" style="background-color: color-mix(in srgb, var(--accent) 12%, transparent);">
            <svg className="w-7 h-7" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style="color: var(--accent);"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/><line x1="16" y1="13" x2="8" y2="13"/><line x1="16" y1="17" x2="8" y2="17"/></svg>
          </div>
          <h3 className="text-xl font-bold mb-3" style="color: var(--foreground);">Try It Now</h3>
          <p className="max-w-lg mx-auto text-sm leading-relaxed mb-6" style="color: var(--muted);">
            Download a file, click any supported document link, or open a local file — the preview opens instantly in an overlay without leaving your current page.
          </p>
          <a
            href="/"
            className="inline-flex items-center gap-2 px-6 py-2.5 rounded-xl text-sm font-bold transition-all hover:-translate-y-0.5"
            style="background: linear-gradient(135deg, var(--accent), #f59e0b); color: white;"
          >
            <svg className="w-4 h-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>
            Download OTF Browser
          </a>
        </div>
      </section>
    </div>
  );
}
