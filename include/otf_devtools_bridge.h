#ifndef OTF_DEVTOOLS_BRIDGE_H_
#define OTF_DEVTOOLS_BRIDGE_H_

#include <functional>
#include <map>
#include <string>

#include "include/cef_browser.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_registration.h"

namespace otf {

// Routes async CefBrowserHost::ExecuteDevToolsMethod responses back to
// per-call callbacks. The vanilla CEF API fires the method on the
// browser host and reports completion through a single observer keyed
// by message id — every caller has to track its own id to recover the
// result. DevToolsBridge owns the observer registration, allocates ids,
// and dispatches OnDevToolsMethodResult to the matching callback so
// individual cefQuery handlers can stay short.
//
// One bridge per browser; attach once to the UI shell browser at
// startup — CDP methods like Storage.getUsageAndQuota are
// browser-context-scoped, not browser-instance-scoped, so any browser
// in the profile works.
class DevToolsBridge : public CefDevToolsMessageObserver {
 public:
  // success: did Chromium report a successful method invocation?
  // result_json: the UTF-8 JSON "result" body (when success=true) or
  //              "error" body (when success=false). Always valid JSON.
  using ResultCb = std::function<void(bool success,
                                      const std::string& result_json)>;

  DevToolsBridge();
  ~DevToolsBridge() override;

  // Register the observer on |browser|. Calling more than once replaces
  // the prior registration so the bridge can be re-attached if the UI
  // shell is recreated (e.g. window rebuild).
  void Attach(CefRefPtr<CefBrowser> browser);

  // Fire a CDP method. Returns the assigned message id (or 0 on
  // failure to submit). |cb| runs on the UI thread when the response
  // arrives or the agent detaches.
  int Execute(const std::string& method,
              CefRefPtr<CefDictionaryValue> params,
              ResultCb cb);

  // CefDevToolsMessageObserver:
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser> browser,
                              int message_id,
                              bool success,
                              const void* result,
                              size_t result_size) override;
  void OnDevToolsAgentDetached(CefRefPtr<CefBrowser> browser) override;

 private:
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefRegistration> registration_;
  std::map<int, ResultCb> pending_;

  IMPLEMENT_REFCOUNTING(DevToolsBridge);
};

}  // namespace otf

#endif  // OTF_DEVTOOLS_BRIDGE_H_
