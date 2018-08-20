#include <chrono>
#include <memory>
#include <string>

#include "common/config/filter_json.h"
#include "common/filesystem/filesystem_impl.h"
#include "common/http/message_impl.h"
#include "common/network/address_impl.h"

#include "extensions/filters/network/client_ssl_auth/client_ssl_auth.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::ReturnNew;
using testing::ReturnRef;
using testing::WithArg;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ClientSslAuth {

TEST(ClientSslAuthAllowedPrincipalsTest, EmptyString) {
  AllowedPrincipals principals;
  principals.add("");
  EXPECT_EQ(0UL, principals.size());
}

TEST(ClientSslAuthConfigTest, BadClientSslAuthConfig) {
  std::string json = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "auth_api_cluster" : "fake_cluster",
    "ip_white_list": ["192.168.3.0/24"],
    "test" : "a"
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json);
  envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth proto_config{};
  EXPECT_THROW(Envoy::Config::FilterJson::translateClientSslAuthFilter(*json_config, proto_config),
               Json::Exception);
}

class ClientSslAuthFilterTest : public testing::Test {
public:
  ClientSslAuthFilterTest()
      : request_(&cm_.async_client_), interval_timer_(new Event::MockTimer(&dispatcher_)) {}
  ~ClientSslAuthFilterTest() { tls_.shutdownThread(); }

  void setup() {
    std::string json = R"EOF(
    {
      "auth_api_cluster": "vpn",
      "stat_prefix": "vpn",
      "ip_white_list":
        [ "1.2.3.4/32",
          "2001:abcd::/64"
        ]
    }
    )EOF";

    Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json);
    envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth proto_config{};
    Envoy::Config::FilterJson::translateClientSslAuthFilter(*json_config, proto_config);
    EXPECT_CALL(cm_, get("vpn"));
    setupRequest();
    config_ =
        ClientSslAuthConfig::create(proto_config, tls_, cm_, dispatcher_, stats_store_, random_);

    createAuthFilter();
  }

  void createAuthFilter() {
    filter_callbacks_.connection_.callbacks_.clear();
    instance_.reset(new ClientSslAuthFilter(config_));
    instance_->initializeReadFilterCallbacks(filter_callbacks_);

    // NOP currently.
    instance_->onAboveWriteBufferHighWatermark();
    instance_->onBelowWriteBufferLowWatermark();
  }

  void setupRequest() {
    EXPECT_CALL(cm_, httpAsyncClientForCluster("vpn")).WillOnce(ReturnRef(cm_.async_client_));
    EXPECT_CALL(cm_.async_client_, send_(_, _, _))
        .WillOnce(Invoke(
            [this](Http::MessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                   absl::optional<std::chrono::milliseconds>) -> Http::AsyncClient::Request* {
              callbacks_ = &callbacks;
              return &request_;
            }));
  }

  NiceMock<ThreadLocal::MockInstance> tls_;
  Upstream::MockClusterManager cm_;
  Event::MockDispatcher dispatcher_;
  Http::MockAsyncClientRequest request_;
  ClientSslAuthConfigSharedPtr config_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  std::unique_ptr<ClientSslAuthFilter> instance_;
  Event::MockTimer* interval_timer_;
  Http::AsyncClient::Callbacks* callbacks_;
  Ssl::MockConnection ssl_;
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Runtime::MockRandomGenerator> random_;
};

TEST_F(ClientSslAuthFilterTest, NoCluster) {
  std::string json = R"EOF(
  {
    "auth_api_cluster": "bad_cluster",
    "stat_prefix": "bad_cluster"
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json);
  envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth proto_config{};
  Envoy::Config::FilterJson::translateClientSslAuthFilter(*json_config, proto_config);
  EXPECT_CALL(cm_, get("bad_cluster")).WillOnce(Return(nullptr));
  EXPECT_THROW(
      ClientSslAuthConfig::create(proto_config, tls_, cm_, dispatcher_, stats_store_, random_),
      EnvoyException);
}

TEST_F(ClientSslAuthFilterTest, NoSsl) {
  setup();
  Buffer::OwnedImpl dummy("hello");

  // Check no SSL case, mulitple iterations.
  EXPECT_CALL(filter_callbacks_.connection_, ssl()).WillOnce(Return(nullptr));
  EXPECT_EQ(Network::FilterStatus::Continue, instance_->onNewConnection());
  EXPECT_EQ(Network::FilterStatus::Continue, instance_->onData(dummy, false));
  EXPECT_EQ(Network::FilterStatus::Continue, instance_->onData(dummy, false));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1U, stats_store_.counter("auth.clientssl.vpn.auth_no_ssl").value());

  EXPECT_CALL(request_, cancel());
}

TEST_F(ClientSslAuthFilterTest, Ssl) {
  InSequence s;

  setup();
  Buffer::OwnedImpl dummy("hello");

  // Create a new filter for an SSL connection, with no backing auth data yet.
  createAuthFilter();
  ON_CALL(filter_callbacks_.connection_, ssl()).WillByDefault(Return(&ssl_));
  filter_callbacks_.connection_.remote_address_ =
      std::make_shared<Network::Address::Ipv4Instance>("192.168.1.1");
  std::string expected_sha_1("digest");
  EXPECT_CALL(ssl_, sha256PeerCertificateDigest()).WillOnce(ReturnRef(expected_sha_1));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_EQ(Network::FilterStatus::StopIteration, instance_->onNewConnection());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::Connected);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  // Respond.
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(
      new Buffer::OwnedImpl(Filesystem::fileReadToEnd(TestEnvironment::runfilesPath(
          "test/extensions/filters/network/client_ssl_auth/test_data/vpn_response_1.json"))));
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ(1U, stats_store_.gauge("auth.clientssl.vpn.total_principals").value());

  // Create a new filter for an SSL connection with an authorized cert.
  createAuthFilter();
  filter_callbacks_.connection_.remote_address_ =
      std::make_shared<Network::Address::Ipv4Instance>("192.168.1.1");
  std::string expected_sha_2("1b7d42ef0025ad89c1c911d6c10d7e86a4cb7c5863b2980abcbad1895f8b5314");
  EXPECT_CALL(ssl_, sha256PeerCertificateDigest()).WillOnce(ReturnRef(expected_sha_2));
  EXPECT_EQ(Network::FilterStatus::StopIteration, instance_->onNewConnection());
  EXPECT_CALL(filter_callbacks_, continueReading());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::Connected);
  EXPECT_EQ(Network::FilterStatus::Continue, instance_->onData(dummy, false));
  EXPECT_EQ(Network::FilterStatus::Continue, instance_->onData(dummy, false));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  // White list case.
  createAuthFilter();
  filter_callbacks_.connection_.remote_address_ =
      std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4");
  EXPECT_EQ(Network::FilterStatus::StopIteration, instance_->onNewConnection());
  EXPECT_CALL(filter_callbacks_, continueReading());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::Connected);
  EXPECT_EQ(Network::FilterStatus::Continue, instance_->onData(dummy, false));
  EXPECT_EQ(Network::FilterStatus::Continue, instance_->onData(dummy, false));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  // IPv6 White list case.
  createAuthFilter();
  filter_callbacks_.connection_.remote_address_ =
      std::make_shared<Network::Address::Ipv6Instance>("2001:abcd::1");
  EXPECT_EQ(Network::FilterStatus::StopIteration, instance_->onNewConnection());
  EXPECT_CALL(filter_callbacks_, continueReading());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::Connected);
  EXPECT_EQ(Network::FilterStatus::Continue, instance_->onData(dummy, false));
  EXPECT_EQ(Network::FilterStatus::Continue, instance_->onData(dummy, false));

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(1U, stats_store_.counter("auth.clientssl.vpn.update_success").value());
  EXPECT_EQ(2U, stats_store_.counter("auth.clientssl.vpn.auth_ip_white_list").value());
  EXPECT_EQ(1U, stats_store_.counter("auth.clientssl.vpn.auth_digest_match").value());
  EXPECT_EQ(1U, stats_store_.counter("auth.clientssl.vpn.auth_digest_no_match").value());

  // Interval timer fires.
  setupRequest();
  interval_timer_->callback_();

  // Error response.
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  message.reset(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "503"}}}));
  callbacks_->onSuccess(std::move(message));

  // Interval timer fires.
  setupRequest();
  interval_timer_->callback_();

  // Parsing error
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  message.reset(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl("bad_json"));
  callbacks_->onSuccess(std::move(message));

  // Interval timer fires.
  setupRequest();
  interval_timer_->callback_();

  // No response failure.
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onFailure(Http::AsyncClient::FailureReason::Reset);

  // Interval timer fires, cannot obtain async client.
  EXPECT_CALL(cm_, httpAsyncClientForCluster("vpn")).WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Http::MessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const absl::optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            callbacks.onSuccess(Http::MessagePtr{new Http::ResponseMessageImpl(
                Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "503"}}})});
            return nullptr;
          }));
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  interval_timer_->callback_();

  EXPECT_EQ(4U, stats_store_.counter("auth.clientssl.vpn.update_failure").value());
}

} // namespace ClientSslAuth
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
