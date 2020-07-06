#pragma once

#include <string>

#include "envoy/server/admin.h"

#include "absl/strings/string_view.h"
#include "config_tracker.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
class MockAdmin : public Admin {
public:
  MockAdmin();
  ~MockAdmin() override;

  // Server::Admin
  MOCK_METHOD(bool, addHandler,
              (const std::string& prefix, const std::string& help_text, HandlerCb callback,
               bool removable, bool mutates_server_state));
  MOCK_METHOD(bool, removeHandler, (const std::string& prefix));
  MOCK_METHOD(Network::Socket&, socket, ());
  MOCK_METHOD(ConfigTracker&, getConfigTracker, ());
  MOCK_METHOD(void, startHttpListener,
              (const std::string& access_log_path, const std::string& address_out_path,
               Network::Address::InstanceConstSharedPtr address,
               const Network::Socket::OptionsSharedPtr& socket_options,
               Stats::ScopePtr&& listener_scope));
  MOCK_METHOD(Http::Code, request,
              (absl::string_view path_and_query, absl::string_view method,
               Http::ResponseHeaderMap& response_headers, std::string& body));
  MOCK_METHOD(void, addListenerToHandler, (Network::ConnectionHandler * handler));

  ::testing::NiceMock<MockConfigTracker> config_tracker_;
};
} // namespace Server
} // namespace Envoy
