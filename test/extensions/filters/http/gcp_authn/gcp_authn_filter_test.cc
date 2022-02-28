#include "test/common/http/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/server/mocks.h"

//#include "test/extensions/filters/http/gcp_authn/mocks.h"
#include "source/common/http/header_map_impl.h"

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "source/extensions/filters/http/gcp_authn/gcp_authn_filter.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthentication {
namespace {

using envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig;
using Http::MockAsyncClient;
using Http::TestRequestHeaderMapImpl;
using Server::Configuration::MockFactoryContext;
using testing::_;
using testing::ByMove;
using testing::Invoke;
using testing::NiceMock;
using Upstream::MockThreadLocalCluster;

class GcpAuthnFilterTest : public testing::Test {
public:
  // TODO(tyxia) UpperCase Setup override
  void setUp() {
    // Set up the mock http async client.
    GcpAuthnFilterConfig config;
    // client_ = std::make_unique<MockGcpAuthnClient>(context_, config);
    client_ = std::make_unique<GcpAuthnClient>(config, context_);
    EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster(_))
        .WillRepeatedly(Return(&thread_local_cluster_));
    ON_CALL(thread_local_cluster_, httpAsyncClient()).WillByDefault(ReturnRef(async_client_));
    // EXPECT_CALL(thread_local_cluster_.async_client_, send_(_, _, _))
    //     .WillRepeatedly(Invoke([this](Envoy::Http::RequestMessagePtr& message,
    //                                   Envoy::Http::AsyncClient::Callbacks& callback,
    //                                   const Envoy::Http::AsyncClient::RequestOptions&) {
    //       // TODO(tyxia) Re-visit
    //       message_.swap(message);
    //       callback_ = &callback;
    //       return nullptr;
    //     }));
  }

  // private:
protected:
  // Context
  NiceMock<MockFactoryContext> context_;
  NiceMock<MockThreadLocalCluster> thread_local_cluster_;
  NiceMock<MockAsyncClient> async_client_;
  Envoy::Http::MockAsyncClientRequest client_request_{&thread_local_cluster_.async_client_};

  // Mocks for http request.
  Envoy::Http::AsyncClient::Callbacks* callback_;
  Envoy::Http::RequestMessagePtr message_;

  // TODO(tyxia) I need to create the client or mock client
  // std::unique_ptr<MockGcpAuthnClient> client_;
  std::unique_ptr<GcpAuthnClient> client_;
  std::string token_url_ = "http://iam/uri_suffix";
};

TEST_F(GcpAuthnFilterTest, Basic) {
  setUp();
  // Setup fake remote request.
  Envoy::Http::RequestHeaderMapPtr headers(
      new Envoy::Http::TestRequestHeaderMapImpl({{":method", "GET"}, {":authority", "TestValue"}}));
  Http::RequestMessagePtr request = client_->buildRequest("GET", token_url_);
  client_->sendRequest();
  // EXPECT_CALL(*client_, buildRequest("GET", token_url_))
  //     .Times(1)
  //     .WillRepeatedly(
  //         Return(ByMove(std::make_unique<Envoy::Http::RequestMessageImpl>(
  //             std::move(headers)))));

  // // Start class under test.
  // setUp();

  // // Assert remote call matches.
  // //ASSERT_EQ(call_count_, 1);
  // EXPECT_EQ(message_->headers().Method()->value().getStringView(), "GET");
  // EXPECT_EQ(message_->headers().Host()->value().getStringView(), "TestValue");
}

} // namespace
} // namespace GcpAuthentication
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
