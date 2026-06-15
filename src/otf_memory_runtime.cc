#include "otf_memory_runtime.h"

#include <string>

#include "include/cef_task.h"
#include "include/cef_task_manager.h"
#include "include/wrapper/cef_helpers.h"
#include "otf_app.h"
#include "otf_handler.h"
#include "otf_native_rpc.h"
#include "otf_utils.h"

namespace otf {
namespace {

std::string BuildMemoryBytesEvent(int tab_id, int64_t memory_bytes) {
  return JsonObjectBuilder()
      .AddInt("id", tab_id)
      .AddString("key", "memoryBytes")
      .AddRaw("value", std::to_string(memory_bytes))
      .Build();
}

class MemoryLogTask : public CefTask {
 public:
  explicit MemoryLogTask(OtfHandler* handler) : handler_(handler) {}

  void Execute() override {
    if (handler_ && handler_->IsMemoryLoggingRunning()) {
      handler_->LogTabMemoryUsage();
      CefPostDelayedTask(TID_UI, new MemoryLogTask(handler_), 60000);
    }
  }

 private:
  OtfHandler* handler_;
  IMPLEMENT_REFCOUNTING(MemoryLogTask);
};

}  // namespace

void OtfHandler::StartMemoryLogging() {
  CEF_REQUIRE_UI_THREAD();
  if (memory_log_running_ || !tab_manager_) return;
  memory_log_running_ = true;
  memory_task_manager_ = CefTaskManager::GetTaskManager();
  CefPostDelayedTask(TID_UI, new MemoryLogTask(this), 60000);
}

void OtfHandler::StopMemoryLogging() {
  CEF_REQUIRE_UI_THREAD();
  memory_log_running_ = false;
  memory_task_manager_ = nullptr;
}

void OtfHandler::LogTabMemoryUsage() {
  CEF_REQUIRE_UI_THREAD();
  if (!tab_manager_) return;

  auto tm = memory_task_manager_;
  if (!tm) return;

  OtfApp* app = OtfApp::GetInstance();
  if (!app) return;

  const int active_tab_id = app->GetCurrentTabId();
  if (active_tab_id < 0) return;

  CefRefPtr<CefBrowser> browser = tab_manager_->GetBrowser(active_tab_id);
  if (!browser) return;

  int64_t task_id = tm->GetTaskIdForBrowserId(browser->GetIdentifier());
  if (task_id < 0) return;

  CefTaskInfo info;
  if (!tm->GetTaskInfo(task_id, info)) return;

  int64_t memory_bytes = info.memory > 0 ? info.memory : -1;
  SendEvent(BuildMemoryBytesEvent(active_tab_id, memory_bytes));
}

}  // namespace otf
