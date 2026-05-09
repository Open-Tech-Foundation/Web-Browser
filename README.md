<div align="center">

# OTF Web Browser

**The Modern Lightweight Desktop Browser.**

*Part of the <img src="https://raw.githubusercontent.com/Open-Tech-Foundation/website/3ed7ac70ec44465eec0f94e5185cb28a9b11ed07/static/img/OTF-Logo.svg" width="24" align="center" /> [Open Tech Foundation](https://github.com/Open-Tech-Foundation) ecosystem.*

[**Report Bug**](https://github.com/Open-Tech-Foundation/Web-Browser/issues)

</div>

A modern lightweight desktop web browser built on top of Chromium Embedded Framework (CEF).

![OTF Web Browser Screenshot](Screenshot.png)

- 🎨 **Modern UI**: Built with React and Tailwind CSS for a premium look and feel.
- ⚡ **High Performance**: Powered by CEF (Chromium) for industry-leading speed and compatibility.
- 🔄 **HMR Support**: Instant UI updates during development without restarting the C++ engine.
- 🛡️ **Privacy Focused**: Built with security and transparency in mind.
- 📦 **Zero Bloat**: Lightweight architecture designed for speed.

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

## 📂 Project Structure

- `src/`: Core C++ source files.
- `include/`: C++ header files.
- `ui/`: React frontend source code.
- `third_party/`: External dependencies (CEF SDK).

## 🛡️ Security

OTF Web Browser prioritizes security:
- **Sandbox Support**: Ready for multi-process sandboxing.
- **Up-to-date Engine**: Regularly updated to the latest CEF/Chromium releases.
- **Open Source**: Transparent codebase for community auditing.

## 📄 License

This project is licensed under the [MIT License](LICENSE).
