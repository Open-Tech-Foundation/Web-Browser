#include "otf/shim/otf_tab_host.h"

#include <algorithm>
#include <utility>

#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/shell/browser/shell.h"
#include "otf/shim/otf_browser_context.h"
#include "ui/aura/window.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace otf {

// Per-tab WebContentsObserver: forwards title/url/load to OtfTabHost keyed by
// the caller-assigned tab id.
class OtfTabObserver : public content::WebContentsObserver {
 public:
  OtfTabObserver(content::WebContents* contents,
                 OtfTabHandle id,
                 OtfTabHost* host)
      : content::WebContentsObserver(contents), id_(id), host_(host) {}

  void TitleWasSet(content::NavigationEntry* /*entry*/) override {
    if (web_contents()) {
      host_->NotifyTitle(id_, base::UTF16ToUTF8(web_contents()->GetTitle()));
    }
  }
  void DidStartLoading() override { host_->NotifyLoad(id_, true); }
  void DidStopLoading() override { host_->NotifyLoad(id_, false); }
  void DidFinishNavigation(content::NavigationHandle* handle) override {
    if (handle && handle->IsInPrimaryMainFrame() && handle->HasCommitted() &&
        web_contents()) {
      host_->NotifyUrl(id_, web_contents()->GetLastCommittedURL().spec());
    }
  }

 private:
  OtfTabHandle id_;
  raw_ptr<OtfTabHost> host_;
};

// static
OtfTabHost& OtfTabHost::Get() {
  static base::NoDestructor<OtfTabHost> instance;
  return *instance;
}

OtfTabHost::OtfTabHost() = default;
OtfTabHost::~OtfTabHost() = default;

void OtfTabHost::SetCallbacks(OtfCallbacks callbacks) {
  callbacks_ = callbacks;
}

content::WebContents* OtfTabHost::Find(OtfTabHandle id) {
  auto it = tabs_.find(id);
  return it == tabs_.end() ? nullptr : it->second.contents.get();
}

// The BrowserContext (profile) backing all page tabs. otf's own context,
// replacing content_shell's. Process-lifetime for now (created lazily on the
// first tab, intentionally never destroyed); ownership moves to
// OtfBrowserMainParts once the own-window work lands, which also lets the UI
// surface share this same context.
// TODO(phase2b): own the context in OtfBrowserMainParts and tear it down on exit.
static content::BrowserContext* PageBrowserContext() {
  static base::NoDestructor<OtfBrowserContext> context(/*off_the_record=*/false);
  return context.get();
}

content::WebContents* OtfTabHost::EnsureContents(OtfTabHandle id) {
  if (auto* existing = Find(id)) {
    return existing;
  }
  content::WebContents::CreateParams params(PageBrowserContext());
  std::unique_ptr<content::WebContents> contents =
      content::WebContents::Create(params);
  content::WebContents* raw = contents.get();
  TabEntry entry;
  entry.contents = std::move(contents);
  entry.observer = std::make_unique<OtfTabObserver>(raw, id, this);
  tabs_[id] = std::move(entry);
  return raw;
}

aura::Window* OtfTabHost::HostWindow() {
  auto& windows = content::Shell::windows();
  if (windows.empty()) {
    return nullptr;
  }
  // The first Shell hosts the UI; its native window is our top-level container.
  return windows.front()->window();
}

gfx::Rect OtfTabHost::ContentBounds() {
  if (content_bounds_) {
    return *content_bounds_;
  }
  // Default: the UI window's client area minus the fixed chrome strip on top.
  constexpr int kChromeHeight = 65;
  aura::Window* host = HostWindow();
  if (!host) {
    return gfx::Rect();
  }
  const gfx::Size size = host->bounds().size();
  return gfx::Rect(0, kChromeHeight, size.width(),
                   std::max(0, size.height() - kChromeHeight));
}

OtfStatus OtfTabHost::Create(OtfTabHandle id, const std::string& url) {
  content::WebContents* wc = EnsureContents(id);
  if (!wc) {
    return -1;
  }
  if (!url.empty()) {
    return Navigate(id, url);
  }
  return 0;
}

OtfStatus OtfTabHost::Navigate(OtfTabHandle id, const std::string& url) {
  content::WebContents* wc = EnsureContents(id);
  if (!wc) {
    return -1;
  }
  content::NavigationController::LoadURLParams params{GURL(url)};
  params.transition_type = ui::PAGE_TRANSITION_TYPED;
  wc->GetController().LoadURLWithParams(params);
  Show(id);
  return 0;
}

OtfStatus OtfTabHost::Show(OtfTabHandle id) {
  // Hide the previously active tab's view, if any and different.
  if (active_ && active_ != id) {
    if (content::WebContents* prev = Find(active_)) {
      if (aura::Window* prev_view = prev->GetNativeView()) {
        prev_view->Hide();
      }
      prev->WasHidden();
    }
  }
  active_ = id;

  content::WebContents* wc = Find(id);
  if (!wc) {
    return 0;  // model-only tab (never navigated): nothing to paint.
  }
  aura::Window* host = HostWindow();
  aura::Window* view = wc->GetNativeView();
  if (host && view) {
    if (view->parent() != host) {
      host->AddChild(view);
    }
    view->SetBounds(ContentBounds());
    view->Show();
    host->StackChildAtTop(view);
  }
  wc->WasShown();
  return 0;
}

OtfStatus OtfTabHost::Hide(OtfTabHandle id) {
  if (content::WebContents* wc = Find(id)) {
    if (aura::Window* view = wc->GetNativeView()) {
      view->Hide();
    }
    wc->WasHidden();
  }
  return 0;
}

OtfStatus OtfTabHost::Close(OtfTabHandle id) {
  auto it = tabs_.find(id);
  if (it == tabs_.end()) {
    return 0;
  }
  if (auto* wc = it->second.contents.get()) {
    if (aura::Window* view = wc->GetNativeView()) {
      view->Hide();
    }
  }
  tabs_.erase(it);  // destroys observer, then WebContents.
  if (active_ == id) {
    active_ = 0;
  }
  return 0;
}

OtfStatus OtfTabHost::Reload(OtfTabHandle id) {
  if (content::WebContents* wc = Find(id)) {
    wc->GetController().Reload(content::ReloadType::NORMAL, false);
  }
  return 0;
}

OtfStatus OtfTabHost::Stop(OtfTabHandle id) {
  if (content::WebContents* wc = Find(id)) {
    wc->Stop();
  }
  return 0;
}

OtfStatus OtfTabHost::GoBack(OtfTabHandle id) {
  if (content::WebContents* wc = Find(id)) {
    if (wc->GetController().CanGoBack()) {
      wc->GetController().GoBack();
    }
  }
  return 0;
}

OtfStatus OtfTabHost::GoForward(OtfTabHandle id) {
  if (content::WebContents* wc = Find(id)) {
    if (wc->GetController().CanGoForward()) {
      wc->GetController().GoForward();
    }
  }
  return 0;
}

void OtfTabHost::SetContentBounds(int32_t x, int32_t y, int32_t w, int32_t h) {
  content_bounds_ = gfx::Rect(x, y, w, h);
  if (active_) {
    if (content::WebContents* wc = Find(active_)) {
      if (aura::Window* view = wc->GetNativeView()) {
        view->SetBounds(*content_bounds_);
      }
    }
  }
}

void OtfTabHost::NotifyTitle(OtfTabHandle id, const std::string& title) {
  if (callbacks_.on_title_changed) {
    callbacks_.on_title_changed(callbacks_.user_data, id, title.c_str());
  }
}

void OtfTabHost::NotifyUrl(OtfTabHandle id, const std::string& url) {
  if (callbacks_.on_url_changed) {
    callbacks_.on_url_changed(callbacks_.user_data, id, url.c_str());
  }
}

void OtfTabHost::NotifyLoad(OtfTabHandle id, bool loading) {
  if (callbacks_.on_load_state) {
    callbacks_.on_load_state(callbacks_.user_data, id, loading ? 1 : 0);
  }
}

}  // namespace otf
