#pragma once

#include "envoy/server/admin.h"

#include "common/common/assert.h"

#include "server/admin/config_tracker_impl.h"

namespace Envoy {
namespace Server {

/**
 * Config-validation-only implementation Server::Admin. This implementation is
 * needed because Admin is referenced by components of the server that add and
 * remove handlers.
 */
class ValidationAdmin : public Admin {
public:
  bool addHandler(const std::string&, const std::string&, HandlerCb, bool, bool) override;
  bool removeHandler(const std::string&) override;
  const Network::Socket& socket() override;
  ConfigTracker& getConfigTracker() override;
  void startHttpListener(const std::string& access_log_path, const std::string& address_out_path,
                         Network::Address::InstanceConstSharedPtr address,
                         const Network::Socket::OptionsSharedPtr&,
                         Stats::ScopePtr&& listener_scope) override;
  Http::Code request(absl::string_view path_and_query, absl::string_view method,
                     Http::ResponseHeaderMap& response_headers, std::string& body) override;
  void addListenerToHandler(Network::ConnectionHandler* handler) override;

private:
  ConfigTrackerImpl config_tracker_;
};

} // namespace Server
} // namespace Envoy
