#pragma once

#include <string>

#include "envoy/server/admin.h"

#include "test/mocks/network/socket.h"

#include "absl/strings/string_view.h"
#include "config_tracker.h"
#include "gmock/gmock.h"

using testing::NiceMock;

namespace Envoy {
namespace Server {

class MockAdmin : public Admin {
public:
  MockAdmin();
  ~MockAdmin() override;

  // Server::Admin
  MOCK_METHOD(bool, addHandler,
              (const std::string& prefix, const std::string& help_text, HandlerCb callback,
               bool removable, bool mutates_server_state, const ParamDescriptorVec& params));
  MOCK_METHOD(bool, addStreamingHandler,
              (const std::string& prefix, const std::string& help_text, GenRequestFn callback,
               bool removable, bool mutates_server_state, const ParamDescriptorVec& params));
  MOCK_METHOD(bool, removeHandler, (const std::string& prefix));
  MOCK_METHOD(Network::Socket&, socket, ());
  MOCK_METHOD(ConfigTracker&, getConfigTracker, ());
  MOCK_METHOD(void, startHttpListener,
              (std::list<AccessLog::InstanceSharedPtr> access_logs,
               Network::Address::InstanceConstSharedPtr address,
               Network::Socket::OptionsSharedPtr socket_options));
  MOCK_METHOD(Http::Code, request,
              (absl::string_view path_and_query, absl::string_view method,
               Http::ResponseHeaderMap& response_headers, std::string& body));
  MOCK_METHOD(void, addListenerToHandler, (Network::ConnectionHandler * handler));
  MOCK_METHOD(uint32_t, concurrency, (), (const));
  MOCK_METHOD(void, closeSocket, ());

  NiceMock<MockConfigTracker> config_tracker_;
  NiceMock<Network::MockSocket> socket_;
};

} // namespace Server
} // namespace Envoy
