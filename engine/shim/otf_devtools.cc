#include "otf/shim/otf_devtools.h"

#include <memory>
#include <string>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_manager_delegate.h"
#include "content/public/browser/devtools_socket_factory.h"
#include "content/public/common/content_switches.h"
#include "otf/shim/otf_browser_context.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#include "net/log/net_log_source.h"
#include "net/socket/tcp_server_socket.h"

namespace otf {

namespace {

constexpr int kBackLog = 10;

// TCP DevTools listen socket, mirroring content_shell's factory: bind the
// requested (or ephemeral) port on the loopback address so CDP clients can
// enumerate and attach to targets over HTTP/WebSocket.
class TCPServerSocketFactory : public content::DevToolsSocketFactory {
 public:
  TCPServerSocketFactory(std::string address, uint16_t port)
      : address_(std::move(address)), port_(port) {}

  TCPServerSocketFactory(const TCPServerSocketFactory&) = delete;
  TCPServerSocketFactory& operator=(const TCPServerSocketFactory&) = delete;

 private:
  std::unique_ptr<net::ServerSocket> CreateForHttpServer() override {
    auto socket =
        std::make_unique<net::TCPServerSocket>(nullptr, net::NetLogSource());
    if (socket->ListenWithAddressAndPort(address_, port_, kBackLog) != net::OK) {
      return nullptr;
    }
    return socket;
  }

  std::unique_ptr<net::ServerSocket> CreateForTethering(
      std::string* /*out_name*/) override {
    return nullptr;
  }

  std::string address_;
  uint16_t port_;
};

std::unique_ptr<content::DevToolsSocketFactory> CreateSocketFactory(
    const base::CommandLine& command_line) {
  uint16_t port = 0;
  int parsed_port = 0;
  if (base::StringToInt(
          command_line.GetSwitchValueASCII(switches::kRemoteDebuggingPort),
          &parsed_port) &&
      parsed_port >= 0 && parsed_port < 65535) {
    port = static_cast<uint16_t>(parsed_port);
  }

  // Bind loopback only; remote CDP exposure is intentionally not supported.
  std::string address = net::IPAddress::IPv4Localhost().ToString();
  return std::make_unique<TCPServerSocketFactory>(std::move(address), port);
}

// Minimal delegate: pins the default browser context; no custom CDP domains.
class OtfDevToolsManagerDelegate : public content::DevToolsManagerDelegate {
 public:
  explicit OtfDevToolsManagerDelegate(content::BrowserContext* context)
      : context_(context) {}

  OtfDevToolsManagerDelegate(const OtfDevToolsManagerDelegate&) = delete;
  OtfDevToolsManagerDelegate& operator=(const OtfDevToolsManagerDelegate&) =
      delete;

  content::BrowserContext* GetDefaultBrowserContext() override {
    return context_;
  }

 private:
  raw_ptr<content::BrowserContext> context_;
};

}  // namespace

std::unique_ptr<content::DevToolsManagerDelegate>
CreateDevToolsManagerDelegate() {
  return std::make_unique<OtfDevToolsManagerDelegate>(OtfBrowserContext::Get());
}

void MaybeStartDevToolsServer(content::BrowserContext* browser_context) {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  if (!command_line.HasSwitch(switches::kRemoteDebuggingPort)) {
    return;
  }
  content::DevToolsAgentHost::StartRemoteDebuggingServer(
      CreateSocketFactory(command_line), browser_context->GetPath(),
      base::FilePath());
}

void StopDevToolsServer() {
  content::DevToolsAgentHost::StopRemoteDebuggingServer();
}

}  // namespace otf
