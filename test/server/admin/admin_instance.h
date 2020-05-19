#pragma once

#include "server/admin/admin.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

#include "absl/strings/match.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

class AdminInstanceTest : public testing::TestWithParam<Network::Address::IpVersion> {
public:
  AdminInstanceTest();

  Http::Code runCallback(absl::string_view path_and_query,
                         Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                         absl::string_view method, absl::string_view body = absl::string_view());

  Http::Code getCallback(absl::string_view path_and_query,
                         Http::ResponseHeaderMap& response_headers, Buffer::Instance& response);

  Http::Code postCallback(absl::string_view path_and_query,
                          Http::ResponseHeaderMap& response_headers, Buffer::Instance& response);

  std::string address_out_path_;
  std::string cpu_profile_path_;
  NiceMock<MockInstance> server_;
  Stats::IsolatedStoreImpl listener_scope_;
  AdminImpl admin_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Server::AdminFilter admin_filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
};

} // namespace Server
} // namespace Envoy
