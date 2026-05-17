<div align="center">

# OTF Web Browser <img src="https://img.shields.io/badge/Alpha-A855F7?style=flat-square" align="center" />

**Engineered for Privacy & Security**

*Part of the <img src="https://raw.githubusercontent.com/Open-Tech-Foundation/website/3ed7ac70ec44465eec0f94e5185cb28a9b11ed07/static/img/OTF-Logo.svg" width="24" align="center" /> [Open Tech Foundation](https://github.com/Open-Tech-Foundation) ecosystem.*

![OTF Web Browser Screenshot](Screenshot.png)

[**browser.opentechf.org**](https://browser.opentechf.org/) | [**Report Bug**](https://github.com/Open-Tech-Foundation/Web-Browser/issues)

</div>

> [!WARNING]  
> **Experimental Project**: This browser is in early development. Features, APIs, and stability are subject to significant changes.

> # A fast, privacy-focused browser with hardened security, built on top of the Chromium Embedded Framework.

## 📥 Download

You can download the latest version of OTF Web Browser for your platform from our [**GitHub Releases**](https://github.com/Open-Tech-Foundation/Web-Browser/releases) page.

| Platform | Release | Status |
| :--- | :--- | :--- |
| **Linux (x64)** | [Latest Tarball](https://github.com/Open-Tech-Foundation/Web-Browser/releases) | ✅ Stable |
| **Windows (x64)** | TBD | 🚧 In Progress |
| **macOS** | TBD | 🚧 In Progress |



## Development

## 📦 Installation

Prerequisites:
- CMake (3.21+)
- Ninja (Build system)
- GCC 14+ (C++20 support)
- Bun (for UI development)

```bash
bun run setup
```

## 🛠 Usage

To start the UI development server (with HMR) and launch the browser automatically:

```bash
bun run dev
```

## 🏗 Build

We use **Ninja** for high-performance builds. To build the project for production:

### 1. Build UI Assets
```bash
bun run build:ui
```

### 2. Build C++ Engine
```bash
bun run build:cpp
```

## 🛣️ Roadmap

- [x] MVP
- [ ] Automated CI/CD Release Pipeline
- [ ] Windows (x64) Support
- [ ] macOS (Universal) Support

## License

This project is licensed under the [GNU Affero General Public License v3.0](LICENSE).

[![License: AGPL v3](https://img.shields.io/badge/License-AGPL_v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)
