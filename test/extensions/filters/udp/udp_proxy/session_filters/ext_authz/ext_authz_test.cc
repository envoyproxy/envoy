#include "envoy/common/exception.h"
#include "envoy/extensions/filters/udp/udp_proxy/session/ext_authz/v3/ext_authz.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/filters/udp/udp_proxy/session_filters/ext_authz/ext_authz.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/extensions/filters/udp/udp_proxy/mocks.h"
#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace ExtAuthz {
namespace {

using CheckStatus = Filters::Common::ExtAuthz::CheckStatus;

class ExtAuthzFilterTest : public testing::Test {
public:
  // Buffering is opt-in; enable_buffering uses the default limits.
  void setup(bool failure_mode_allow = false, bool enable_buffering = false) {
    FilterConfig proto_config;
    proto_config.set_stat_prefix("test");
    proto_config.set_failure_mode_allow(failure_mode_allow);
    if (enable_buffering) {
      proto_config.mutable_buffer_options();
    }
    build(proto_config);
  }

  void setupWithBufferLimits(uint32_t max_datagrams, uint64_t max_bytes) {
    FilterConfig proto_config;
    proto_config.set_stat_prefix("test");
    auto* buffer_options = proto_config.mutable_buffer_options();
    buffer_options->mutable_max_buffered_datagrams()->set_value(max_datagrams);
    buffer_options->mutable_max_buffered_bytes()->set_value(max_bytes);
    build(proto_config);
  }

  void build(const FilterConfig& proto_config) {
    config_ =
        std::make_shared<Config>(proto_config, context_.scope(), context_.server_factory_context_);

    auto client = std::make_unique<NiceMock<Filters::Common::ExtAuthz::MockClient>>();
    client_ = client.get();
    filter_ = std::make_unique<Filter>(config_, std::move(client));
    filter_->initializeReadFilterCallbacks(callbacks_);
    ON_CALL(callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info_));

    // Capture the callbacks (to drive onComplete() asynchronously) and the emitted CheckRequest.
    ON_CALL(*client_, check(_, _, _, _))
        .WillByDefault(Invoke([this](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                                     const envoy::service::auth::v3::CheckRequest& check_request,
                                     Tracing::Span&, const StreamInfo::StreamInfo&) {
          request_callbacks_ = &callbacks;
          check_request_ = check_request;
        }));
  }

  Network::UdpRecvData makeDatagram(const std::string& payload) {
    Network::UdpRecvData data;
    data.buffer_ = std::make_unique<Buffer::OwnedImpl>(payload);
    return data;
  }

  Filters::Common::ExtAuthz::ResponsePtr makeResponse(CheckStatus status) {
    auto response = std::make_unique<Filters::Common::ExtAuthz::Response>();
    response->status = status;
    return response;
  }

  Filters::Common::ExtAuthz::ResponsePtr makeResponse(CheckStatus status,
                                                      const Protobuf::Struct& dynamic_metadata) {
    auto response = makeResponse(status);
    response->dynamic_metadata = dynamic_metadata;
    return response;
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  ConfigSharedPtr config_;
  NiceMock<Filters::Common::ExtAuthz::MockClient>* client_{};
  std::unique_ptr<Filter> filter_;
  NiceMock<MockReadFilterCallbacks> callbacks_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  Filters::Common::ExtAuthz::RequestCallbacks* request_callbacks_{};
  envoy::service::auth::v3::CheckRequest check_request_;
};

// A new session triggers a single check call and pauses iteration until the response arrives.
TEST_F(ExtAuthzFilterTest, AllowedResumesChainAndReplaysBufferedDatagrams) {
  setup(/*failure_mode_allow=*/false, /*enable_buffering=*/true);

  EXPECT_CALL(*client_, check(_, _, _, _));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());

  auto d1 = makeDatagram("one");
  auto d2 = makeDatagram("two");
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(d1));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(d2));

  EXPECT_CALL(callbacks_, continueFilterChain()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, injectDatagramToFilterChain(_)).Times(2);
  request_callbacks_->onComplete(makeResponse(CheckStatus::OK));

  EXPECT_EQ(1U, config_->stats().ok_.value());
  EXPECT_EQ(1U, config_->stats().total_.value());
  EXPECT_EQ(0U, config_->stats().active_.value());

  auto d3 = makeDatagram("three");
  EXPECT_EQ(ReadFilterStatus::Continue, filter_->onData(d3));
}

// A denied session keeps iteration stopped, drops buffered datagrams, and never resumes.
TEST_F(ExtAuthzFilterTest, DeniedDropsSession) {
  setup(/*failure_mode_allow=*/false, /*enable_buffering=*/true);

  EXPECT_CALL(*client_, check(_, _, _, _));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());

  auto d1 = makeDatagram("one");
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(d1));

  EXPECT_CALL(callbacks_, continueFilterChain()).Times(0);
  EXPECT_CALL(callbacks_, injectDatagramToFilterChain(_)).Times(0);
  request_callbacks_->onComplete(makeResponse(CheckStatus::Denied));

  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(0U, config_->stats().active_.value());

  auto d2 = makeDatagram("two");
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(d2));
}

// Dynamic metadata returned by the authz service is published to the session on allow.
TEST_F(ExtAuthzFilterTest, AllowedPublishesDynamicMetadata) {
  setup();

  EXPECT_CALL(*client_, check(_, _, _, _));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());

  Protobuf::Struct metadata;
  (*metadata.mutable_fields())["foo"].set_string_value("bar");

  EXPECT_CALL(stream_info_, setDynamicMetadata(_, _))
      .WillOnce(Invoke([](const std::string& ns, const Protobuf::Struct& value) {
        EXPECT_EQ("envoy.filters.udp.session.ext_authz", ns);
        EXPECT_EQ("bar", value.fields().at("foo").string_value());
      }));
  EXPECT_CALL(callbacks_, continueFilterChain()).WillOnce(Return(true));
  request_callbacks_->onComplete(makeResponse(CheckStatus::OK, metadata));
}

// Dynamic metadata is published even when the session is denied (set before the deny return).
TEST_F(ExtAuthzFilterTest, DeniedPublishesDynamicMetadata) {
  setup();

  EXPECT_CALL(*client_, check(_, _, _, _));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());

  Protobuf::Struct metadata;
  (*metadata.mutable_fields())["foo"].set_string_value("bar");

  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.filters.udp.session.ext_authz", _));
  EXPECT_CALL(callbacks_, continueFilterChain()).Times(0);
  request_callbacks_->onComplete(makeResponse(CheckStatus::Denied, metadata));
}

// On an authz service error, the session is dropped unless failure_mode_allow is set.
TEST_F(ExtAuthzFilterTest, ErrorDropsSessionByDefault) {
  setup(/*failure_mode_allow=*/false);

  EXPECT_CALL(*client_, check(_, _, _, _));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());

  EXPECT_CALL(callbacks_, continueFilterChain()).Times(0);
  request_callbacks_->onComplete(makeResponse(CheckStatus::Error));

  EXPECT_EQ(1U, config_->stats().error_.value());
  EXPECT_EQ(0U, config_->stats().failure_mode_allowed_.value());
}

// With failure_mode_allow set, an authz service error admits the session.
TEST_F(ExtAuthzFilterTest, ErrorAllowedWithFailureModeAllow) {
  setup(/*failure_mode_allow=*/true);

  EXPECT_CALL(*client_, check(_, _, _, _));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());

  EXPECT_CALL(callbacks_, continueFilterChain()).WillOnce(Return(true));
  request_callbacks_->onComplete(makeResponse(CheckStatus::Error));

  EXPECT_EQ(1U, config_->stats().error_.value());
  EXPECT_EQ(1U, config_->stats().failure_mode_allowed_.value());
}

// A synchronous (inline) OK completion lets onNewSession() continue without re-entering the chain.
TEST_F(ExtAuthzFilterTest, SynchronousAllowedContinuesInline) {
  setup();

  ON_CALL(*client_, check(_, _, _, _))
      .WillByDefault(Invoke([](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                               const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                               const StreamInfo::StreamInfo&) {
        auto response = std::make_unique<Filters::Common::ExtAuthz::Response>();
        response->status = CheckStatus::OK;
        callbacks.onComplete(std::move(response));
      }));

  EXPECT_CALL(callbacks_, continueFilterChain()).Times(0);
  EXPECT_EQ(ReadFilterStatus::Continue, filter_->onNewSession());
  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// An in-flight check is cancelled when the filter is destroyed.
TEST_F(ExtAuthzFilterTest, CancelInflightCheckOnDestruction) {
  setup();

  EXPECT_CALL(*client_, check(_, _, _, _));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());
  EXPECT_EQ(1U, config_->stats().active_.value());

  EXPECT_CALL(*client_, cancel());
  filter_.reset();

  // The active gauge must be released even though onComplete never ran.
  EXPECT_EQ(0U, config_->stats().active_.value());
}

// If the session was removed during resume, the filter must not inject buffered datagrams.
TEST_F(ExtAuthzFilterTest, ContinueFilterChainFailureDoesNotInject) {
  setup(/*failure_mode_allow=*/false, /*enable_buffering=*/true);

  EXPECT_CALL(*client_, check(_, _, _, _));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());

  auto d1 = makeDatagram("one");
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(d1));

  EXPECT_CALL(callbacks_, continueFilterChain()).WillOnce(Return(false));
  EXPECT_CALL(callbacks_, injectDatagramToFilterChain(_)).Times(0);
  request_callbacks_->onComplete(makeResponse(CheckStatus::OK));
}

// Without buffer_options, datagrams arriving during the check are dropped, not replayed.
TEST_F(ExtAuthzFilterTest, BufferingDisabledByDefault) {
  setup();
  EXPECT_FALSE(config_->bufferEnabled());

  EXPECT_CALL(*client_, check(_, _, _, _));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());

  auto d1 = makeDatagram("one");
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(d1));

  EXPECT_CALL(callbacks_, continueFilterChain()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, injectDatagramToFilterChain(_)).Times(0);
  request_callbacks_->onComplete(makeResponse(CheckStatus::OK));
}

// An empty buffer_options enables buffering with the default limits.
TEST_F(ExtAuthzFilterTest, DefaultBufferLimits) {
  setup(/*failure_mode_allow=*/false, /*enable_buffering=*/true);
  EXPECT_TRUE(config_->bufferEnabled());
  EXPECT_EQ(1024, config_->maxBufferedDatagrams());
  EXPECT_EQ(16384, config_->maxBufferedBytes());
}

// Datagrams beyond the configured buffer limits are dropped and counted.
TEST_F(ExtAuthzFilterTest, BufferOverflowIsCountedAndDropped) {
  setupWithBufferLimits(/*max_datagrams=*/1, /*max_bytes=*/1024);
  EXPECT_EQ(1, config_->maxBufferedDatagrams());

  EXPECT_CALL(*client_, check(_, _, _, _));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());

  auto d1 = makeDatagram("one");
  auto d2 = makeDatagram("two");
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(d1));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(d2));
  EXPECT_EQ(1U, config_->stats().buffer_overflow_.value());

  EXPECT_CALL(callbacks_, continueFilterChain()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, injectDatagramToFilterChain(_));
  request_callbacks_->onComplete(makeResponse(CheckStatus::OK));
}

// The CheckRequest carries the session's downstream source and destination addresses.
TEST_F(ExtAuthzFilterTest, CheckRequestCarriesSessionAddresses) {
  setup();
  stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:5678"));
  stream_info_.downstream_connection_info_provider_->setLocalAddress(
      Network::Utility::parseInternetAddressAndPortNoThrow("5.6.7.8:9000"));

  EXPECT_CALL(*client_, check(_, _, _, _));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());

  const auto& attributes = check_request_.attributes();
  EXPECT_EQ("1.2.3.4", attributes.source().address().socket_address().address());
  EXPECT_EQ(5678, attributes.source().address().socket_address().port_value());
  EXPECT_EQ("5.6.7.8", attributes.destination().address().socket_address().address());
  EXPECT_EQ(9000, attributes.destination().address().socket_address().port_value());
}

// The byte-based buffer limit drops datagrams that would exceed it, independent of the count limit.
TEST_F(ExtAuthzFilterTest, BufferByteOverflowIsCountedAndDropped) {
  setupWithBufferLimits(/*max_datagrams=*/100, /*max_bytes=*/4);

  EXPECT_CALL(*client_, check(_, _, _, _));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());

  auto d1 = makeDatagram("ab"); // 2 bytes: fits (2 <= 4).
  auto d2 =
      makeDatagram("cde"); // 3 bytes: 2 + 3 > 4, dropped though the count limit is not reached.
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(d1));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(d2));
  EXPECT_EQ(1U, config_->stats().buffer_overflow_.value());

  // Only the datagram under the byte limit is replayed on allow.
  EXPECT_CALL(callbacks_, continueFilterChain()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, injectDatagramToFilterChain(_));
  request_callbacks_->onComplete(makeResponse(CheckStatus::OK));
}

// A failure to create the gRPC client factory is surfaced as a configuration exception.
TEST_F(ExtAuthzFilterTest, ConfigThrowsWhenGrpcClientFactoryFails) {
  EXPECT_CALL(context_.server_factory_context_.cluster_manager_.async_client_manager_,
              factoryForGrpcService(_, _, _))
      .WillOnce(Return(absl::InvalidArgumentError("bad grpc service")));

  FilterConfig proto_config;
  proto_config.set_stat_prefix("test");
  EXPECT_THROW(
      std::make_shared<Config>(proto_config, context_.scope(), context_.server_factory_context_),
      EnvoyException);
}

} // namespace
} // namespace ExtAuthz
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
