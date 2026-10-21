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
               bool removable, bool mutates_server_state, const ParamDescriptorVec& params),
              (override));
  MOCK_METHOD(bool, addStreamingHandler,
              (const std::string& prefix, const std::string& help_text, GenRequestFn callback,
               bool removable, bool mutates_server_state, const ParamDescriptorVec& params),
              (override));
  MOCK_METHOD(bool, removeHandler, (const std::string& prefix), (override));
  MOCK_METHOD(Network::Socket&, socket, (), (override));
  MOCK_METHOD(ConfigTracker&, getConfigTracker, (), (override));
  MOCK_METHOD(void, startHttpListener,
              (AccessLog::InstanceSharedPtrVector access_logs,
               Network::Address::InstanceConstSharedPtr address,
               Network::Socket::OptionsSharedPtr socket_options),
              (override));
  MOCK_METHOD(Http::Code, request,
              (absl::string_view path_and_query, absl::string_view method,
               Http::ResponseHeaderMap& response_headers, std::string& body),
              (override));
  MOCK_METHOD(void, addListenerToHandler, (Network::ConnectionHandler * handler), (override));
  MOCK_METHOD(uint32_t, concurrency, (), (const, override));
  MOCK_METHOD(void, closeSocket, (), (override));
  MOCK_METHOD(RequestPtr, makeRequest, (AdminStream & admin_stream), (const, override));

  NiceMock<MockConfigTracker> config_tracker_;
  NiceMock<Network::MockSocket> socket_;
};

} // namespace Server
} // namespace Envoy
