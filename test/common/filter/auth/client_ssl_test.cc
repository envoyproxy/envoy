#include "common/filesystem/filesystem_impl.h"
#include "common/filter/auth/client_ssl.h"
#include "common/http/message_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::ReturnNew;
using testing::ReturnRefOfCopy;
using testing::WithArg;

namespace Filter {
namespace Auth {
namespace ClientSsl {

TEST(ClientSslAuthAllowedPrincipalsTest, EmptyString) {
  AllowedPrincipals principals;
  principals.add("");
  EXPECT_EQ(0UL, principals.size());
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
      "ip_white_list": [ "1.2.3.4/32" ]
    }
    )EOF";

    Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
    EXPECT_CALL(cm_, get("vpn"));
    setupRequest();
    config_ = Config::create(*loader, tls_, cm_, dispatcher_, stats_store_, random_);

    createAuthFilter();
  }

  void createAuthFilter() {
    filter_callbacks_.connection_.callbacks_.clear();
    instance_.reset(new Instance(config_));
    instance_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  void setupRequest() {
    EXPECT_CALL(cm_, httpAsyncClientForCluster("vpn")).WillOnce(ReturnRef(cm_.async_client_));
    EXPECT_CALL(cm_.async_client_, send_(_, _, _))
        .WillOnce(
            Invoke([this](Http::MessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                          Optional<std::chrono::milliseconds>) -> Http::AsyncClient::Request* {
              callbacks_ = &callbacks;
              return &request_;
            }));
  }

  NiceMock<ThreadLocal::MockInstance> tls_;
  Upstream::MockClusterManager cm_;
  Event::MockDispatcher dispatcher_;
  Http::MockAsyncClientRequest request_;
  ConfigPtr config_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  std::unique_ptr<Instance> instance_;
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

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  EXPECT_CALL(cm_, get("bad_cluster")).WillOnce(Return(nullptr));
  EXPECT_THROW(Config::create(*loader, tls_, cm_, dispatcher_, stats_store_, random_),
               EnvoyException);
}

TEST_F(ClientSslAuthFilterTest, NoSsl) {
  setup();
  Buffer::OwnedImpl dummy("hello");

  // Check no SSL case, mulitple iterations.
  EXPECT_CALL(filter_callbacks_.connection_, ssl()).WillOnce(Return(nullptr));
  EXPECT_EQ(Network::FilterStatus::Continue, instance_->onNewConnection());
  EXPECT_EQ(Network::FilterStatus::Continue, instance_->onData(dummy));
  EXPECT_EQ(Network::FilterStatus::Continue, instance_->onData(dummy));
  filter_callbacks_.connection_.raiseEvents(Network::ConnectionEvent::RemoteClose);

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
  EXPECT_CALL(filter_callbacks_.connection_, remoteAddress())
      .WillOnce(ReturnRefOfCopy(std::string("192.168.1.1")));
  EXPECT_CALL(ssl_, sha256PeerCertificateDigest()).WillOnce(Return("digest"));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_EQ(Network::FilterStatus::StopIteration, instance_->onNewConnection());
  filter_callbacks_.connection_.raiseEvents(Network::ConnectionEvent::Connected);
  filter_callbacks_.connection_.raiseEvents(Network::ConnectionEvent::RemoteClose);

  // Respond.
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body(Buffer::InstancePtr{new Buffer::OwnedImpl(
      Filesystem::fileReadToEnd("test/common/filter/auth/test_data/vpn_response_1.json"))});
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ(1U, stats_store_.gauge("auth.clientssl.vpn.total_principals").value());

  // Create a new filter for an SSL connection with an authorized cert.
  createAuthFilter();
  EXPECT_CALL(filter_callbacks_.connection_, remoteAddress())
      .WillOnce(ReturnRefOfCopy(std::string("192.168.1.1")));
  EXPECT_CALL(ssl_, sha256PeerCertificateDigest())
      .WillOnce(Return("1b7d42ef0025ad89c1c911d6c10d7e86a4cb7c5863b2980abcbad1895f8b5314"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, instance_->onNewConnection());
  EXPECT_CALL(filter_callbacks_, continueReading());
  filter_callbacks_.connection_.raiseEvents(Network::ConnectionEvent::Connected);
  EXPECT_EQ(Network::FilterStatus::Continue, instance_->onData(dummy));
  EXPECT_EQ(Network::FilterStatus::Continue, instance_->onData(dummy));
  filter_callbacks_.connection_.raiseEvents(Network::ConnectionEvent::RemoteClose);

  // White list case.
  createAuthFilter();
  EXPECT_CALL(filter_callbacks_.connection_, remoteAddress())
      .WillOnce(ReturnRefOfCopy(std::string("1.2.3.4")));
  EXPECT_EQ(Network::FilterStatus::StopIteration, instance_->onNewConnection());
  EXPECT_CALL(filter_callbacks_, continueReading());
  filter_callbacks_.connection_.raiseEvents(Network::ConnectionEvent::Connected);
  EXPECT_EQ(Network::FilterStatus::Continue, instance_->onData(dummy));
  EXPECT_EQ(Network::FilterStatus::Continue, instance_->onData(dummy));
  filter_callbacks_.connection_.raiseEvents(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1U, stats_store_.counter("auth.clientssl.vpn.update_success").value());
  EXPECT_EQ(1U, stats_store_.counter("auth.clientssl.vpn.auth_ip_white_list").value());
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
  message->body(Buffer::InstancePtr{new Buffer::OwnedImpl("bad_json")});
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
      .WillOnce(
          Invoke([&](Http::MessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            callbacks.onSuccess(Http::MessagePtr{new Http::ResponseMessageImpl(
                Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "503"}}})});
            return nullptr;
          }));
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  interval_timer_->callback_();

  EXPECT_EQ(4U, stats_store_.counter("auth.clientssl.vpn.update_failure").value());
}

} // ClientSsl
} // Auth
} // Filter
