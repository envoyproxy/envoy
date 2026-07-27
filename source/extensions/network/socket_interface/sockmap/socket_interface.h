#pragma once

#include <optional>

#include "envoy/registry/registry.h"
#include "envoy/server/bootstrap_extension_config.h"

#include "source/common/common/assert.h"
#include "source/common/network/socket_interface.h"
#include "source/common/network/socket_interface_impl.h"
#include "source/extensions/network/socket_interface/sockmap/bpf_datapath.h"

namespace Envoy {
namespace Network {

class SockmapSocketInterfaceExtension;

// Socket interface that accelerates same-host TCP hops with eBPF. It reuses the default socket
// creation and only swaps the stream IoHandle for one that registers the socket into the sockhash.
// When the datapath is not loaded, every socket uses the standard datapath unchanged.
class SockmapSocketInterface : public SocketInterfaceImpl {
public:
  // Server::Configuration::BootstrapExtensionFactory
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override { return "envoy.extensions.network.socket_interface.sockmap"; }

  // Non-owning back pointer to the bootstrap extension that owns the datapath. Set once when the
  // extension is created at startup, before any worker runs, and cleared when it is destroyed. The
  // const hot path reads it, keeping the config derived state off this process wide singleton.
  SockmapSocketInterfaceExtension* extension_{nullptr};

protected:
  IoHandlePtr makeSocket(int socket_fd, bool socket_v6only, Socket::Type socket_type,
                         std::optional<int> domain,
                         const SocketCreationOptions& options) const override;

  // Builds the datapath from the parsed config. Tests override this to observe the config and
  // inject a datapath without loading eBPF programs.
  virtual BpfDatapathSharedPtr createDatapath(const BpfDatapathConfig& config);
};

// Bootstrap extension that owns the datapath and registration policy. The state lives here rather
// than on the socket interface singleton so the singleton stays free of mutable state, matching the
// pattern other socket interfaces use. Its lifetime spans the whole server, so the back pointer it
// installs on the interface is valid for every socket the workers create.
class SockmapSocketInterfaceExtension : public SocketInterfaceExtension {
public:
  SockmapSocketInterfaceExtension(SockmapSocketInterface& sock_interface,
                                  BpfDatapathSharedPtr datapath, bool register_user_space_sockets)
      : SocketInterfaceExtension(sock_interface), datapath_(std::move(datapath)),
        register_user_space_sockets_(register_user_space_sockets) {
    // The interface is a process wide singleton, so only one extension may own its back pointer.
    ASSERT(sock_interface.extension_ == nullptr);
    sock_interface.extension_ = this;
  }
  ~SockmapSocketInterfaceExtension() override {
    // Clear the back pointer only if it still refers to this extension, so a second extension that
    // had already overwritten it is not nulled out from under the interface.
    auto& sock_interface = static_cast<SockmapSocketInterface&>(sock_interface_);
    if (sock_interface.extension_ == this) {
      sock_interface.extension_ = nullptr;
    }
  }

  const BpfDatapathSharedPtr& datapath() const { return datapath_; }
  bool registerUserSpaceSockets() const { return register_user_space_sockets_; }

private:
  const BpfDatapathSharedPtr datapath_;
  const bool register_user_space_sockets_;
};

DECLARE_FACTORY(SockmapSocketInterface);

} // namespace Network
} // namespace Envoy
