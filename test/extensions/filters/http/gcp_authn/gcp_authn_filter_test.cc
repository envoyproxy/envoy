#include "test/common/http/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthentication {
namespace {

using Server::Configuration::MockFactoryContext;
using Upstream::MockThreadLocalCluster;
using testing::Invoke;
using testing::_;
using testing::NiceMock;

class GcpAuthnFilterTest : public testing::Test {
public:
  void setUp() {
    // Setup mock http async client.
    EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster(_))
        .WillRepeatedly(Return(&thread_local_cluster_));
    EXPECT_CALL(thread_local_cluster_.async_client_, send_(_, _, _))
        .WillRepeatedly(Invoke([this](Envoy::Http::RequestMessagePtr& message,
                                      Envoy::Http::AsyncClient::Callbacks& callback,
                                      const Envoy::Http::AsyncClient::RequestOptions&) {
          // TODO(tyxia) Re-visit
          message_.swap(message);
          callback_ = &callback;
          return nullptr;
        }));
  }

private:
  // Context
  NiceMock<MockFactoryContext> context_;
  NiceMock<MockThreadLocalCluster> thread_local_cluster_;
  Envoy::Http::MockAsyncClientRequest client_request_{&thread_local_cluster_.async_client_};

  // Mocks for http request.
  Envoy::Http::AsyncClient::Callbacks* callback_;
  Envoy::Http::RequestMessagePtr message_;
};

} // namespace
} // namespace GcpAuthentication
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

