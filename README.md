# OTF Browser

A custom web browser built with the Chromium Embedded Framework (CEF).

## Prerequisites

- CMake (3.10+)
- Build tools (gcc/g++, make)
- Dependencies for CEF (GTK, etc.)

## Development Cycle

For the best experience, use the following workflow:

### 1. Setup Dependencies
```bash
bun run setup
```

### 2. UI Development (HMR)
To work on the browser's UI with Hot Module Replacement:
```bash
bun run dev:ui
```
This serves the `ui/` directory at `http://localhost:3000`.

### 3. Run Browser in Dev Mode
In another terminal, run the browser pointed at the dev server:
```bash
bun run dev:browser
```
Now, any changes you make to `ui/index.html` will be reflected instantly in the browser without restarting the C++ application.

### 4. Fast Builds
We recommend using **Ninja** for significantly faster builds:
```bash
bun run build
```

## Build Dependencies
*   CMake 3.21+
*   Ninja (optional but recommended)
*   Bun (for UI dev workflow)
*   GCC 14+ (C++20 support)

## Project Structure
- `src/`: Main C++ source files.
- `include/`: Header files.
- `ui/`: HTML/CSS/JS for the browser interface.
- `third_party/`: External dependencies (ignored by Git, managed by `setup_deps.sh`).
