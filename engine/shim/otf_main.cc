// Process entry point. Per plan.md the Rust backend is the control brain, so
// main() immediately hands control to Rust, which boots Chromium through the
// shim (otf_browser_init / otf_browser_run) and owns the tab/navigation model.
//
// The Rust staticlib exports otf_backend_main with C linkage.

#include <cstdint>

extern "C" int32_t otf_backend_main(int argc, char** argv);

int main(int argc, char** argv) {
  return static_cast<int>(otf_backend_main(argc, argv));
}
