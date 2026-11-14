#include <optional>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/http1/http1_handshake_initiator.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

namespace {

constexpr absl::string_view kNodeHeader = "x-envoy-reverse-tunnel-node-id";
constexpr absl::string_view kClusterHeader = "x-envoy-reverse-tunnel-cluster-id";
constexpr absl::string_view kTenantHeader = "x-envoy-reverse-tunnel-tenant-id";
constexpr absl::string_view kUpstreamClusterHeader = "x-envoy-reverse-tunnel-upstream-cluster-name";
constexpr absl::string_view kInitiationTimeHeader = "x-envoy-reverse-tunnel-initiation-time";
constexpr absl::string_view kWorkerIdHeader = "x-envoy-reverse-tunnel-worker-id";
constexpr absl::string_view kConnectionIdHeader = "x-envoy-reverse-tunnel-connection-id";

class MockInitiatorHandshakeCallbacks : public InitiatorHandshakeCallbacks {
public:
  MOCK_METHOD(Network::ClientConnection&, connection, (), (override));
  MOCK_METHOD(Upstream::HostDescriptionConstSharedPtr, host, (), (override));
  MOCK_METHOD(const std::string&, clusterName, (), (override));
  MOCK_METHOD(const std::string&, srcTenantId, (), (override));
  MOCK_METHOD(const std::string&, srcClusterId, (), (override));
  MOCK_METHOD(const std::string&, srcNodeId, (), (override));
  MOCK_METHOD(const std::string&, requestPath, (), (override));
  MOCK_METHOD(bool, useHttpUpgrade, (), (override));
  MOCK_METHOD((const std::vector<envoy::config::core::v3::HeaderValueOption>&), additionalHeaders,
              (), (override));
  MOCK_METHOD(HandshakeHeadersConstSharedPtr, effectiveHandshakeHeaders, (), (override));
  MOCK_METHOD(void, onHandshakeSucceeded, (), (override));
  MOCK_METHOD(void, onHandshakeDone,
              (HandshakeOutcome outcome, std::optional<std::chrono::milliseconds> retry_after),
              (override));

  int succeeded_count_{0};
};

// Builds one handshake header with a literal value.
HandshakeHeader
makeHandshakeHeader(absl::string_view key, absl::string_view value,
                    envoy::config::core::v3::HeaderValueOption::HeaderAppendAction action =
                        envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD) {
  return HandshakeHeader{Http::LowerCaseString(std::string(key)), action,
                         Formatter::FormatterImpl::create(std::string(value), true).value()};
}

envoy::config::core::v3::HeaderValueOption
makeAdditionalHeader(absl::string_view key, absl::string_view value,
                     envoy::config::core::v3::HeaderValueOption::HeaderAppendAction action =
                         envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD) {
  envoy::config::core::v3::HeaderValueOption option;
  option.mutable_header()->set_key(std::string(key));
  option.mutable_header()->set_value(std::string(value));
  option.set_append_action(action);
  return option;
}

} // namespace

class Http1HandshakeInitiatorTest : public Event::TestUsingSimulatedTime, public testing::Test {
protected:
  void SetUp() override {
    connection_ = std::make_unique<NiceMock<Network::MockClientConnection>>();
    EXPECT_CALL(*connection_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
    connection_->stream_info_.downstream_connection_info_provider_->setRemoteAddress(
        *Network::Utility::resolveUrl("tcp://10.0.0.5:9443"));
    // Capture everything the handshake encodes; drain so the codec sees the write as complete.
    ON_CALL(*connection_, write(_, _)).WillByDefault([this](Buffer::Instance& data, bool) -> void {
      absl::StrAppend(&written_, data.toString());
      data.drain(data.length());
    });

    cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
    host_ = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
    EXPECT_CALL(*host_, cluster()).WillRepeatedly(ReturnRef(*cluster_info_));

    ON_CALL(callbacks_, connection()).WillByDefault(ReturnRef(*connection_));
    ON_CALL(callbacks_, host()).WillByDefault(Return(host_));
    ON_CALL(callbacks_, clusterName()).WillByDefault(ReturnRef(cluster_name_));
    ON_CALL(callbacks_, srcTenantId()).WillByDefault(ReturnRef(tenant_id_));
    ON_CALL(callbacks_, srcClusterId()).WillByDefault(ReturnRef(cluster_id_));
    ON_CALL(callbacks_, srcNodeId()).WillByDefault(ReturnRef(node_id_));
    ON_CALL(callbacks_, requestPath()).WillByDefault(ReturnRef(path_));
    ON_CALL(callbacks_, useHttpUpgrade()).WillByDefault([this]() { return use_http_upgrade_; });
    ON_CALL(callbacks_, additionalHeaders()).WillByDefault(ReturnRef(empty_additional_headers_));
    ON_CALL(callbacks_, effectiveHandshakeHeaders()).WillByDefault([this]() {
      return effective_handshake_headers_;
    });
    ON_CALL(callbacks_, onHandshakeSucceeded()).WillByDefault([this]() {
      callbacks_.succeeded_count_++;
    });
    ON_CALL(callbacks_, onHandshakeDone(_, _))
        .WillByDefault(
            [this](HandshakeOutcome outcome, std::optional<std::chrono::milliseconds> retry_after) {
              outcomes_.push_back(std::move(outcome));
              retry_afters_.push_back(retry_after);
            });

    handshake_ = std::make_unique<Http1HandshakeInitiator>(callbacks_);
  }

  // Runs start() and returns the captured request bytes.
  const std::string& start(std::optional<int64_t> initiation_time_ms = std::nullopt) {
    EXPECT_TRUE(handshake_->start(initiation_time_ms).ok());
    return written_;
  }

  // Feeds raw response bytes into the handshake.
  void feed(absl::string_view bytes) {
    Buffer::OwnedImpl buffer(bytes);
    handshake_->onData(buffer);
  }

  // A minimal success response matching the configured mode.
  std::string successResponse(bool upgrade) const {
    return upgrade ? "HTTP/1.1 101 Switching Protocols\r\nconnection: upgrade\r\n"
                     "upgrade: reverse-tunnel\r\n\r\n"
                   : "HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n";
  }

  NiceMock<Event::MockDispatcher> dispatcher_{"worker_1"};
  std::unique_ptr<NiceMock<Network::MockClientConnection>> connection_;
  std::shared_ptr<NiceMock<Upstream::MockClusterInfo>> cluster_info_;
  std::shared_ptr<NiceMock<Upstream::MockHostDescription>> host_;
  NiceMock<MockInitiatorHandshakeCallbacks> callbacks_;

  std::string written_;
  std::string cluster_name_{"remote-cluster"};
  std::string tenant_id_{"test-tenant"};
  std::string cluster_id_{"test-cluster"};
  std::string node_id_{"test-node"};
  std::string path_{"/reverse-connections"};
  bool use_http_upgrade_{false};
  HandshakeHeadersConstSharedPtr effective_handshake_headers_;
  std::vector<envoy::config::core::v3::HeaderValueOption> empty_additional_headers_;

  std::vector<HandshakeOutcome> outcomes_;
  std::vector<std::optional<std::chrono::milliseconds>> retry_afters_;
  std::unique_ptr<Http1HandshakeInitiator> handshake_;
};

// The request carries the GET line, Host from the remote address, the identity headers, the
// per-connection correlation headers, and Content-Length: 0.
TEST_F(Http1HandshakeInitiatorTest, RequestCarriesIdentityAndCorrelationHeaders) {
  const std::string& request = start();

  EXPECT_THAT(request, testing::HasSubstr("GET /reverse-connections HTTP/1.1\r\n"));
  EXPECT_THAT(request, testing::HasSubstr("host: 10.0.0.5:9443\r\n"));
  EXPECT_THAT(request, testing::HasSubstr(absl::StrCat(kNodeHeader, ": test-node\r\n")));
  EXPECT_THAT(request, testing::HasSubstr(absl::StrCat(kClusterHeader, ": test-cluster\r\n")));
  EXPECT_THAT(request, testing::HasSubstr(absl::StrCat(kTenantHeader, ": test-tenant\r\n")));
  EXPECT_THAT(request,
              testing::HasSubstr(absl::StrCat(kUpstreamClusterHeader, ": remote-cluster\r\n")));
  EXPECT_THAT(request, testing::HasSubstr(absl::StrCat(kWorkerIdHeader, ": worker_1\r\n")));
  EXPECT_THAT(request, testing::HasSubstr(
                           absl::StrCat(kConnectionIdHeader, ": ", connection_->id(), "\r\n")));
  EXPECT_THAT(request, testing::HasSubstr("content-length: 0\r\n"));
  EXPECT_THAT(request, testing::Not(testing::HasSubstr("upgrade:")));
}

// The initiation-time header carries the explicit value passed to start().
TEST_F(Http1HandshakeInitiatorTest, ExplicitInitiationTime) {
  start(1234567890);
  EXPECT_THAT(written_,
              testing::HasSubstr(absl::StrCat(kInitiationTimeHeader, ": 1234567890\r\n")));
}

// Without an explicit value the initiation-time header carries the current simulated time.
TEST_F(Http1HandshakeInitiatorTest, DefaultInitiationTimeIsNow) {
  const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             simTime().systemTime().time_since_epoch())
                             .count();
  start();
  EXPECT_THAT(written_,
              testing::HasSubstr(absl::StrCat(kInitiationTimeHeader, ": ", now_ms, "\r\n")));
}

// Upgrade mode sends Connection: Upgrade + Upgrade: reverse-tunnel.
TEST_F(Http1HandshakeInitiatorTest, UpgradeNegotiation) {
  use_http_upgrade_ = true;
  const std::string& request = start();

  EXPECT_THAT(request, testing::HasSubstr("connection: upgrade\r\n"));
  EXPECT_THAT(request, testing::HasSubstr("upgrade: reverse-tunnel\r\n"));
}

// Headers configured via additional_headers are appended with their action.
TEST_F(Http1HandshakeInitiatorTest, AdditionalHeadersApplied) {
  auto headers = std::make_shared<const std::vector<envoy::config::core::v3::HeaderValueOption>>(
      std::vector<envoy::config::core::v3::HeaderValueOption>{
          makeAdditionalHeader("x-custom", "custom-value"),
      });
  ON_CALL(callbacks_, additionalHeaders()).WillByDefault(ReturnRef(*headers));

  const std::string& request = start();
  EXPECT_THAT(request, testing::HasSubstr("x-custom: custom-value\r\n"));
}

// ADD_IF_ABSENT does not clobber a header the handshake itself set.
TEST_F(Http1HandshakeInitiatorTest, AdditionalHeadersAddIfAbsentSkipsExisting) {
  auto headers = std::make_shared<const std::vector<envoy::config::core::v3::HeaderValueOption>>(
      std::vector<envoy::config::core::v3::HeaderValueOption>{
          makeAdditionalHeader(kTenantHeader, "other-tenant",
                               envoy::config::core::v3::HeaderValueOption::ADD_IF_ABSENT),
      });
  ON_CALL(callbacks_, additionalHeaders()).WillByDefault(ReturnRef(*headers));

  const std::string& request = start();
  EXPECT_THAT(request, testing::Not(testing::HasSubstr("other-tenant")));
  EXPECT_THAT(request, testing::HasSubstr(absl::StrCat(kTenantHeader, ": test-tenant\r\n")));
}

// A non-empty effectiveHandshakeHeaders() set replaces the additional_headers path, and is
// resolved exactly once per start().
TEST_F(Http1HandshakeInitiatorTest, EffectiveHandshakeHeadersWinAndResolveOnce) {
  std::vector<HandshakeHeader> effective_headers;
  effective_headers.push_back(makeHandshakeHeader("x-effective", "effective-value"));
  effective_handshake_headers_ =
      std::make_shared<const std::vector<HandshakeHeader>>(std::move(effective_headers));
  auto headers = std::make_shared<const std::vector<envoy::config::core::v3::HeaderValueOption>>(
      std::vector<envoy::config::core::v3::HeaderValueOption>{
          makeAdditionalHeader("x-custom", "custom-value"),
      });
  ON_CALL(callbacks_, additionalHeaders()).WillByDefault(ReturnRef(*headers));
  EXPECT_CALL(callbacks_, effectiveHandshakeHeaders());

  const std::string& request = start();
  EXPECT_THAT(request, testing::HasSubstr("x-effective: effective-value\r\n"));
  EXPECT_THAT(request, testing::Not(testing::HasSubstr("x-custom: custom-value")));
}

// A 200 response in non-upgrade mode reports success, with no residual.
TEST_F(Http1HandshakeInitiatorTest, Success200) {
  start();
  feed(successResponse(false));

  EXPECT_EQ(callbacks_.succeeded_count_, 1);
  ASSERT_EQ(outcomes_.size(), 1);
  EXPECT_TRUE(outcomes_[0].accepted);
  EXPECT_EQ(outcomes_[0].message, "reverse connection accepted");
  EXPECT_EQ(retry_afters_[0], std::nullopt);
  EXPECT_EQ(handshake_->takeResidual(), nullptr);
}

// A 101 response in upgrade mode reports success; post-101 bytes are carried as residual.
TEST_F(Http1HandshakeInitiatorTest, Success101CarriesResidual) {
  use_http_upgrade_ = true;
  start();
  feed(absl::StrCat(successResponse(true), "post-upgrade-payload"));

  EXPECT_EQ(callbacks_.succeeded_count_, 1);
  ASSERT_EQ(outcomes_.size(), 1);
  EXPECT_TRUE(outcomes_[0].accepted);
  Buffer::InstancePtr residual = handshake_->takeResidual();
  ASSERT_NE(residual, nullptr);
  EXPECT_EQ(residual->toString(), "post-upgrade-payload");
}

// Bytes coalesced with a 200 response in non-upgrade mode are captured as residual.
TEST_F(Http1HandshakeInitiatorTest, ResidualCoalescedWith200Response) {
  start();
  feed(absl::StrCat(successResponse(false), "residual-bytes"));

  ASSERT_EQ(outcomes_.size(), 1);
  EXPECT_TRUE(outcomes_[0].accepted);
  Buffer::InstancePtr residual = handshake_->takeResidual();
  ASSERT_NE(residual, nullptr);
  EXPECT_EQ(residual->toString(), "residual-bytes");
}

// A 429 with a delta-seconds Retry-After reports failure and forwards the cool-off.
TEST_F(Http1HandshakeInitiatorTest, Rejected429ForwardsRetryAfter) {
  start();
  feed("HTTP/1.1 429 Too Many Requests\r\ncontent-length: 0\r\nretry-after: 7\r\n\r\n");

  ASSERT_EQ(outcomes_.size(), 1);
  EXPECT_FALSE(outcomes_[0].accepted);
  ASSERT_TRUE(outcomes_[0].failure_reason.has_value());
  EXPECT_EQ(outcomes_[0].failure_reason->getNameForStats(), "http.429");
  EXPECT_EQ(outcomes_[0].message, "HTTP handshake failed with status 429");
  ASSERT_TRUE(retry_afters_[0].has_value());
  EXPECT_EQ(retry_afters_[0].value(), std::chrono::milliseconds(7000));
}

// A zero or HTTP-date Retry-After is treated as absent.
TEST_F(Http1HandshakeInitiatorTest, RetryAfterZeroAndDateFormsIgnored) {
  start();
  feed("HTTP/1.1 429 Too Many Requests\r\ncontent-length: 0\r\nretry-after: 0\r\n\r\n");
  ASSERT_EQ(outcomes_.size(), 1);
  EXPECT_EQ(retry_afters_[0], std::nullopt);

  // A second 429 on the same handshake is dropped: the first response already reported terminal.
  feed("HTTP/1.1 429 Too Many Requests\r\ncontent-length: 0\r\n"
       "retry-after: Wed, 21 Oct 2015 07:28:00 GMT\r\n\r\n");
  EXPECT_EQ(outcomes_.size(), 1);
}

// An unexpected status reports failure with the http.<status> stat name.
TEST_F(Http1HandshakeInitiatorTest, Rejected401) {
  start();
  feed("HTTP/1.1 401 Unauthorized\r\ncontent-length: 0\r\n\r\n");

  ASSERT_EQ(outcomes_.size(), 1);
  EXPECT_FALSE(outcomes_[0].accepted);
  ASSERT_TRUE(outcomes_[0].failure_reason.has_value());
  EXPECT_EQ(outcomes_[0].failure_reason->getNameForStats(), "http.401");
  EXPECT_EQ(outcomes_[0].message, "HTTP handshake failed with status 401");
  EXPECT_EQ(retry_afters_[0], std::nullopt);
}

// A malformed response fails the handshake as a connection close carrying the codec detail.
TEST_F(Http1HandshakeInitiatorTest, MalformedResponseFails) {
  start();
  feed("this is not http\r\n\r\n");

  ASSERT_EQ(outcomes_.size(), 1);
  EXPECT_FALSE(outcomes_[0].accepted);
  ASSERT_TRUE(outcomes_[0].failure_reason.has_value());
  EXPECT_EQ(outcomes_[0].failure_reason->getNameForStats(), "connection_close");
  EXPECT_THAT(outcomes_[0].failure_reason->getDetailedName(),
              testing::HasSubstr("HTTP handshake connection closed: "));
  EXPECT_EQ(retry_afters_[0], std::nullopt);
}

// Bytes arriving after the terminal outcome are dropped without further callbacks.
TEST_F(Http1HandshakeInitiatorTest, LateBytesAfterTerminalIgnored) {
  start();
  feed(successResponse(false));
  ASSERT_EQ(outcomes_.size(), 1);

  feed("HTTP/1.1 500 Internal Server Error\r\ncontent-length: 0\r\n\r\n");
  EXPECT_EQ(outcomes_.size(), 1);
  EXPECT_EQ(callbacks_.succeeded_count_, 1);
}

// cancel() detaches the read filter so late data through it cannot reach the handshake.
TEST_F(Http1HandshakeInitiatorTest, CancelDetachesReadFilter) {
  std::vector<Network::ReadFilterSharedPtr> added;
  std::vector<Network::ReadFilterSharedPtr> removed;
  ON_CALL(*connection_, addReadFilter(_))
      .WillByDefault([&added](Network::ReadFilterSharedPtr filter) { added.push_back(filter); });
  ON_CALL(*connection_, removeReadFilter(_))
      .WillByDefault(
          [&removed](Network::ReadFilterSharedPtr filter) { removed.push_back(filter); });

  start();
  ASSERT_EQ(added.size(), 1);
  EXPECT_TRUE(removed.empty());

  handshake_->cancel();
  ASSERT_EQ(removed.size(), 1);
  EXPECT_EQ(removed[0], added[0]);

  // Data through the detached filter reaches neither the codec nor the callbacks.
  Buffer::OwnedImpl buffer("HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n");
  EXPECT_EQ(added[0]->onData(buffer, false), Network::FilterStatus::StopIteration);
  EXPECT_TRUE(outcomes_.empty());
}

// Asserts abort at header insertion, so the codec encode failure is only observable here.
#if !defined(ENVOY_ENABLE_UHV) && defined(NDEBUG)
TEST_F(Http1HandshakeInitiatorTest, EncodeFailureReportsTerminal) {
  auto headers = std::make_shared<const std::vector<envoy::config::core::v3::HeaderValueOption>>(
      std::vector<envoy::config::core::v3::HeaderValueOption>{
          makeAdditionalHeader("x-bad", "bad\rvalue"),
      });
  ON_CALL(callbacks_, additionalHeaders()).WillByDefault(ReturnRef(*headers));

  const auto status = handshake_->start(std::nullopt);
  EXPECT_FALSE(status.ok());
  ASSERT_EQ(outcomes_.size(), 1);
  EXPECT_FALSE(outcomes_[0].accepted);
  ASSERT_TRUE(outcomes_[0].failure_reason.has_value());
  EXPECT_EQ(outcomes_[0].failure_reason->getNameForStats(), "encode_error");
  EXPECT_EQ(outcomes_[0].message, "HTTP handshake encode failed");
}
#endif

// In upgrade mode a 200 response is a protocol mismatch: the responder never switched protocols.
TEST_F(Http1HandshakeInitiatorTest, UpgradeMode200IsMismatch) {
  use_http_upgrade_ = true;
  start();
  feed(successResponse(false));

  ASSERT_EQ(outcomes_.size(), 1);
  EXPECT_FALSE(outcomes_[0].accepted);
  ASSERT_TRUE(outcomes_[0].failure_reason.has_value());
  EXPECT_EQ(outcomes_[0].failure_reason->getNameForStats(), "http.200");
  EXPECT_EQ(retry_afters_[0], std::nullopt);
}

// parseRetryAfter honors the RFC 7231 delta-seconds form, clamps it (overflow guard) at one hour,
// and returns nullopt for absent/zero/HTTP-date/malformed values so the caller falls back to its
// computed backoff.
TEST_F(Http1HandshakeInitiatorTest, ParseRetryAfterForms) {
  {
    Http::ResponseHeaderMapPtr headers = Http::ResponseHeaderMapImpl::create();
    headers->addCopy(Http::LowerCaseString("retry-after"), "7");
    auto result = Http1HandshakeInitiator::parseRetryAfter(*headers);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->count(), 7000);
  }
  {
    Http::ResponseHeaderMapPtr headers = Http::ResponseHeaderMapImpl::create();
    EXPECT_FALSE(Http1HandshakeInitiator::parseRetryAfter(*headers).has_value());
  }
  {
    // A zero cool-off must be treated as absent so it cannot short-circuit the backoff.
    Http::ResponseHeaderMapPtr headers = Http::ResponseHeaderMapImpl::create();
    headers->addCopy(Http::LowerCaseString("retry-after"), "0");
    EXPECT_FALSE(Http1HandshakeInitiator::parseRetryAfter(*headers).has_value());
  }
  {
    Http::ResponseHeaderMapPtr headers = Http::ResponseHeaderMapImpl::create();
    headers->addCopy(Http::LowerCaseString("retry-after"), "Wed, 21 Oct 2026 07:28:00 GMT");
    EXPECT_FALSE(Http1HandshakeInitiator::parseRetryAfter(*headers).has_value());
  }
  {
    Http::ResponseHeaderMapPtr headers = Http::ResponseHeaderMapImpl::create();
    headers->addCopy(Http::LowerCaseString("retry-after"), "100000"); // > 1h.
    auto result = Http1HandshakeInitiator::parseRetryAfter(*headers);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->count(), 3600 * 1000);
  }
}

// A read filter with no parent swallows data without touching the codec.
TEST_F(Http1HandshakeInitiatorTest, ReadFilterWithNullParentSwallowsData) {
  SimpleConnReadFilter filter(nullptr);
  Buffer::OwnedImpl buffer(successResponse(false));
  EXPECT_EQ(filter.onData(buffer, false), Network::FilterStatus::StopIteration);
  EXPECT_TRUE(outcomes_.empty());
}

// The factory produces HTTP/1 handshakes.
TEST_F(Http1HandshakeInitiatorTest, ConfiguratorCreatesHttp1Handshake) {
  Http1HandshakeConfigurator configurator;
  auto handshake = configurator.createHandshake(callbacks_);
  ASSERT_NE(handshake, nullptr);
  // A fresh handshake from the factory works end to end.
  Http1HandshakeInitiator* http1 = static_cast<Http1HandshakeInitiator*>(handshake.get());
  EXPECT_TRUE(http1->start(std::nullopt).ok());
  Buffer::OwnedImpl response(successResponse(false));
  http1->onData(response);
  ASSERT_EQ(outcomes_.size(), 1);
  EXPECT_TRUE(outcomes_[0].accepted);
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
