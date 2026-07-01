// otf's BrowserMainParts. Stands alone on content::BrowserMainParts (no
// content_shell base): it performs the desktop toolkit bring-up (input method,
// LinuxUi), creates the browser context, and builds otf's own top-level window
// (OtfWindow) around the UI WebContents, then owns the run-loop quit.

#ifndef OTF_ENGINE_SHIM_OTF_BROWSER_MAIN_PARTS_H_
#define OTF_ENGINE_SHIM_OTF_BROWSER_MAIN_PARTS_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/browser_main_parts.h"

namespace content {
class WebContents;
}

namespace otf {

class OtfBrowserContextManager;
class OtfPlatformWindow;

class OtfBrowserMainParts : public content::BrowserMainParts {
 public:
  OtfBrowserMainParts();

  OtfBrowserMainParts(const OtfBrowserMainParts&) = delete;
  OtfBrowserMainParts& operator=(const OtfBrowserMainParts&) = delete;

  ~OtfBrowserMainParts() override;

  // content::BrowserMainParts:
  int PreEarlyInitialization() override;
  void ToolkitInitialized() override;
  int PreMainMessageLoopRun() override;
  void WillRunMainMessageLoop(
      std::unique_ptr<base::RunLoop>& run_loop) override;
  void PostMainMessageLoopRun() override;

 private:
  // Runs the captured run-loop quit closure; wired to OtfWindow's close.
  void OnWindowClosed();

  std::unique_ptr<OtfBrowserContextManager> context_manager_;
  std::unique_ptr<content::WebContents> ui_contents_;
  std::unique_ptr<OtfPlatformWindow> window_;
  base::OnceClosure quit_closure_;

  base::WeakPtrFactory<OtfBrowserMainParts> weak_factory_{this};
};

}  // namespace otf

#endif  // OTF_ENGINE_SHIM_OTF_BROWSER_MAIN_PARTS_H_
