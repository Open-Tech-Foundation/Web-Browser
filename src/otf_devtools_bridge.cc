#include "otf_devtools_bridge.h"

#include <utility>

namespace otf {

DevToolsBridge::DevToolsBridge() = default;
DevToolsBridge::~DevToolsBridge() = default;

void DevToolsBridge::Attach(CefRefPtr<CefBrowser> browser) {
  // Drop any prior registration before re-attaching. The CefRegistration
  // destructor disconnects the previous observer.
  registration_ = nullptr;
  browser_ = browser;
  if (browser_) {
    registration_ = browser_->GetHost()->AddDevToolsMessageObserver(this);
  }
}

int DevToolsBridge::Execute(const std::string& method,
                            CefRefPtr<CefDictionaryValue> params,
                            ResultCb cb) {
  if (!browser_) return 0;
  // Pass 0 to let CEF allocate the next id automatically; the return
  // value is the assigned id we use to route the response.
  int id = browser_->GetHost()->ExecuteDevToolsMethod(0, method, params);
  if (id == 0) return 0;
  pending_[id] = std::move(cb);
  return id;
}

void DevToolsBridge::OnDevToolsMethodResult(CefRefPtr<CefBrowser> /*browser*/,
                                            int message_id,
                                            bool success,
                                            const void* result,
                                            size_t result_size) {
  auto it = pending_.find(message_id);
  if (it == pending_.end()) return;
  std::string json;
  if (result && result_size > 0) {
    json.assign(static_cast<const char*>(result), result_size);
  } else {
    json = "{}";
  }
  ResultCb cb = std::move(it->second);
  pending_.erase(it);
  if (cb) cb(success, json);
}

void DevToolsBridge::OnDevToolsAgentDetached(CefRefPtr<CefBrowser> /*browser*/) {
  // Fail all pending callbacks so cefQuery callers don't hang forever.
  for (auto& [id, cb] : pending_) {
    if (cb) cb(false, "{\"error\":\"devtools agent detached\"}");
  }
  pending_.clear();
}

}  // namespace otf
