# OTF Browser

A custom web browser built with the Chromium Embedded Framework (CEF).

## Prerequisites

- CMake (3.10+)
- Build tools (gcc/g++, make)
- Dependencies for CEF (GTK, etc.)

## Setup

Before building, you need to download the required CEF binaries.

1.  **Download Dependencies:**
    Run the setup script to fetch the specific CEF version required for this project.
    ```bash
    chmod +x setup_deps.sh
    ./setup_deps.sh
    ```

2.  **Configure and Build:**
    ```bash
    mkdir build
    cd build
    cmake ..
    make -j$(nproc)
    ```

3.  **Run:**
    ```bash
    ./otf-browser
    ```

## Project Structure

- `src/`: Main C++ source files.
- `include/`: Header files.
- `ui/`: HTML/CSS/JS for the browser interface.
- `third_party/`: External dependencies (ignored by Git, managed by `setup_deps.sh`).
