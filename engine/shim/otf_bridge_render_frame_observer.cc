#include "otf/shim/otf_bridge_render_frame_observer.h"

#include <utility>

#include "base/functional/bind.h"
#include "content/public/renderer/render_frame.h"
#include "gin/converter.h"
#include "gin/function_template.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/scheduler/web_agent_group_scheduler.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "v8/include/v8.h"

namespace otf {

OtfBridgeRenderFrameObserver::OtfBridgeRenderFrameObserver(
    content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame) {}

OtfBridgeRenderFrameObserver::~OtfBridgeRenderFrameObserver() = default;

void OtfBridgeRenderFrameObserver::OnDestruct() {
  delete this;
}

void OtfBridgeRenderFrameObserver::DidClearWindowObject() {
  // TODO(security): only install on internal browser:// UI frames; web content
  // must not see window.otf (mirror the content-permission whitelist).
  blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
  if (!frame) {
    return;
  }
  v8::Isolate* isolate = frame->GetAgentGroupScheduler()->Isolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Context> context = frame->MainWorldScriptContext();
  if (context.IsEmpty()) {
    return;
  }
  v8::Context::Scope context_scope(context);

  EnsureConnected();

  v8::Local<v8::Object> otf = v8::Object::New(isolate);
  otf->Set(context, gin::StringToSymbol(isolate, "postMessage"),
           gin::CreateFunctionTemplate(
               isolate,
               base::BindRepeating(&OtfBridgeRenderFrameObserver::OnPostMessage,
                                   weak_factory_.GetWeakPtr()))
               ->GetFunction(context)
               .ToLocalChecked())
      .Check();
  context->Global()
      ->Set(context, gin::StringToSymbol(isolate, "otf"), otf)
      .Check();
}

void OtfBridgeRenderFrameObserver::EnsureConnected() {
  if (host_.is_bound()) {
    return;
  }
  render_frame()->GetBrowserInterfaceBroker().GetInterface(
      host_.BindNewPipeAndPassReceiver());
  host_->Connect(client_receiver_.BindNewPipeAndPassRemote());
}

void OtfBridgeRenderFrameObserver::OnPostMessage(
    const std::string& message_json) {
  EnsureConnected();
  if (host_.is_bound()) {
    host_->Call(message_json);
  }
}

void OtfBridgeRenderFrameObserver::Deliver(const std::string& message_json) {
  blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
  if (!frame) {
    return;
  }
  v8::Isolate* isolate = frame->GetAgentGroupScheduler()->Isolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Context> context = frame->MainWorldScriptContext();
  if (context.IsEmpty()) {
    return;
  }
  v8::Context::Scope context_scope(context);
  v8::Local<v8::Object> global = context->Global();
  v8::Local<v8::Value> receiver;
  if (!global->Get(context, gin::StringToV8(isolate, "__otfReceive"))
           .ToLocal(&receiver) ||
      !receiver->IsFunction()) {
    return;
  }
  v8::Local<v8::Value> arg = gin::StringToV8(isolate, message_json);
  frame->CallFunctionEvenIfScriptDisabled(receiver.As<v8::Function>(), global,
                                          1, &arg);
}

}  // namespace otf
