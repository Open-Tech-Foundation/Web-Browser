// otf's BrowserMainParts. For now it subclasses content_shell's
// ShellBrowserMainParts so it inherits the proven toolkit/Linux bring-up
// (LinuxUi, bluez, input method, aura/views init via Shell::Initialize) and the
// browser-context creation, but it overrides the window step: instead of opening
// a content::Shell window it builds otf's own top-level window (OtfWindow) around
// the UI WebContents. Dropping the ShellBrowserMainParts base entirely comes with
// the rest of the content_shell de-scaffold.

#ifndef OTF_ENGINE_SHIM_OTF_BROWSER_MAIN_PARTS_H_
#define OTF_ENGINE_SHIM_OTF_BROWSER_MAIN_PARTS_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "content/shell/browser/shell_browser_main_parts.h"

namespace content {
class WebContents;
}

namespace otf {

class OtfWindow;

class OtfBrowserMainParts : public content::ShellBrowserMainParts {
 public:
  OtfBrowserMainParts();

  OtfBrowserMainParts(const OtfBrowserMainParts&) = delete;
  OtfBrowserMainParts& operator=(const OtfBrowserMainParts&) = delete;

  ~OtfBrowserMainParts() override;

  // content::ShellBrowserMainParts:
  void WillRunMainMessageLoop(
      std::unique_ptr<base::RunLoop>& run_loop) override;
  void PostMainMessageLoopRun() override;

 protected:
  // Replaces Shell::CreateNewWindow with otf's own UI WebContents + OtfWindow.
  void InitializeMessageLoopContext() override;

 private:
  // Runs the captured run-loop quit closure; wired to OtfWindow's close.
  void OnWindowClosed();

  std::unique_ptr<content::WebContents> ui_contents_;
  std::unique_ptr<OtfWindow> window_;
  base::OnceClosure quit_closure_;

  base::WeakPtrFactory<OtfBrowserMainParts> weak_factory_{this};
};

}  // namespace otf

#endif  // OTF_ENGINE_SHIM_OTF_BROWSER_MAIN_PARTS_H_
