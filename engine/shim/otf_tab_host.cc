#include "otf/shim/otf_tab_host.h"

#include <algorithm>
#include <utility>

#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "content/public/browser/context_menu_params.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "otf/shim/otf_browser_context_manager.h"
#include "otf/shim/otf_platform_window.h"
#include "otf/shim/otf_trust.h"
#include "third_party/blink/public/common/context_menu_data/edit_flags.h"
#include "third_party/blink/public/mojom/context_menu/context_menu.mojom-shared.h"
#include "ui/base/page_transition_types.h"
#include "ui/gfx/geometry/rect.h"
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

namespace {

const char* MediaTypeString(blink::mojom::ContextMenuDataMediaType type) {
  using MediaType = blink::mojom::ContextMenuDataMediaType;
  switch (type) {
    case MediaType::kImage: return "image";
    case MediaType::kVideo: return "video";
    case MediaType::kAudio: return "audio";
    case MediaType::kCanvas: return "canvas";
    case MediaType::kFile: return "file";
    case MediaType::kPlugin: return "plugin";
    default: return "none";
  }
}

// Serialize the trustworthy subset of a context-menu hit-test into the JSON the
// UI menu overlay renders from.
std::string SerializeContextMenu(const content::ContextMenuParams& params) {
  const int flags = params.edit_flags;
  using Edit = blink::ContextMenuDataEditFlags;
  base::DictValue d;
  d.Set("x", params.x);
  d.Set("y", params.y);
  d.Set("mediaType", MediaTypeString(params.media_type));
  d.Set("linkUrl", params.link_url.spec());
  d.Set("linkText", base::UTF16ToUTF8(params.link_text));
  d.Set("srcUrl", params.src_url.spec());
  d.Set("hasImage", params.has_image_contents);
  d.Set("selectionText", base::UTF16ToUTF8(params.selection_text));
  d.Set("isEditable", params.is_editable);
  d.Set("pageUrl", params.page_url.spec());
  d.Set("canUndo", (flags & Edit::kCanUndo) != 0);
  d.Set("canRedo", (flags & Edit::kCanRedo) != 0);
  d.Set("canCut", (flags & Edit::kCanCut) != 0);
  d.Set("canCopy", (flags & Edit::kCanCopy) != 0);
  d.Set("canPaste", (flags & Edit::kCanPaste) != 0);
  d.Set("canSelectAll", (flags & Edit::kCanSelectAll) != 0);
  return base::WriteJson(d).value_or("{}");
}

}  // namespace

// Per-tab WebContentsDelegate: otf draws its own page context menu, so it
// intercepts the content-layer request and forwards the hit-test to the UI.
class OtfTabWebContentsDelegate : public content::WebContentsDelegate {
 public:
  OtfTabWebContentsDelegate(OtfTabHandle id, OtfTabHost* host)
      : id_(id), host_(host) {}

  bool HandleContextMenu(content::RenderFrameHost& /*rfh*/,
                         const content::ContextMenuParams& params) override {
    host_->NotifyContextMenu(id_, SerializeContextMenu(params));
    return true;  // otf shows its own menu; suppress the default.
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

content::WebContents* OtfTabHost::EnsureContents(OtfTabHandle id) {
  if (auto* existing = Find(id)) {
    return existing;
  }
  // Phase 1: page tabs share the system context (Phase 2 routes them to their
  // workspace's context for cookie/cache/storage isolation).
  OtfBrowserContextManager* manager = OtfBrowserContextManager::Get();
  content::BrowserContext* context = manager ? manager->System() : nullptr;
  if (!context) {
    return nullptr;
  }
  content::WebContents::CreateParams params(context);
  std::unique_ptr<content::WebContents> contents =
      content::WebContents::Create(params);
  content::WebContents* raw = contents.get();
  TabEntry entry;
  entry.contents = std::move(contents);
  entry.observer = std::make_unique<OtfTabObserver>(raw, id, this);
  entry.delegate = std::make_unique<OtfTabWebContentsDelegate>(id, this);
  raw->SetDelegate(entry.delegate.get());
  tabs_[id] = std::move(entry);
  return raw;
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
  // Internal pages (browser://<page>) are served as the UI's "<page>.html" next
  // to the shell. Remember the browser:// URL so the URL bar keeps showing it
  // (see NotifyUrl); normal web pages clear that mapping.
  GURL target(url);
  auto& entry = tabs_[id];
  entry.internal_url.clear();
  entry.internal_target.clear();
  if (target.SchemeIs(kInternalScheme) && !target.host().empty()) {
    const GURL served = ResolveUiUrl().Resolve(std::string(target.host()) + ".html");
    // In dev the UI is served over http, so redirect internal pages there (and
    // remember the browser:// URL for display). In production browser:// is
    // served natively (OtfInternalURLLoaderFactory), so navigate it as-is.
    if (served.is_valid() && !served.SchemeIs(kInternalScheme)) {
      entry.internal_url = url;
      entry.internal_target = served.spec();
      target = served;
    }
  }

  content::NavigationController::LoadURLParams params{target};
  params.transition_type = ui::PAGE_TRANSITION_TYPED;
  wc->GetController().LoadURLWithParams(params);
  Show(id);
  return 0;
}

OtfStatus OtfTabHost::Show(OtfTabHandle id) {
  OtfPlatformWindow* window = OtfPlatformWindow::Get();
  // Hide the previously active tab's view, if any and different.
  if (active_ && active_ != id) {
    if (content::WebContents* prev = Find(active_)) {
      if (window) {
        window->HideTab(prev);
      }
    }
  }
  active_ = id;

  content::WebContents* wc = Find(id);
  if (!wc) {
    return 0;  // model-only tab (never navigated): nothing to paint.
  }
  if (window) {
    window->ShowTab(wc);
  }
  return 0;
}

OtfStatus OtfTabHost::Hide(OtfTabHandle id) {
  if (content::WebContents* wc = Find(id)) {
    if (OtfPlatformWindow* window = OtfPlatformWindow::Get()) {
      window->HideTab(wc);
    }
  }
  return 0;
}

OtfStatus OtfTabHost::Close(OtfTabHandle id) {
  auto it = tabs_.find(id);
  if (it == tabs_.end()) {
    return 0;
  }
  if (auto* wc = it->second.contents.get()) {
    if (OtfPlatformWindow* window = OtfPlatformWindow::Get()) {
      window->HideTab(wc);
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
  if (OtfPlatformWindow* window = OtfPlatformWindow::Get()) {
    window->SetContentBounds(gfx::Rect(x, y, w, h));
  }
}

void OtfTabHost::NotifyTitle(OtfTabHandle id, const std::string& title) {
  if (callbacks_.on_title_changed) {
    callbacks_.on_title_changed(callbacks_.user_data, id, title.c_str());
  }
}

void OtfTabHost::NotifyUrl(OtfTabHandle id, const std::string& url) {
  // For an internal page still on its served "<page>.html" URL, report the
  // browser:// URL so the UI shows/treats it as internal. Any navigation away
  // clears the mapping and reports the real URL.
  std::string reported = url;
  auto it = tabs_.find(id);
  if (it != tabs_.end() && !it->second.internal_url.empty()) {
    if (url == it->second.internal_target) {
      reported = it->second.internal_url;
    } else {
      it->second.internal_url.clear();
      it->second.internal_target.clear();
    }
  }
  if (callbacks_.on_url_changed) {
    callbacks_.on_url_changed(callbacks_.user_data, id, reported.c_str());
  }
}

void OtfTabHost::NotifyLoad(OtfTabHandle id, bool loading) {
  if (callbacks_.on_load_state) {
    callbacks_.on_load_state(callbacks_.user_data, id, loading ? 1 : 0);
  }
}

void OtfTabHost::NotifyContextMenu(OtfTabHandle id,
                                   const std::string& params_json) {
  if (callbacks_.on_context_menu) {
    callbacks_.on_context_menu(callbacks_.user_data, id, params_json.c_str());
  }
}

OtfStatus OtfTabHost::ContextAction(OtfTabHandle id,
                                    const std::string& action,
                                    int x,
                                    int y) {
  content::WebContents* wc = Find(id);
  if (!wc) {
    return -1;
  }
  if (action == "copy") {
    wc->Copy();
  } else if (action == "cut") {
    wc->Cut();
  } else if (action == "paste") {
    wc->Paste();
  } else if (action == "pasteMatchStyle") {
    wc->PasteAndMatchStyle();
  } else if (action == "selectAll") {
    wc->SelectAll();
  } else if (action == "undo") {
    wc->Undo();
  } else if (action == "redo") {
    wc->Redo();
  } else if (action == "copyImage" || action == "saveImage") {
    if (content::RenderFrameHost* rfh = wc->GetPrimaryMainFrame()) {
      if (action == "copyImage") {
        rfh->CopyImageAt(x, y);
      } else {
        rfh->SaveImageAt(x, y);
      }
    }
  } else {
    return -1;
  }
  return 0;
}

}  // namespace otf
