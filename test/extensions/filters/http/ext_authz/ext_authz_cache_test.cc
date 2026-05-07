#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/http/codes.h"
#include "envoy/service/auth/v3/external_auth.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/empty_string.h"
#include "source/common/http/headers.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/ext_authz/ext_authz.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using Envoy::Http::LowerCaseString;
using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy::Extensions::HttpFilters::ExtAuthz {
namespace {

class ExtAuthzCacheTest : public testing::Test {
public:

  void initialize(const std::string& yaml) {
    envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config{};
    if (!yaml.empty()) {
      TestUtility::loadFromYaml(yaml, proto_config);
    }
    config_ = std::make_shared<FilterConfig>(proto_config, *stats_store_.rootScope(),
                                             "ext_authz_prefix", factory_context_);
    client_ = new NiceMock<Filters::Common::ExtAuthz::MockClient>();
    filter_ = std::make_unique<Filter>(config_, Filters::Common::ExtAuthz::ClientPtr{client_},
                                       factory_context_);
    ON_CALL(decoder_filter_callbacks_, filterConfigName()).WillByDefault(Return("ext_authz"));
    filter_->setDecoderFilterCallbacks(decoder_filter_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_filter_callbacks_);
    addr_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 1111);
  }

  void prepareCheck() {
    ON_CALL(decoder_filter_callbacks_, connection())
        .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
    connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
    connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  }

  void setCacheMetadata(const envoy::service::auth::v3::CheckResponse& response, const std::string& metadata_namespace) {
    Protobuf::Any typed_metadata;
    typed_metadata.PackFrom(response);
    
    // Set it in dynamic typed metadata
    decoder_callbacks_metadata_.mutable_typed_filter_metadata()->insert(
        {metadata_namespace, typed_metadata});
        
    ON_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata())
        .WillByDefault(ReturnRef(decoder_callbacks_metadata_));
  }

  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  FilterConfigSharedPtr config_;
  Filters::Common::ExtAuthz::MockClient* client_;
  std::unique_ptr<Filter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_filter_callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  Network::Address::InstanceConstSharedPtr addr_;
  NiceMock<Envoy::Network::MockConnection> connection_;
  envoy::config::core::v3::Metadata decoder_callbacks_metadata_;
};

TEST_F(ExtAuthzCacheTest, CacheHitOK) {
  initialize(R"(
    grpc_service:
      envoy_grpc:
        cluster_name: "ext_authz_server"
    check_response_typed_metadata_namespace: "envoy.filters.http.ext_authz.cache"
  )");

  prepareCheck();

  // Prepare cached response
  envoy::service::auth::v3::CheckResponse cached_response;
  cached_response.mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::Ok);
  auto* ok_response = cached_response.mutable_ok_response();
  auto* header = ok_response->add_headers();
  header->mutable_header()->set_key("x-cached-header");
  header->mutable_header()->set_value("yes");

  setCacheMetadata(cached_response, "envoy.filters.http.ext_authz.cache");

  // We expect client_->check to NOT be called
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);

  // Call decodeHeaders
  request_headers_.addCopy(Http::Headers::get().Host, "example.com");
  request_headers_.addCopy(Http::Headers::get().Method, "GET");
  request_headers_.addCopy(Http::Headers::get().Path, "/");
  
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  // Verify mutations are applied
  EXPECT_EQ("yes", request_headers_.get_("x-cached-header"));
  EXPECT_EQ(1U, config_->stats().ok_.value());
}

TEST_F(ExtAuthzCacheTest, CacheHitDenied) {
  initialize(R"(
    grpc_service:
      envoy_grpc:
        cluster_name: "ext_authz_server"
    check_response_typed_metadata_namespace: "envoy.filters.http.ext_authz.cache"
  )");

  prepareCheck();

  // Prepare cached response
  envoy::service::auth::v3::CheckResponse cached_response;
  cached_response.mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::PermissionDenied);
  auto* denied_response = cached_response.mutable_denied_response();
  denied_response->mutable_status()->set_code(static_cast<envoy::type::v3::StatusCode>(enumToInt(Http::Code::Forbidden)));
  denied_response->set_body("Access Denied by Cache");

  setCacheMetadata(cached_response, "envoy.filters.http.ext_authz.cache");

  // We expect client_->check to NOT be called
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);

  // Expect local reply
  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(Http::Code::Forbidden, "Access Denied by Cache", _, _, _));

  // Call decodeHeaders
  request_headers_.addCopy(Http::Headers::get().Host, "example.com");
  request_headers_.addCopy(Http::Headers::get().Method, "GET");
  request_headers_.addCopy(Http::Headers::get().Path, "/");

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(1U, config_->stats().denied_.value());
}

TEST_F(ExtAuthzCacheTest, CacheMissAndRecordgRPC) {
  initialize(R"(
    grpc_service:
      envoy_grpc:
        cluster_name: "ext_authz_server"
    check_response_typed_metadata_namespace: "envoy.filters.http.ext_authz.cache"
  )");

  prepareCheck();

  // We expect client_->check to be called
  Filters::Common::ExtAuthz::ResponsePtr authz_response = std::make_unique<Filters::Common::ExtAuthz::Response>();
  authz_response->status = Filters::Common::ExtAuthz::CheckStatus::OK;
  
  envoy::service::auth::v3::CheckResponse raw_response;
  raw_response.mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::Ok);
  auto* h = raw_response.mutable_ok_response()->add_headers();
  h->mutable_header()->set_key("x-live-header");
  h->mutable_header()->set_value("live");
  authz_response->raw_check_response = raw_response;

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::move(authz_response));
      }));

  // Expect dynamic typed metadata to be set with the cached response directly
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_, setDynamicTypedMetadata("envoy.filters.http.ext_authz.cache", _))
      .WillOnce(Invoke([&](const std::string&, const Protobuf::Any& metadata) {
        envoy::service::auth::v3::CheckResponse recorded;
        ASSERT_TRUE(MessageUtil::unpackTo(metadata, recorded).ok());
        EXPECT_EQ(Grpc::Status::WellKnownGrpcStatus::Ok, recorded.status().code());
        EXPECT_EQ("x-live-header", recorded.ok_response().headers(0).header().key());
        EXPECT_EQ("live", recorded.ok_response().headers(0).header().value());
      }));

  // Call decodeHeaders
  request_headers_.addCopy(Http::Headers::get().Host, "example.com");
  request_headers_.addCopy(Http::Headers::get().Method, "GET");
  request_headers_.addCopy(Http::Headers::get().Path, "/");

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

TEST_F(ExtAuthzCacheTest, InvalidCacheMetadataFallback) {
  initialize(R"(
    grpc_service:
      envoy_grpc:
        cluster_name: "ext_authz_server"
    check_response_typed_metadata_namespace: "envoy.filters.http.ext_authz.cache"
  )");

  prepareCheck();

  // Set unexpected proto type in dynamic typed metadata to trigger unpack failure
  envoy::config::core::v3::Metadata unexpected_proto;
  (*unexpected_proto.mutable_filter_metadata())["unexpected"] = {};
  
  Protobuf::Any typed_metadata_any;
  typed_metadata_any.PackFrom(unexpected_proto);

  decoder_callbacks_metadata_.mutable_typed_filter_metadata()->insert(
      {"envoy.filters.http.ext_authz.cache", typed_metadata_any});
  ON_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata())
      .WillByDefault(ReturnRef(decoder_callbacks_metadata_));

  // We expect fallback to live call, so client_->check IS called
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        Filters::Common::ExtAuthz::ResponsePtr fallback_response = std::make_unique<Filters::Common::ExtAuthz::Response>();
        fallback_response->status = Filters::Common::ExtAuthz::CheckStatus::OK;
        callbacks.onComplete(std::move(fallback_response));
      }));

  // Call decodeHeaders
  request_headers_.addCopy(Http::Headers::get().Host, "example.com");
  request_headers_.addCopy(Http::Headers::get().Method, "GET");
  request_headers_.addCopy(Http::Headers::get().Path, "/");

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  
  // Verify stats
  EXPECT_EQ(1U, config_->stats().invalid_cached_response_.value());
}

TEST_F(ExtAuthzCacheTest, CacheHitErrorFailClosed) {
  initialize(R"(
    grpc_service:
      envoy_grpc:
        cluster_name: "ext_authz_server"
    check_response_typed_metadata_namespace: "envoy.filters.http.ext_authz.cache"
  )");

  prepareCheck();

  // Prepare cached response representing an error (500)
  envoy::service::auth::v3::CheckResponse cached_response;
  cached_response.mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::Internal);
  auto* error_response = cached_response.mutable_error_response();
  error_response->mutable_status()->set_code(static_cast<envoy::type::v3::StatusCode>(enumToInt(Http::Code::InternalServerError)));
  error_response->set_body("Cached Error Body");

  setCacheMetadata(cached_response, "envoy.filters.http.ext_authz.cache");

  // We expect client_->check to NOT be called
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);

  // Expect local reply (fail-closed) with custom status from error_response
  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(Http::Code::InternalServerError, "Cached Error Body", _, _, _));

  // Call decodeHeaders
  request_headers_.addCopy(Http::Headers::get().Host, "example.com");
  request_headers_.addCopy(Http::Headers::get().Method, "GET");
  request_headers_.addCopy(Http::Headers::get().Path, "/");

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(1U, config_->stats().error_.value());
}

TEST_F(ExtAuthzCacheTest, CacheHitErrorFailOpen) {
  initialize(R"(
    grpc_service:
      envoy_grpc:
        cluster_name: "ext_authz_server"
    check_response_typed_metadata_namespace: "envoy.filters.http.ext_authz.cache"
    failure_mode_allow: true
    failure_mode_allow_header_add: true
  )");

  prepareCheck();

  // Prepare cached response representing an error (500)
  envoy::service::auth::v3::CheckResponse cached_response;
  cached_response.mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::Internal);
  auto* error_response = cached_response.mutable_error_response();
  error_response->mutable_status()->set_code(static_cast<envoy::type::v3::StatusCode>(enumToInt(Http::Code::InternalServerError)));

  setCacheMetadata(cached_response, "envoy.filters.http.ext_authz.cache");

  // We expect client_->check to NOT be called
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);

  // Call decodeHeaders
  request_headers_.addCopy(Http::Headers::get().Host, "example.com");
  request_headers_.addCopy(Http::Headers::get().Method, "GET");
  request_headers_.addCopy(Http::Headers::get().Path, "/");

  // Should continue decoding (fail-open)
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  
  // Verify fail-open header is added
  EXPECT_EQ("true", request_headers_.get_("x-envoy-auth-failure-mode-allowed"));
  EXPECT_EQ(1U, config_->stats().error_.value());
  EXPECT_EQ(1U, config_->stats().failure_mode_allowed_.value());
}

} // namespace
} // namespace Envoy::Extensions::HttpFilters::ExtAuthz
