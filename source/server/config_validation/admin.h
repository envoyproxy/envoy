#pragma once

#include "envoy/network/listen_socket.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "source/common/common/assert.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/server/admin/config_tracker_impl.h"

namespace Envoy {
namespace Server {

/**
 * Config-validation-only implementation Server::Admin. This implementation is
 * needed because Admin is referenced by components of the server that add and
 * remove handlers.
 */
class ValidationAdmin : public Admin {
public:
  // We want to implement the socket interface without implementing the http listener function.
  // This is useful for TAP because it wants to emit warnings when the address type is UDS
  explicit ValidationAdmin(Network::Address::InstanceConstSharedPtr address)
      : socket_(address ? std::make_shared<Network::TcpListenSocket>(nullptr, std::move(address),
                                                                     nullptr)
                        : nullptr) {}
  bool addHandler(const std::string&, const std::string&, HandlerCb, bool, bool,
                  const ParamDescriptorVec& = {}) override;
  bool addStreamingHandler(const std::string&, const std::string&, GenRequestFn, bool, bool,
                           const ParamDescriptorVec& = {}) override;
  bool removeHandler(const std::string&) override;
  const Network::Socket& socket() override;
  ConfigTracker& getConfigTracker() override;
  void startHttpListener(std::list<AccessLog::InstanceSharedPtr> access_logs,
                         Network::Address::InstanceConstSharedPtr address,
                         Network::Socket::OptionsSharedPtr) override;
  Http::Code request(absl::string_view path_and_query, absl::string_view method,
                     Http::ResponseHeaderMap& response_headers, std::string& body) override;
  void addListenerToHandler(Network::ConnectionHandler* handler) override;
  uint32_t concurrency() const override { return 1; }
  void closeSocket() override {}

private:
  ConfigTrackerImpl config_tracker_;
  Network::SocketSharedPtr socket_;
};

} // namespace Server
} // namespace Envoy
