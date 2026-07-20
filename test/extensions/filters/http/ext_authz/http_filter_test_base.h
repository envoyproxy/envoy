#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/http/metadata_interface.h"
#include "envoy/http/query_params.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/tracing/trace_driver.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/headers.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/ext_authz/ext_authz.h"
#include "source/extensions/filters/http/ext_authz/ext_authz.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

using ::testing::InSequence;
using ::testing::Return;

constexpr char FilterConfigName[] = "ext_authz_filter";

// Builds a per-route config from proto, asserting construction succeeds. For tests that only use
// valid per-route configurations.
inline FilterConfigPerRoute
makePerRoute(const envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute& config) {
  absl::Status creation_status = absl::OkStatus();
  FilterConfigPerRoute per_route(config, creation_status);
  EXPECT_OK(creation_status);
  return per_route;
}

template <class T> class HttpFilterTestBase : public T {
public:
  HttpFilterTestBase() = default;

  void initialize(std::string&& yaml) {
    envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config{};
    if (!yaml.empty()) {
      TestUtility::loadFromYaml(yaml, proto_config);
    }
    initialize(proto_config);
  }

  void initialize(const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz& proto_config) {
    absl::Status creation_status = absl::OkStatus();
    config_ = std::make_shared<FilterConfig>(proto_config, *stats_store_.rootScope(),
                                             "ext_authz_prefix", factory_context_, creation_status);
    ASSERT_OK(creation_status);
    client_ = new NiceMock<Filters::Common::ExtAuthz::MockClient>();
    filter_ = std::make_unique<Filter>(config_, Filters::Common::ExtAuthz::ClientPtr{client_},
                                       factory_context_);
    ON_CALL(decoder_filter_callbacks_, filterConfigName()).WillByDefault(Return(FilterConfigName));
    filter_->setDecoderFilterCallbacks(decoder_filter_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_filter_callbacks_);
    addr_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 1111);
  }

  static envoy::extensions::filters::http::ext_authz::v3::ExtAuthz
  getFilterConfig(bool failure_mode_allow, bool http_client, bool emit_filter_state_stats = false,
                  std::optional<Envoy::Protobuf::Struct> filter_metadata = std::nullopt) {
    const std::string http_config = R"EOF(
    failure_mode_allow_header_add: true
    http_service:
      server_uri:
        uri: "ext_authz:9000"
        cluster: "ext_authz"
        timeout: 0.25s
    )EOF";

    const std::string grpc_config = R"EOF(
    failure_mode_allow_header_add: true
    grpc_service:
      envoy_grpc:
        cluster_name: "ext_authz_server"
    )EOF";

    envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config{};
    TestUtility::loadFromYaml(http_client ? http_config : grpc_config, proto_config);
    proto_config.set_failure_mode_allow(failure_mode_allow);
    if (emit_filter_state_stats) {
      proto_config.set_emit_filter_state_stats(true);
    }
    if (filter_metadata.has_value()) {
      *proto_config.mutable_filter_metadata() = *filter_metadata;
    }
    return proto_config;
  }

  void prepareCheck() {
    ON_CALL(decoder_filter_callbacks_, connection())
        .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
    connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
    connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  }

  void queryParameterTest(const std::string& original_path, const std::string& expected_path,
                          const Http::Utility::QueryParamsVector& add_me,
                          const std::vector<std::string>& remove_me) {
    InSequence s;

    // Set up all the typical headers plus a path with a query string that we'll remove later.
    request_headers_.addCopy(Http::Headers::get().Host, "example.com");
    request_headers_.addCopy(Http::Headers::get().Method, "GET");
    request_headers_.addCopy(Http::Headers::get().Path, original_path);
    request_headers_.addCopy(Http::Headers::get().Scheme, "https");

    prepareCheck();

    Filters::Common::ExtAuthz::Response response{};
    response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
    response.query_parameters_to_set = add_me;
    response.query_parameters_to_remove = remove_me;

    auto response_ptr = std::make_unique<Filters::Common::ExtAuthz::Response>(response);

    EXPECT_CALL(*client_, check(_, _, _, _))
        .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                             const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                             const StreamInfo::StreamInfo&) -> void {
          callbacks.onComplete(std::move(response_ptr));
        }));
    EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
    EXPECT_EQ(request_headers_.getPathValue(), expected_path);

    Buffer::OwnedImpl response_data{};
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    Http::TestResponseTrailerMapImpl response_trailers{};
    Http::MetadataMap response_metadata{};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_data, false));
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
    EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(response_metadata));
  }

  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  Stats::Scope& stats_scope_{*stats_store_.rootScope()};
  FilterConfigSharedPtr config_;
  Filters::Common::ExtAuthz::MockClient* client_;
  std::unique_ptr<Filter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_filter_callbacks_;
  Filters::Common::ExtAuthz::RequestCallbacks* request_callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestRequestTrailerMapImpl request_trailers_;
  Buffer::OwnedImpl data_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  NiceMock<Upstream::MockClusterManager> cm_;
  Network::Address::InstanceConstSharedPtr addr_;
  NiceMock<Envoy::Network::MockConnection> connection_;
};

class HttpFilterTest : public HttpFilterTestBase<testing::Test> {
public:
  HttpFilterTest() = default;

  void initializeMetadata(Filters::Common::ExtAuthz::Response& response) {
    auto* fields = response.dynamic_metadata.mutable_fields();
    (*fields)["foo"] = ValueUtil::stringValue("cool");
    (*fields)["bar"] = ValueUtil::numberValue(1);
  }
};

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
