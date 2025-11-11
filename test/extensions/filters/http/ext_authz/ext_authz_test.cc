#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.validate.h"
#include "envoy/http/codes.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/empty_string.h"
#include "source/common/http/context_impl.h"
#include "source/common/http/headers.h"
#include "source/common/json/json_loader.h"
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
#include "test/proto/helloworld.pb.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using Envoy::Http::LowerCaseString;
using testing::_;
using testing::Contains;
using testing::InSequence;
using testing::Invoke;
using testing::Key;
using testing::NiceMock;
using testing::Not;
using testing::Return;
using testing::ReturnRef;
using testing::Values;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {
namespace {

// Matcher to convert a Buffer::Instance to its string representation for composition.
MATCHER_P(BufferString, m, "") {
  return testing::ExplainMatchResult(m, arg->toString(), result_listener);
}

// Matcher to parse a buffer string into a CheckRequest proto.
MATCHER_P(AsCheckRequest, m, "") {
  envoy::service::auth::v3::CheckRequest check_request;
  if (!check_request.ParseFromString(arg)) {
    *result_listener << "failed to parse CheckRequest from buffer";
    return false;
  }
  return testing::ExplainMatchResult(m, check_request, result_listener);
}

// Matcher to verify CheckRequest has specific context extension.
MATCHER_P2(HasContextExtension, key, value, "") {
  const auto& context_extensions = arg.attributes().context_extensions();
  if (context_extensions.find(key) == context_extensions.end()) {
    *result_listener << "context extension '" << key << "' not found";
    return false;
  }
  if (context_extensions.at(key) != value) {
    *result_listener << "context extension '" << key << "' has value '"
                     << context_extensions.at(key) << "', expected '" << value << "'";
    return false;
  }
  return true;
}

// Matcher to verify RequestOptions has specific timeout value.
MATCHER_P(HasTimeout, expected_timeout_ms, "") {
  if (!arg.timeout.has_value()) {
    *result_listener << "timeout not set";
    return false;
  }
  if (arg.timeout->count() != expected_timeout_ms) {
    *result_listener << "timeout is " << arg.timeout->count() << "ms, expected "
                     << expected_timeout_ms << "ms";
    return false;
  }
  return true;
}

constexpr char FilterConfigName[] = "ext_authz_filter";

template <class T> class HttpFilterTestBase : public T {
public:
  HttpFilterTestBase() {}

  void initialize(std::string&& yaml) {
    envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config{};
    if (!yaml.empty()) {
      TestUtility::loadFromYaml(yaml, proto_config);
    }
    initialize(proto_config);
  }

  void initialize(const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz& proto_config) {
    config_ = std::make_shared<FilterConfig>(proto_config, *stats_store_.rootScope(),
                                             "ext_authz_prefix", factory_context_);
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
                  absl::optional<Envoy::Protobuf::Struct> filter_metadata = absl::nullopt) {
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

using CreateFilterConfigFunc = envoy::extensions::filters::http::ext_authz::v3::ExtAuthz();

class HttpFilterTestParam
    : public HttpFilterTestBase<
          testing::TestWithParam<std::tuple<bool /*failure_mode_allow*/, bool /*http_client*/>>> {
public:
  void SetUp() override {
    initialize(getFilterConfig(std::get<0>(GetParam()), std::get<1>(GetParam())));
  }

  static std::string ParamsToString(const testing::TestParamInfo<std::tuple<bool, bool>>& info) {
    return absl::StrCat(std::get<0>(info.param) ? "FailOpen" : "FailClosed", "_",
                        std::get<1>(info.param) ? "HttpClient" : "GrpcClient");
  }
};

INSTANTIATE_TEST_SUITE_P(ParameterizedFilterConfig, HttpFilterTestParam,
                         testing::Combine(testing::Bool(), testing::Bool()),
                         HttpFilterTestParam::ParamsToString);

class ExtAuthzLoggingInfoTest
    : public testing::TestWithParam<
          std::tuple<std::string /* field_name */, absl::optional<uint64_t> /* value */>> {
public:
  ExtAuthzLoggingInfoTest() : logging_info_({}) {}

  void SetUp() override {
    std::string fieldName = std::get<0>(GetParam());
    absl::optional<uint64_t> optional = std::get<1>(GetParam());
    if (optional.has_value()) {
      if (fieldName == "latency_us") {
        logging_info_.setLatency(std::chrono::microseconds(optional.value()));
      }
      if (fieldName == "bytesSent") {
        logging_info_.setBytesSent(optional.value());
      }
      if (fieldName == "bytesReceived") {
        logging_info_.setBytesReceived(optional.value());
      }
    }
  }

  void test() {
    ASSERT_TRUE(logging_info_.hasFieldSupport());
    absl::optional<uint64_t> optional = std::get<1>(GetParam());
    if (optional.has_value()) {
      EXPECT_THAT(logging_info_.getField(std::get<0>(GetParam())),
                  testing::VariantWith<int64_t>(optional.value()));
    } else {
      EXPECT_THAT(logging_info_.getField(std::get<0>(GetParam())),
                  testing::VariantWith<absl::monostate>(absl::monostate{}));
    }
  }

  static std::string ParamsToString(
      const testing::TestParamInfo<std::tuple<std::string, absl::optional<uint64_t>>>& info) {
    return absl::StrCat(std::get<1>(info.param).has_value() ? "" : "no_", std::get<0>(info.param),
                        std::get<1>(info.param).has_value()
                            ? absl::StrCat("_", std::to_string(std::get<1>(info.param).value()))
                            : "");
  }

  ExtAuthzLoggingInfo logging_info_;
};

INSTANTIATE_TEST_SUITE_P(
    ExtAuthzLoggingInfoTestValid, ExtAuthzLoggingInfoTest,
    testing::Combine(testing::Values("latency_us", "bytesSent", "bytesReceived"),
                     testing::Values(absl::optional<uint64_t>{}, absl::optional<uint64_t>{0},
                                     absl::optional<uint64_t>{1})),
    ExtAuthzLoggingInfoTest::ParamsToString);

INSTANTIATE_TEST_SUITE_P(ExtAuthzLoggingInfoTestInvalid, ExtAuthzLoggingInfoTest,
                         testing::Values(std::make_tuple("wrong_property_name",
                                                         absl::optional<uint64_t>{})),
                         ExtAuthzLoggingInfoTest::ParamsToString);

class EmitFilterStateTest
    : public HttpFilterTestBase<testing::TestWithParam<
          std::tuple<bool /*http_client*/, bool /*emit_stats*/, bool /*emit_filter_metadata*/>>> {
public:
  EmitFilterStateTest() : expected_output_(filterMetadata()) {}

  absl::optional<Envoy::Protobuf::Struct> filterMetadata() const {
    if (!std::get<2>(GetParam())) {
      return absl::nullopt;
    }

    auto filter_metadata = Envoy::Protobuf::Struct();
    *(*filter_metadata.mutable_fields())["foo"].mutable_string_value() = "bar";
    return filter_metadata;
  }

  void SetUp() override {
    initialize(getFilterConfig(/*failure_mode_allow=*/false, std::get<0>(GetParam()),
                               std::get<1>(GetParam()), filterMetadata()));

    stream_info_ = std::make_unique<NiceMock<StreamInfo::MockStreamInfo>>();

    auto bytes_meter = std::make_shared<StreamInfo::BytesMeter>();
    bytes_meter->addWireBytesSent(123);
    bytes_meter->addWireBytesReceived(456);
    stream_info_->upstream_bytes_meter_ = bytes_meter;

    auto upstream_info = std::make_shared<NiceMock<StreamInfo::MockUpstreamInfo>>();
    auto upstream_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
    upstream_info->upstream_host_ = upstream_host;
    stream_info_->upstream_info_ = upstream_info;

    auto upstream_cluster_info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
    stream_info_->upstream_cluster_info_ = upstream_cluster_info;

    if (std::get<1>(GetParam())) {
      expected_output_.setLatency(std::chrono::milliseconds(1));
      expected_output_.setUpstreamHost(upstream_host);
      expected_output_.setClusterInfo(upstream_cluster_info);
      expected_output_.setBytesSent(123);
      expected_output_.setBytesReceived(456);
    }
  }

  static std::string
  ParamsToString(const testing::TestParamInfo<std::tuple<bool, bool, bool>>& info) {
    return absl::StrCat(std::get<0>(info.param) ? "HttpClient" : "GrpcClient", "_",
                        std::get<1>(info.param) ? "EmitStats" : "DoNotEmitStats", "_",
                        std::get<2>(info.param) ? "EmitFilterMetadata" : "DoNotEmitFilterMetadata");
  }

  // Convenience function to save rewriting the same boilerplate & checks for all these tests.
  void test(const Filters::Common::ExtAuthz::Response& response) {
    InSequence s;

    prepareCheck();

    auto& filter_state = decoder_filter_callbacks_.streamInfo().filterState();
    absl::optional<ExtAuthzLoggingInfo> preexisting_data_copy =
        filter_state->hasData<ExtAuthzLoggingInfo>(FilterConfigName)
            ? absl::make_optional(
                  *filter_state->getDataReadOnly<ExtAuthzLoggingInfo>(FilterConfigName))
            : absl::nullopt;

    // ext_authz makes a single call to the external auth service once it sees the end of stream.
    EXPECT_CALL(*client_, check(_, _, _, _))
        .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                             const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                             const StreamInfo::StreamInfo&) -> void {
          decoder_filter_callbacks_.dispatcher_.globalTimeSystem().advanceTimeWait(
              std::chrono::milliseconds(1));
          request_callbacks_ = &callbacks;
        }));

    EXPECT_CALL(*client_, streamInfo()).WillRepeatedly(Return(stream_info_.get()));
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter_->decodeHeaders(request_headers_, true));

    request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

    // Will exist if stats or filter metadata is emitted or if there was preexisting logging info.
    bool expect_logging_info =
        (std::get<1>(GetParam()) || std::get<2>(GetParam()) || preexisting_data_copy);

    ASSERT_EQ(filter_state->hasData<ExtAuthzLoggingInfo>(FilterConfigName), expect_logging_info);

    if (!expect_logging_info) {
      return;
    }

    auto actual = filter_state->getDataReadOnly<ExtAuthzLoggingInfo>(FilterConfigName);
    ASSERT_NE(actual, nullptr);
    if (preexisting_data_copy) {
      expectEq(*actual, *preexisting_data_copy);
      EXPECT_EQ((std::get<1>(GetParam()) || std::get<2>(GetParam())) ? 1U : 0U,
                config_->stats().filter_state_name_collision_.value());
      return;
    }

    expectEq(*actual, expected_output_);
  }

  static void expectEq(const ExtAuthzLoggingInfo& actual, const ExtAuthzLoggingInfo& expected) {
    EXPECT_EQ(actual.latency(), expected.latency());
    EXPECT_EQ(actual.upstreamHost(), expected.upstreamHost());
    EXPECT_EQ(actual.clusterInfo(), expected.clusterInfo());
    EXPECT_EQ(actual.bytesSent(), expected.bytesSent());
    EXPECT_EQ(actual.bytesReceived(), expected.bytesReceived());
    EXPECT_EQ(actual.grpcStatus().has_value(), expected.grpcStatus().has_value());
    if (expected.grpcStatus().has_value()) {
      EXPECT_EQ(actual.grpcStatus().value(), expected.grpcStatus().value());
    }

    ASSERT_EQ(actual.filterMetadata().has_value(), expected.filterMetadata().has_value());
    if (expected.filterMetadata().has_value()) {
      EXPECT_EQ(actual.filterMetadata()->DebugString(), expected.filterMetadata()->DebugString());
    }
  }

  std::unique_ptr<NiceMock<StreamInfo::MockStreamInfo>> stream_info_;
  ExtAuthzLoggingInfo expected_output_;
};

INSTANTIATE_TEST_SUITE_P(EmitFilterStateTestParams, EmitFilterStateTest,
                         testing::Combine(testing::Bool(), testing::Bool(), testing::Bool()),
                         EmitFilterStateTest::ParamsToString);

class InvalidMutationTest : public HttpFilterTestBase<testing::Test> {
public:
  InvalidMutationTest() : invalid_value_(reinterpret_cast<const char*>(invalid_value_bytes_)) {}

  void testResponse(const Filters::Common::ExtAuthz::Response& response) {
    InSequence s;

    initialize(R"(
        grpc_service:
          envoy_grpc:
            cluster_name: "ext_authz_server"
        validate_mutations: true
    )");

    // Simulate a downstream request.
    ON_CALL(decoder_filter_callbacks_, connection())
        .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
    connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
    connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
    EXPECT_CALL(*client_, check(_, _, _, _))
        .WillOnce(Invoke(
            [&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

    // (The path pseudo header needs to be set for the query parameter tests.)
    request_headers_.addCopy(Http::Headers::get().Path, "/some-endpoint");
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter_->decodeHeaders(request_headers_, false));

    EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
    EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, true))
        .WillOnce(Invoke([&](const Http::ResponseHeaderMap& headers, bool) -> void {
          EXPECT_EQ(headers.getStatusValue(),
                    std::to_string(enumToInt(Http::Code::InternalServerError)));
        }));

    // Send this test's response. It should get invalidated.
    request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

    EXPECT_EQ(decoder_filter_callbacks_.details(), "ext_authz_invalid");
    EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                      ->statsScope()
                      .counterFromString("ext_authz.invalid")
                      .value());
    EXPECT_EQ(1U, config_->stats().invalid_.value());
  }

  static constexpr const char* invalid_key_ = "invalid-\nkey";
  static constexpr uint8_t invalid_value_bytes_[3]{0x7f, 0x7f, 0};
  const std::string invalid_value_;

  static std::string getInvalidValue() {
    return std::string(reinterpret_cast<const char*>(invalid_value_bytes_));
  }
};

TEST_F(HttpFilterTest, DisableDynamicMetadataIngestion) {
  InSequence s;

  initialize(R"(
      grpc_service:
        envoy_grpc:
          cluster_name: "ext_authz_server"
      enable_dynamic_metadata_ingestion:
        value: false
  )");

  // Simulate a downstream request.
  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());

  // Send response. Dynamic metadata should be ignored.
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_, setDynamicMetadata(_, _)).Times(0);

  Filters::Common::ExtAuthz::Response response;
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  (*response.dynamic_metadata.mutable_fields())["key"] = ValueUtil::stringValue("value");
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  EXPECT_EQ(1U, config_->stats().ignored_dynamic_metadata_.value());
}

// Tests that the filter rejects authz responses with mutations with an invalid key when
// validate_authz_response is set to true in config.
// Parameterized test for invalid mutation scenarios to reduce redundancy.
class InvalidMutationParamTest
    : public InvalidMutationTest,
      public testing::WithParamInterface<
          std::tuple<std::string,                                               // test name
                     std::function<void(Filters::Common::ExtAuthz::Response&)>, // setup func
                     Filters::Common::ExtAuthz::CheckStatus                     // status
                     >> {};

TEST_P(InvalidMutationParamTest, InvalidMutationFields) {
  const auto& [test_name, setup_func, status] = GetParam();

  Filters::Common::ExtAuthz::Response response;
  response.status = status;
  setup_func(response);
  testResponse(response);
}

INSTANTIATE_TEST_SUITE_P(
    InvalidMutationScenarios, InvalidMutationParamTest,
    testing::Values(
        // Invalid key tests
        std::make_tuple(
            "HeadersToSetKey",
            [](Filters::Common::ExtAuthz::Response& r) {
              r.headers_to_set = {{InvalidMutationTest::invalid_key_, "bar"}};
            },
            Filters::Common::ExtAuthz::CheckStatus::OK),
        std::make_tuple(
            "HeadersToAddKey",
            [](Filters::Common::ExtAuthz::Response& r) {
              r.headers_to_add = {{InvalidMutationTest::invalid_key_, "bar"}};
            },
            Filters::Common::ExtAuthz::CheckStatus::OK),
        std::make_tuple(
            "HeadersToSetKeyDenied",
            [](Filters::Common::ExtAuthz::Response& r) {
              r.headers_to_set = {{InvalidMutationTest::invalid_key_, "bar"}};
            },
            Filters::Common::ExtAuthz::CheckStatus::Denied),
        std::make_tuple(
            "HeadersToAppendKey",
            [](Filters::Common::ExtAuthz::Response& r) {
              r.headers_to_append = {{InvalidMutationTest::invalid_key_, "bar"}};
            },
            Filters::Common::ExtAuthz::CheckStatus::OK),
        std::make_tuple(
            "ResponseHeadersToAddKey",
            [](Filters::Common::ExtAuthz::Response& r) {
              r.response_headers_to_set = {{InvalidMutationTest::invalid_key_, "bar"}};
            },
            Filters::Common::ExtAuthz::CheckStatus::OK),
        std::make_tuple(
            "ResponseHeadersToSetKey",
            [](Filters::Common::ExtAuthz::Response& r) {
              r.response_headers_to_set = {{InvalidMutationTest::invalid_key_, "bar"}};
            },
            Filters::Common::ExtAuthz::CheckStatus::OK),
        std::make_tuple(
            "ResponseHeadersToAddIfAbsentKey",
            [](Filters::Common::ExtAuthz::Response& r) {
              r.response_headers_to_add_if_absent = {{InvalidMutationTest::invalid_key_, "bar"}};
            },
            Filters::Common::ExtAuthz::CheckStatus::OK),
        std::make_tuple(
            "ResponseHeadersToOverwriteIfExistsKey",
            [](Filters::Common::ExtAuthz::Response& r) {
              r.response_headers_to_overwrite_if_exists = {
                  {InvalidMutationTest::invalid_key_, "bar"}};
            },
            Filters::Common::ExtAuthz::CheckStatus::OK),
        std::make_tuple(
            "QueryParametersToSetKey",
            [](Filters::Common::ExtAuthz::Response& r) {
              r.query_parameters_to_set = {{"f o o", "bar"}};
            },
            Filters::Common::ExtAuthz::CheckStatus::OK),
        // Invalid value tests
        std::make_tuple(
            "HeadersToSetValue",
            [](Filters::Common::ExtAuthz::Response& r) {
              r.headers_to_set = {{"foo", InvalidMutationTest::getInvalidValue()}};
            },
            Filters::Common::ExtAuthz::CheckStatus::OK),
        std::make_tuple(
            "HeadersToAddValue",
            [](Filters::Common::ExtAuthz::Response& r) {
              r.headers_to_add = {{"foo", InvalidMutationTest::getInvalidValue()}};
            },
            Filters::Common::ExtAuthz::CheckStatus::OK),
        std::make_tuple(
            "HeadersToSetValueDenied",
            [](Filters::Common::ExtAuthz::Response& r) {
              r.headers_to_set = {{"foo", InvalidMutationTest::getInvalidValue()}};
            },
            Filters::Common::ExtAuthz::CheckStatus::Denied),
        std::make_tuple(
            "HeadersToAppendValue",
            [](Filters::Common::ExtAuthz::Response& r) {
              r.headers_to_append = {{"foo", InvalidMutationTest::getInvalidValue()}};
            },
            Filters::Common::ExtAuthz::CheckStatus::OK),
        std::make_tuple(
            "ResponseHeadersToAddValue",
            [](Filters::Common::ExtAuthz::Response& r) {
              r.response_headers_to_set = {{"foo", InvalidMutationTest::getInvalidValue()}};
            },
            Filters::Common::ExtAuthz::CheckStatus::OK),
        std::make_tuple(
            "ResponseHeadersToSetValue",
            [](Filters::Common::ExtAuthz::Response& r) {
              r.response_headers_to_set = {{"foo", InvalidMutationTest::getInvalidValue()}};
            },
            Filters::Common::ExtAuthz::CheckStatus::OK),
        std::make_tuple(
            "ResponseHeadersToAddIfAbsentValue",
            [](Filters::Common::ExtAuthz::Response& r) {
              r.response_headers_to_add_if_absent = {
                  {"foo", InvalidMutationTest::getInvalidValue()}};
            },
            Filters::Common::ExtAuthz::CheckStatus::OK),
        std::make_tuple(
            "ResponseHeadersToOverwriteIfExistsValue",
            [](Filters::Common::ExtAuthz::Response& r) {
              r.response_headers_to_overwrite_if_exists = {
                  {"foo", InvalidMutationTest::getInvalidValue()}};
            },
            Filters::Common::ExtAuthz::CheckStatus::OK),
        std::make_tuple(
            "QueryParametersToSetValue",
            [](Filters::Common::ExtAuthz::Response& r) {
              r.query_parameters_to_set = {{"foo", "b a r"}};
            },
            Filters::Common::ExtAuthz::CheckStatus::OK)),
    [](const testing::TestParamInfo<InvalidMutationParamTest::ParamType>& info) {
      return std::get<0>(info.param);
    });

// Keep one simple focused test to ensure backward compatibility.
TEST_F(InvalidMutationTest, BasicInvalidKey) {
  Filters::Common::ExtAuthz::Response response;
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_set = {{invalid_key_, "bar"}};
  testResponse(response);
}

TEST_F(InvalidMutationTest, InvalidHeaderAppendAction) {
  Filters::Common::ExtAuthz::Response response;
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.saw_invalid_append_actions = true;
  testResponse(response);
}

struct DecoderHeaderMutationRulesTestOpts {
  absl::optional<envoy::config::common::mutation_rules::v3::HeaderMutationRules> rules;
  bool expect_reject_response = false;
  Filters::Common::ExtAuthz::UnsafeHeaderVector allowed_headers_to_add;
  Filters::Common::ExtAuthz::UnsafeHeaderVector disallowed_headers_to_add;
  Filters::Common::ExtAuthz::UnsafeHeaderVector allowed_headers_to_append;
  Filters::Common::ExtAuthz::UnsafeHeaderVector disallowed_headers_to_append;
  Filters::Common::ExtAuthz::UnsafeHeaderVector allowed_headers_to_set;
  Filters::Common::ExtAuthz::UnsafeHeaderVector disallowed_headers_to_set;
  std::vector<absl::string_view> allowed_headers_to_remove;
  std::vector<absl::string_view> disallowed_headers_to_remove;
};
class DecoderHeaderMutationRulesTest
    : public HttpFilterTestBase<testing::TestWithParam<bool /*disallow_is_error*/>> {
public:
  void runTest(DecoderHeaderMutationRulesTestOpts opts) {
    InSequence s;

    envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config{};
    TestUtility::loadFromYaml(R"(
        transport_api_version: V3
        grpc_service:
          envoy_grpc:
            cluster_name: "ext_authz_server"
        failure_mode_allow: false
    )",
                              proto_config);
    if (opts.rules != std::nullopt) {
      *(proto_config.mutable_decoder_header_mutation_rules()) = *opts.rules;
    }

    initialize(proto_config);

    // Simulate a downstream request.
    populateRequestHeadersFromOpts(opts);

    ON_CALL(decoder_filter_callbacks_, connection())
        .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
    connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
    connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
    EXPECT_CALL(*client_, check(_, _, _, _))
        .WillOnce(Invoke(
            [&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter_->decodeHeaders(request_headers_, false));
    if (opts.expect_reject_response) {
      EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, true))
          .WillOnce(Invoke([&](const Http::ResponseHeaderMap& headers, bool) -> void {
            EXPECT_EQ(headers.getStatusValue(),
                      std::to_string(enumToInt(Http::Code::InternalServerError)));
          }));
    } else {
      EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
    }

    // Construct authz response from opts.
    Filters::Common::ExtAuthz::Response response = getResponseFromOpts(opts);

    request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

    if (!opts.expect_reject_response) {
      // Now make sure the downstream header is / is not there depending on the test.
      checkRequestHeadersFromOpts(opts);
    }
  }

  void populateRequestHeadersFromOpts(const DecoderHeaderMutationRulesTestOpts& opts) {
    for (const auto& [key, _] : opts.allowed_headers_to_set) {
      request_headers_.addCopy(Http::LowerCaseString(key), "will be overridden");
    }
    for (const auto& [key, _] : opts.disallowed_headers_to_set) {
      request_headers_.addCopy(Http::LowerCaseString(key), "will not be overridden");
    }

    for (const auto& [key, _] : opts.allowed_headers_to_append) {
      request_headers_.addCopy(Http::LowerCaseString(key), "will be appended to");
    }
    for (const auto& [key, _] : opts.disallowed_headers_to_append) {
      request_headers_.addCopy(Http::LowerCaseString(key), "will not be appended to");
    }

    for (const auto& key : opts.allowed_headers_to_remove) {
      request_headers_.addCopy(Http::LowerCaseString(key), "will be removed");
    }
    for (const auto& key : opts.disallowed_headers_to_remove) {
      request_headers_.addCopy(Http::LowerCaseString(key), "will not be removed");
    }
  }

  void checkRequestHeadersFromOpts(const DecoderHeaderMutationRulesTestOpts& opts) {
    for (const auto& [key, value] : opts.allowed_headers_to_add) {
      EXPECT_EQ(request_headers_.get_(Http::LowerCaseString(key)), value)
          << "(key: '" << key << "')";
    }
    for (const auto& [key, _] : opts.disallowed_headers_to_add) {
      EXPECT_FALSE(request_headers_.has(Http::LowerCaseString(key))) << "(key: '" << key << "')";
    }

    for (const auto& [key, value] : opts.allowed_headers_to_set) {
      EXPECT_EQ(request_headers_.get_(Http::LowerCaseString(key)), value)
          << "(key: '" << key << "')";
    }
    for (const auto& [key, _] : opts.disallowed_headers_to_set) {
      EXPECT_EQ(request_headers_.get_(Http::LowerCaseString(key)), "will not be overridden")
          << "(key: '" << key << "')";
    }

    for (const auto& [key, value] : opts.allowed_headers_to_append) {
      EXPECT_EQ(request_headers_.get_(Http::LowerCaseString(key)),
                absl::StrCat("will be appended to,", value))
          << "(key: '" << key << "')";
    }
    for (const auto& [key, value] : opts.disallowed_headers_to_append) {
      EXPECT_EQ(request_headers_.get_(Http::LowerCaseString(key)), "will not be appended to")
          << "(key: '" << key << "')";
    }

    for (const auto& key : opts.allowed_headers_to_remove) {
      EXPECT_FALSE(request_headers_.has(Http::LowerCaseString(key))) << "(key: '" << key << "')";
    }
    for (const auto& key : opts.disallowed_headers_to_remove) {
      EXPECT_EQ(request_headers_.get_(Http::LowerCaseString(key)), "will not be removed")
          << "(key: '" << key << "')";
    }
  }

  static Filters::Common::ExtAuthz::Response
  getResponseFromOpts(const DecoderHeaderMutationRulesTestOpts& opts) {
    Filters::Common::ExtAuthz::Response response;
    response.status = Filters::Common::ExtAuthz::CheckStatus::OK;

    for (const auto& vec : {opts.allowed_headers_to_add, opts.disallowed_headers_to_add}) {
      for (const auto& [key, value] : vec) {
        response.headers_to_add.emplace_back(key, value);
      }
    }

    for (const auto& vec : {opts.allowed_headers_to_set, opts.disallowed_headers_to_set}) {
      for (const auto& [key, value] : vec) {
        response.headers_to_set.emplace_back(key, value);
      }
    }

    for (const auto& vec : {opts.allowed_headers_to_append, opts.disallowed_headers_to_append}) {
      for (const auto& [key, value] : vec) {
        response.headers_to_append.emplace_back(key, value);
      }
    }

    for (const auto& vec : {opts.allowed_headers_to_remove, opts.disallowed_headers_to_remove}) {
      for (const auto& key : vec) {
        response.headers_to_remove.emplace_back(key);
      }
    }

    return response;
  }
};

// If decoder_header_mutation_rules is empty, there should be no additional restrictions to
// ext_authz header mutations.
TEST_F(DecoderHeaderMutationRulesTest, EmptyConfig) {
  DecoderHeaderMutationRulesTestOpts opts;
  opts.allowed_headers_to_add = {{":authority", "google"}};
  opts.allowed_headers_to_append = {{"normal", "one"}, {":fake-pseudo-header", "append me"}};
  opts.allowed_headers_to_set = {{"x-envoy-whatever", "override"}};
  opts.allowed_headers_to_remove = {"delete-me"};
  // These are not allowed not because of the header mutation rules config, but because ext_authz
  // doesn't allow the removable of headers starting with `:` anyway.
  opts.disallowed_headers_to_remove = {":method", ":fake-pseudoheader"};
  runTest(opts);
}

// Test behavior when the rules field exists but all sub-fields are their default values (should be
// exactly the same as above)
TEST_F(DecoderHeaderMutationRulesTest, ExplicitDefaultConfig) {
  DecoderHeaderMutationRulesTestOpts opts;
  opts.rules = envoy::config::common::mutation_rules::v3::HeaderMutationRules();
  opts.disallowed_headers_to_add = {{":authority", "google"}};
  opts.allowed_headers_to_append = {{"normal", "one"}};
  opts.disallowed_headers_to_append = {{":fake-pseudo-header", "append me"}};
  opts.disallowed_headers_to_set = {{"x-envoy-whatever", "override"}};
  opts.allowed_headers_to_remove = {"delete-me"};
  // These are not allowed not because of the header mutation rules config, but because ext_authz
  // doesn't allow the removable of headers starting with `:` anyway.
  opts.disallowed_headers_to_remove = {":method", ":fake-pseudoheader"};
  runTest(opts);
}

TEST_F(DecoderHeaderMutationRulesTest, DisallowAll) {
  DecoderHeaderMutationRulesTestOpts opts;
  opts.rules = envoy::config::common::mutation_rules::v3::HeaderMutationRules();
  opts.rules->mutable_disallow_all()->set_value(true);

  opts.disallowed_headers_to_add = {{"cant-add-me", "sad"}};
  opts.disallowed_headers_to_append = {{"cant-append-to-me", "fail"}};
  opts.disallowed_headers_to_set = {{"cant-override-me", "nope"}};
  opts.disallowed_headers_to_remove = {"cant-delete-me"};
  runTest(opts);
}

// Consolidated rejection test that covers all the scenarios previously tested individually.
TEST_F(DecoderHeaderMutationRulesTest, RejectResponseOperations) {
  // Test data structure for all rejection scenarios
  struct TestCase {
    std::string name;
    bool use_disallow_all;
    std::function<void(DecoderHeaderMutationRulesTestOpts&)> setup_func;
  };

  std::vector<TestCase> test_cases = {
      {"RejectResponseAdd", true,
       [](DecoderHeaderMutationRulesTestOpts& opts) {
         opts.disallowed_headers_to_add = {{"cant-add-me", "sad"}};
       }},
      {"RejectResponseAppend", true,
       [](DecoderHeaderMutationRulesTestOpts& opts) {
         opts.disallowed_headers_to_append = {{"cant-append-to-me", "fail"}};
       }},
      {"RejectResponseAppendPseudoheader", false,
       [](DecoderHeaderMutationRulesTestOpts& opts) {
         opts.disallowed_headers_to_append = {{":fake-pseudo-header", "fail"}};
       }},
      {"RejectResponseSet", true,
       [](DecoderHeaderMutationRulesTestOpts& opts) {
         opts.disallowed_headers_to_set = {{"cant-override-me", "nope"}};
       }},
      {"RejectResponseRemove", true,
       [](DecoderHeaderMutationRulesTestOpts& opts) {
         opts.disallowed_headers_to_remove = {"cant-delete-me"};
       }},
      {"RejectResponseRemovePseudoHeader", false, [](DecoderHeaderMutationRulesTestOpts& opts) {
         opts.disallowed_headers_to_remove = {":fake-pseudo-header"};
       }}};

  // Run all test cases
  for (const auto& test_case : test_cases) {
    SCOPED_TRACE(test_case.name);

    DecoderHeaderMutationRulesTestOpts opts;
    opts.rules = envoy::config::common::mutation_rules::v3::HeaderMutationRules();
    if (test_case.use_disallow_all) {
      opts.rules->mutable_disallow_all()->set_value(true);
    }
    opts.rules->mutable_disallow_is_error()->set_value(true);
    opts.expect_reject_response = true;

    test_case.setup_func(opts);
    runTest(opts);
  }
}

TEST_F(DecoderHeaderMutationRulesTest, DisallowExpression) {
  DecoderHeaderMutationRulesTestOpts opts;
  opts.rules = envoy::config::common::mutation_rules::v3::HeaderMutationRules();
  opts.rules->mutable_disallow_expression()->set_regex("^x-example-.*");

  opts.allowed_headers_to_add = {{"add-me-one", "one"}, {"add-me-two", "two"}};
  opts.disallowed_headers_to_add = {{"x-example-add", "nope"}};
  opts.allowed_headers_to_append = {{"append-to-me", "appended value"}};
  opts.disallowed_headers_to_append = {{"x-example-append", "no sir"}};
  opts.allowed_headers_to_set = {{"override-me", "new value"}};
  opts.disallowed_headers_to_set = {{"x-example-set", "no can do"}};
  opts.allowed_headers_to_remove = {"delete-me"};
  opts.disallowed_headers_to_remove = {"x-example-remove"};
  runTest(opts);
}

// Tests that allow_expression overrides other settings (except disallow, which is tested
// elsewhere).
TEST_F(DecoderHeaderMutationRulesTest, AllowExpression) {
  DecoderHeaderMutationRulesTestOpts opts;
  opts.rules = envoy::config::common::mutation_rules::v3::HeaderMutationRules();
  opts.rules->mutable_disallow_all()->set_value(true);
  opts.rules->mutable_allow_expression()->set_regex("^x-allow-.*");

  opts.allowed_headers_to_add = {{"x-allow-add-me-one", "one"}, {"x-allow-add-me-two", "two"}};
  opts.disallowed_headers_to_add = {{"not-allowed", "nope"}};
  opts.allowed_headers_to_append = {{"x-allow-append-to-me", "appended value"}};
  opts.disallowed_headers_to_append = {{"xx-allow-wrong-prefix", "no sir"}};
  opts.allowed_headers_to_set = {{"x-allow-override-me", "new value"}};
  opts.disallowed_headers_to_set = {{"cant-set-me", "no can do"}};
  opts.allowed_headers_to_remove = {"x-allow-delete-me"};
  opts.disallowed_headers_to_remove = {"cannot-remove"};
  runTest(opts);
}

// Tests that disallow_expression overrides allow_expression.
TEST_F(DecoderHeaderMutationRulesTest, OverlappingAllowAndDisallowExpressions) {
  DecoderHeaderMutationRulesTestOpts opts;
  opts.rules = envoy::config::common::mutation_rules::v3::HeaderMutationRules();
  opts.rules->mutable_disallow_all()->set_value(true);
  // Note the disallow expression's matches are a subset of the allow expression's matches.
  opts.rules->mutable_allow_expression()->set_regex(".*allowed.*");
  opts.rules->mutable_disallow_expression()->set_regex(".*disallowed.*");

  opts.allowed_headers_to_add = {{"allowed-add", "yes"}};
  opts.disallowed_headers_to_add = {{"disallowed-add", "nope"}};
  opts.allowed_headers_to_append = {{"allowed-append", "appended value"}};
  opts.disallowed_headers_to_append = {{"disallowed-append", "no sir"}};
  opts.allowed_headers_to_set = {{"allowed-set", "new value"}};
  opts.disallowed_headers_to_set = {{"disallowed-set", "no can do"}};
  opts.allowed_headers_to_remove = {"allowed-remove"};
  opts.disallowed_headers_to_remove = {"disallowed-remove"};
  runTest(opts);
}

// Test that the per route config is properly merged: more specific keys override previous keys.
TEST_F(HttpFilterTest, MergeConfig) {
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settings;
  auto&& extensions = settings.mutable_check_settings()->mutable_context_extensions();

  // First config base config with one base value, and one value to be overridden.
  (*extensions)["base_key"] = "base_value";
  (*extensions)["merged_key"] = "base_value";
  FilterConfigPerRoute base_config(settings);

  // Construct a config to merge, that provides one value and overrides one value.
  settings.Clear();
  auto&& specific_extensions = settings.mutable_check_settings()->mutable_context_extensions();
  (*specific_extensions)["merged_key"] = "value";
  (*specific_extensions)["key"] = "value";
  FilterConfigPerRoute specific_config(settings);

  // Perform the merge:
  base_config.merge(specific_config);

  settings.Clear();
  settings.set_disabled(true);
  FilterConfigPerRoute disabled_config(settings);

  // Perform a merge with disabled config:
  base_config.merge(disabled_config);

  // Make sure all values were merged:
  auto&& merged_extensions = base_config.contextExtensions();
  EXPECT_EQ("base_value", merged_extensions.at("base_key"));
  EXPECT_EQ("value", merged_extensions.at("merged_key"));
  EXPECT_EQ("value", merged_extensions.at("key"));
}

// Test that defining stat_prefix appends an additional prefix to the emitted statistics names.
TEST_F(HttpFilterTest, StatsWithPrefix) {
  const std::string stat_prefix = "with_stat_prefix";
  const std::string error_counter_name_with_prefix =
      absl::StrCat("ext_authz.", stat_prefix, ".error");
  const std::string error_counter_name_without_prefix = "ext_authz.error";

  InSequence s;

  initialize(fmt::format(R"EOF(
  stat_prefix: "{}"
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  )EOF",
                         stat_prefix));

  EXPECT_EQ(0U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString(error_counter_name_with_prefix)
                    .value());
  EXPECT_EQ(0U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString(error_counter_name_without_prefix)
                    .value());

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString(error_counter_name_with_prefix)
                    .value());
  // The one without an additional prefix is not incremented, since it is not "defined".
  EXPECT_EQ(0U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString(error_counter_name_without_prefix)
                    .value());
}

// Test when failure_mode_allow is set to false and the response from the authorization service is
// Error that the request is not allowed to continue.
TEST_F(HttpFilterTest, ErrorFailClose) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](const Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.getStatusValue(), std::to_string(enumToInt(Http::Code::Forbidden)));
      }));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.error")
                    .value());
  EXPECT_EQ(1U, config_->stats().error_.value());
}

// Verifies that the filter responds with a configurable HTTP status when an network error occurs.
TEST_F(HttpFilterTest, ErrorCustomStatusCode) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  status_on_error:
    code: 503
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](const Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.getStatusValue(),
                  std::to_string(enumToInt(Http::Code::ServiceUnavailable)));
      }));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.error")
                    .value());
  EXPECT_EQ(1U, config_->stats().error_.value());
  EXPECT_EQ("ext_authz_error", decoder_filter_callbacks_.details());
}

// Test when failure_mode_allow is set and the response from the authorization service is Error that
// the request is allowed to continue.
TEST_F(HttpFilterTest, ErrorFailOpen) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: true
  failure_mode_allow_header_add: true
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.error")
                    .value());
  EXPECT_EQ(1U, config_->stats().error_.value());
  EXPECT_EQ(request_headers_.get_("x-envoy-auth-failure-mode-allowed"), "true");
}

// Test when failure_mode_allow is set and the response from the authorization service is an
// immediate Error that the request is allowed to continue.
TEST_F(HttpFilterTest, ImmediateErrorOpen) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: true
  failure_mode_allow_header_add: true
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.error")
                    .value());
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.failure_mode_allowed")
                    .value());
  EXPECT_EQ(1U, config_->stats().error_.value());
  EXPECT_EQ(1U, config_->stats().failure_mode_allowed_.value());
  EXPECT_EQ(request_headers_.get_("x-envoy-auth-failure-mode-allowed"), "true");
}

// Test when failure_mode_allow is set with header add closed and the response from the
// authorization service is Error that the request is allowed to continue.
TEST_F(HttpFilterTest, ErrorOpenWithHeaderAddClose) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: true
  failure_mode_allow_header_add: false
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.error")
                    .value());
  EXPECT_EQ(1U, config_->stats().error_.value());
  EXPECT_EQ(request_headers_.get_("x-envoy-auth-failure-mode-allowed"), EMPTY_STRING);
}

// Test when failure_mode_allow is set with header add closed and the response from the
// authorization service is an immediate Error that the request is allowed to continue.
TEST_F(HttpFilterTest, ImmediateErrorOpenWithHeaderAddClose) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: true
  failure_mode_allow_header_add: false
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.error")
                    .value());
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.failure_mode_allowed")
                    .value());
  EXPECT_EQ(1U, config_->stats().error_.value());
  EXPECT_EQ(1U, config_->stats().failure_mode_allowed_.value());
  EXPECT_EQ(request_headers_.get_("x-envoy-auth-failure-mode-allowed"), EMPTY_STRING);
}

// Check a bad configuration results in validation exception.
TEST_F(HttpFilterTest, BadConfig) {
  const std::string filter_config = R"EOF(
  grpc_service:
    envoy_grpc: {}
  failure_mode_allow: true
  )EOF";
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config{};
  TestUtility::loadFromYaml(filter_config, proto_config);
  EXPECT_THROW(TestUtility::downcastAndValidate<
                   const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz&>(proto_config),
               ProtoValidationException);
}

// Checks that filter does not initiate the authorization request when the buffer reaches the max
// request bytes.
TEST_F(HttpFilterTest, RequestDataIsTooLarge) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  with_request_body:
    max_request_bytes: 10
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  EXPECT_CALL(decoder_filter_callbacks_, setDecoderBufferLimit(_));
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl buffer1("foo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer1, false));

  Buffer::OwnedImpl buffer2("foobarbaz");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer2, false));
}

// Checks that the filter initiates an authorization request when the buffer reaches max
// request bytes and allow_partial_message is set to true.
TEST_F(HttpFilterTest, RequestDataWithPartialMessage) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  with_request_body:
    max_request_bytes: 10
    allow_partial_message: true
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  ON_CALL(decoder_filter_callbacks_, decodingBuffer()).WillByDefault(Return(&data_));
  ON_CALL(decoder_filter_callbacks_, addDecodedData(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& data, bool) { data_.add(data); }));
  EXPECT_CALL(decoder_filter_callbacks_, setDecoderBufferLimit(_)).Times(0);
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl buffer1("foo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer1, false));
  data_.add(buffer1.toString());

  Buffer::OwnedImpl buffer2("bar");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer2, false));
  data_.add(buffer2.toString());

  Buffer::OwnedImpl buffer3("barfoo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer3, false));
  data_.add(buffer3.toString());

  Buffer::OwnedImpl buffer4("more data after watermark is set is possible");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer4, true));

  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
}

// Checks that the filter initiates an authorization request when the buffer reaches maximum
// request bytes and allow_partial_message is set to true. In addition to that, after the filter
// sends the check request, data decoding continues.
TEST_F(HttpFilterTest, RequestDataWithPartialMessageThenContinueDecoding) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  with_request_body:
    max_request_bytes: 10
    allow_partial_message: true
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  ON_CALL(decoder_filter_callbacks_, decodingBuffer()).WillByDefault(Return(&data_));
  ON_CALL(decoder_filter_callbacks_, addDecodedData(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& data, bool) { data_.add(data); }));
  EXPECT_CALL(decoder_filter_callbacks_, setDecoderBufferLimit(_)).Times(0);
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);

  // The check call should only be called once.
  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl buffer1("foo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer1, false));
  data_.add(buffer1.toString());

  Buffer::OwnedImpl buffer2("bar");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer2, false));
  data_.add(buffer2.toString());

  Buffer::OwnedImpl buffer3("barfoo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer3, false));
  // data added by the previous decodeData call.

  Buffer::OwnedImpl buffer4("more data after watermark is set is possible");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer4, false));
  data_.add(buffer4.toString());

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  Buffer::OwnedImpl buffer5("more data after calling check request");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer5, true));

  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
}

// Checks that the filter initiates the authorization process only when the filter decode trailers
// is called.
TEST_F(HttpFilterTest, RequestDataWithSmallBuffer) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  with_request_body:
    max_request_bytes: 10
    allow_partial_message: true
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  ON_CALL(decoder_filter_callbacks_, decodingBuffer()).WillByDefault(Return(&data_));
  ON_CALL(decoder_filter_callbacks_, addDecodedData(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& data, bool) { data_.add(data); }));
  EXPECT_CALL(decoder_filter_callbacks_, setDecoderBufferLimit(_)).Times(0);
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl buffer("foo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer, false));
  data_.add(buffer.toString());
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
}

// Checks that the filter buffers the data and initiates the authorization request.
TEST_F(HttpFilterTest, AuthWithRequestData) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  with_request_body:
    max_request_bytes: 10
  )EOF");

  ON_CALL(decoder_filter_callbacks_, decodingBuffer()).WillByDefault(Return(&data_));
  ON_CALL(decoder_filter_callbacks_, addDecodedData(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& data, bool) { data_.add(data); }));
  prepareCheck();

  envoy::service::auth::v3::CheckRequest check_request;
  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest& check_param,
                           Tracing::Span&, const StreamInfo::StreamInfo&) -> void {
        request_callbacks_ = &callbacks;
        check_request = check_param;
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl buffer1("foo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer1, false));
  data_.add(buffer1.toString());

  Buffer::OwnedImpl buffer2("bar");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer2, true));
  // data added by the previous decodeData call.

  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(data_.length(), check_request.attributes().request().http().body().size());
  EXPECT_EQ(0, check_request.attributes().request().http().raw_body().size());
}

// Checks that the filter buffers the data and initiates the authorization request.
TEST_F(HttpFilterTest, AuthWithNonUtf8RequestData) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  with_request_body:
    max_request_bytes: 10
    pack_as_bytes: true
  )EOF");

  ON_CALL(decoder_filter_callbacks_, decodingBuffer()).WillByDefault(Return(&data_));
  ON_CALL(decoder_filter_callbacks_, addDecodedData(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& data, bool) { data_.add(data); }));
  prepareCheck();

  envoy::service::auth::v3::CheckRequest check_request;
  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest& check_param,
                           Tracing::Span&, const StreamInfo::StreamInfo&) -> void {
        request_callbacks_ = &callbacks;
        check_request = check_param;
      }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  // Use non UTF-8 data to fill up the decoding buffer.
  uint8_t raw[1] = {0xc0};
  Buffer::OwnedImpl raw_buffer(raw, 1);

  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(raw_buffer, false));
  data_.add(raw_buffer);

  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark,
            filter_->decodeData(raw_buffer, true));
  // data added by the previous decodeData call.

  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(0, check_request.attributes().request().http().body().size());
  EXPECT_EQ(data_.length(), check_request.attributes().request().http().raw_body().size());
}

// Checks that the filter buffers the data and initiates the authorization request.
TEST_F(HttpFilterTest, AuthWithNonUtf8RequestHeaders) {
  InSequence s;

  // N.B. encode_raw_headers is set to true.
  initialize(R"EOF(
  encode_raw_headers: true
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  )EOF");

  prepareCheck();

  // Add header with non-UTF-8 value.
  absl::string_view header_key = "header-with-non-utf-8-value";
  const uint8_t non_utf_8_bytes[3] = {0xc0, 0xc0, 0};
  absl::string_view header_value = reinterpret_cast<const char*>(non_utf_8_bytes);
  request_headers_.addCopy(Http::LowerCaseString{header_key}, header_value);

  envoy::service::auth::v3::CheckRequest check_request;
  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest& check_request,
                           Tracing::Span&, const StreamInfo::StreamInfo&) -> void {
        request_callbacks_ = &callbacks;
        // headers should be empty.
        EXPECT_EQ(0, check_request.attributes().request().http().headers().size());
        ASSERT_TRUE(check_request.attributes().request().http().has_header_map());
        // header_map should contain the header we added and it should be unchanged.
        bool exact_match_utf_8_header = false;
        for (const auto& header :
             check_request.attributes().request().http().header_map().headers()) {
          if (header.key() == header_key && header.raw_value() == header_value) {
            exact_match_utf_8_header = true;
            break;
          }
        }
        EXPECT_TRUE(exact_match_utf_8_header);
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, true));
}

// Checks that filter does not buffer data on header-only request.
TEST_F(HttpFilterTest, HeaderOnlyRequest) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  with_request_body:
    max_request_bytes: 10
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, true));
  // decodeData() and decodeTrailers() will not be called since request is header only.
}

// Checks that filter does not buffer data on upgrade WebSocket request.
TEST_F(HttpFilterTest, UpgradeWebsocketRequest) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  with_request_body:
    max_request_bytes: 10
  )EOF");

  prepareCheck();

  request_headers_.addCopy(Http::Headers::get().Connection,
                           Http::Headers::get().ConnectionValues.Upgrade);
  request_headers_.addCopy(Http::Headers::get().Upgrade,
                           Http::Headers::get().UpgradeValues.WebSocket);

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  // decodeData() and decodeTrailers() will not be called until continueDecoding() is called.
}

// Checks that filter does not buffer data on upgrade H2 WebSocket request.
TEST_F(HttpFilterTest, H2UpgradeRequest) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  with_request_body:
    max_request_bytes: 10
  )EOF");

  prepareCheck();

  request_headers_.addCopy(Http::Headers::get().Method, Http::Headers::get().MethodValues.Connect);
  request_headers_.addCopy(Http::Headers::get().Protocol,
                           Http::Headers::get().ProtocolStrings.Http2String);

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  // decodeData() and decodeTrailers() will not be called until continueDecoding() is called.
}

// Checks that filter does not buffer data when is not the end of the stream, but header-only
// request has been received.
TEST_F(HttpFilterTest, HeaderOnlyRequestWithStream) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  with_request_body:
    max_request_bytes: 10
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
}

// Checks that the filter removes the specified headers from the request, but
// that pseudo headers and Host are not removed.
TEST_F(HttpFilterTest, HeadersToRemoveRemovesHeadersExceptSpecialHeaders) {
  InSequence s;

  // Set up all the typical headers plus an additional user defined header.
  request_headers_.addCopy(Http::Headers::get().Host, "example.com");
  request_headers_.addCopy(Http::Headers::get().Method, "GET");
  request_headers_.addCopy(Http::Headers::get().Path, "/users");
  request_headers_.addCopy(Http::Headers::get().Protocol, "websocket");
  request_headers_.addCopy(Http::Headers::get().Scheme, "https");
  request_headers_.addCopy("remove-me", "upstream-should-not-see-me");

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  clear_route_cache: true
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  // Let's try to remove all the headers in the request.
  response.headers_to_remove = std::vector<std::string>{
      Http::Headers::get().Host.get(),
      Http::Headers::get().HostLegacy.get(),
      Http::Headers::get().Method.get(),
      Http::Headers::get().Path.get(),
      Http::Headers::get().Protocol.get(),
      Http::Headers::get().Scheme.get(),
      "remove-me",
  };
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  // All :-prefixed headers (and Host) should still be there - only the user
  // defined header should have been removed.
  EXPECT_EQ("example.com", request_headers_.get_(Http::Headers::get().Host));
  EXPECT_EQ("example.com", request_headers_.get_(Http::Headers::get().HostLegacy));
  EXPECT_EQ("GET", request_headers_.get_(Http::Headers::get().Method));
  EXPECT_EQ("/users", request_headers_.get_(Http::Headers::get().Path));
  EXPECT_EQ("websocket", request_headers_.get_(Http::Headers::get().Protocol));
  EXPECT_EQ("https", request_headers_.get_(Http::Headers::get().Scheme));
  EXPECT_TRUE(request_headers_.get(Http::LowerCaseString{"remove-me"}).empty());
}

// Verifies that the filter clears the route cache when an authorization response:
// 1. is an OK response.
// 2. has headers to append.
// 3. has headers to add.
// 4. has headers to remove.
TEST_F(HttpFilterTest, ClearCache) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  clear_route_cache: true
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_CALL(decoder_filter_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_append = {{"foo", "bar"}};
  response.headers_to_set = {{"bar", "foo"}};
  response.headers_to_remove = std::vector<std::string>{"remove-me"};
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.ok")
                    .value());
  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// Verifies that the filter clears the route cache when an authorization response:
// 1. is an OK response.
// 2. has headers to append.
// 3. has NO headers to add.
// 4. has NO headers to remove.
TEST_F(HttpFilterTest, ClearCacheRouteHeadersToAppendOnly) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  clear_route_cache: true
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_CALL(decoder_filter_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_append = {{"foo", "bar"}};
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.ok")
                    .value());
  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// Verifies that the filter clears the route cache when an authorization response:
// 1. is an OK response.
// 2. has NO headers to append.
// 3. has headers to add.
// 4. has NO headers to remove.
TEST_F(HttpFilterTest, ClearCacheRouteHeadersToAddOnly) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  clear_route_cache: true
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_CALL(decoder_filter_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_set = {{"foo", "bar"}};
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.ok")
                    .value());
  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// Verifies that the filter clears the route cache when an authorization response:
// 1. is an OK response.
// 2. has NO headers to append.
// 3. has NO headers to add.
// 4. has headers to remove.
TEST_F(HttpFilterTest, ClearCacheRouteHeadersToRemoveOnly) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  clear_route_cache: true
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_CALL(decoder_filter_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_remove = {"remove-me"};
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.ok")
                    .value());
  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// Test that a DENIED response with a body from the authorization service is truncated if the body
// size is larger than max_denied_response_body_bytes.
TEST_F(HttpFilterTest, DeniedResponseWithBodyTruncation) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  max_denied_response_body_bytes: 10
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService));
  // The body is truncated to 10 bytes.
  EXPECT_CALL(decoder_filter_callbacks_,
              sendLocalReply(Http::Code::Forbidden, "1234567890", _, _, _));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Forbidden;
  response.body = "1234567890-this-should-be-truncated";
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.denied")
                    .value());
  EXPECT_EQ(1U, config_->stats().denied_.value());
}

// Test that a DENIED response with a body from the authorization service is NOT truncated if the
// body size is smaller than max_denied_response_body_bytes.
TEST_F(HttpFilterTest, DeniedResponseWithBodyNotTruncated) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  max_denied_response_body_bytes: 20
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService));
  const std::string body = "body-not-truncated";
  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(Http::Code::Forbidden, body, _, _, _));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Forbidden;
  response.body = body;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.denied")
                    .value());
  EXPECT_EQ(1U, config_->stats().denied_.value());
}

// Test that a DENIED response with a body from the authorization service is NOT truncated if
// max_denied_response_body_bytes is not set (or zero).
TEST_F(HttpFilterTest, DeniedResponseWithBodyNotTruncatedWhenLimitIsZero) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService));
  const std::string body = "this-is-a-long-body-that-will-not-be-truncated";
  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(Http::Code::Forbidden, body, _, _, _));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Forbidden;
  response.body = body;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.denied")
                    .value());
  EXPECT_EQ(1U, config_->stats().denied_.value());
}

class RequestHeaderLimitTest : public HttpFilterTest {
public:
  RequestHeaderLimitTest() = default;

  void runTest(Http::RequestHeaderMap& request_headers,
               Filters::Common::ExtAuthz::Response response) {
    InSequence s;

    initialize(R"EOF(
        grpc_service:
          envoy_grpc:
            cluster_name: "ext_authz_server"
        )EOF");

    prepareCheck();

    EXPECT_CALL(*client_, check(_, _, _, _))
        .WillOnce(Invoke(
            [&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter_->decodeHeaders(request_headers, false));

    // Now the test should fail, since we expect the downstream request to fail.
    EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
                setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService));
    EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _))
        .WillOnce(Invoke([&](const Http::ResponseHeaderMap& headers, bool) -> void {
          EXPECT_EQ(headers.getStatusValue(),
                    std::to_string(enumToInt(Http::Code::InternalServerError)));
        }));
    EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

    request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
    EXPECT_EQ(1U, config_->stats().request_header_limits_reached_.value());
  }
};

TEST_F(RequestHeaderLimitTest, HeadersToSetCount) {
  // The total number of headers in the request header map is not allowed to
  // exceed the limit.
  Http::TestRequestHeaderMapImpl request_headers({}, /*max_headers_kb=*/99999,
                                                 /*max_headers_count=*/4);
  request_headers.addCopy(Http::Headers::get().Host, "host");
  request_headers.addCopy(Http::Headers::get().Path, "/");
  request_headers.addCopy(Http::Headers::get().Method, "GET");

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_set = {{"foo", "bar"}, {"foo2", "bar2"}};

  runTest(request_headers, response);
}

TEST_F(RequestHeaderLimitTest, HeadersToSetSize) {
  // The total number of headers in the request header map is not allowed to
  // exceed the limit.
  Http::TestRequestHeaderMapImpl request_headers({}, /*max_headers_kb=*/1,
                                                 /*max_headers_count=*/9999);
  request_headers.addCopy(Http::Headers::get().Host, "host");
  request_headers.addCopy(Http::Headers::get().Path, "/");
  request_headers.addCopy(Http::Headers::get().Method, "GET");

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_set = {{"foo", "bar"}, {"foo2", std::string(9999, 'a')}};

  runTest(request_headers, response);
}

// (headers to append can't add new headers, so it won't ever violate the count limit)
TEST_F(RequestHeaderLimitTest, HeadersToAppendSize) {
  // The total number of headers in the request header map is not allowed to
  // exceed the limit.
  Http::TestRequestHeaderMapImpl request_headers({}, /*max_headers_kb=*/1,
                                                 /*max_headers_count=*/9999);
  request_headers.addCopy(Http::Headers::get().Host, "host");
  request_headers.addCopy(Http::Headers::get().Path, "/");
  request_headers.addCopy(Http::Headers::get().Method, "GET");
  request_headers.addCopy("foo", "original value");

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_append = {{"foo", std::string(9999, 'a')}};

  runTest(request_headers, response);
}

TEST_F(RequestHeaderLimitTest, HeadersToAddCount) {
  // The total number of headers in the request header map is not allowed to
  // exceed the limit.
  Http::TestRequestHeaderMapImpl request_headers({}, /*max_headers_kb=*/99999,
                                                 /*max_headers_count=*/4);
  request_headers.addCopy(Http::Headers::get().Host, "host");
  request_headers.addCopy(Http::Headers::get().Path, "/");
  request_headers.addCopy(Http::Headers::get().Method, "GET");

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_add = {{"foo", "bar"}, {"foo2", "bar2"}};

  runTest(request_headers, response);
}

TEST_F(RequestHeaderLimitTest, HeadersToAddSize) {
  // The total number of headers in the request header map is not allowed to
  // exceed the limit.
  Http::TestRequestHeaderMapImpl request_headers({}, /*max_headers_kb=*/1,
                                                 /*max_headers_count=*/9999);
  request_headers.addCopy(Http::Headers::get().Host, "host");
  request_headers.addCopy(Http::Headers::get().Path, "/");
  request_headers.addCopy(Http::Headers::get().Method, "GET");

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_add = {{"foo2", std::string(9999, 'a')}};

  runTest(request_headers, response);
}

// Verifies that the downstream request fails when the ext_authz response
// would cause the request headers to exceed their size limit.
TEST_F(HttpFilterTest, DownstreamRequestFailsOnHeaderSizeLimit) {
  InSequence s;

  initialize(R"EOF(
      grpc_service:
        envoy_grpc:
          cluster_name: "ext_authz_server"
      )EOF");

  // The total size of headers in the request header map is not allowed to
  // exceed the limit (1KB).
  Http::TestRequestHeaderMapImpl request_headers({}, /*max_headers_kb=*/10,
                                                 /*max_headers_count=*/9999);
  request_headers.addCopy(Http::Headers::get().Host, "host");
  request_headers.addCopy(Http::Headers::get().Path, "/");
  request_headers.addCopy(Http::Headers::get().Method, "GET");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers, false));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  // A very large header that will cause the request headers to exceed their limit.
  response.headers_to_set = {{"too-big", std::string(10 * 1024, 'a')}};

  // Now the test should fail, since we expect the downstream request to fail.
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService));
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([&](const Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.getStatusValue(),
                  std::to_string(enumToInt(Http::Code::InternalServerError)));
      }));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(decoder_filter_callbacks_.details(), "ext_authz_invalid");
}

// Verifies that the filter DOES NOT clear the route cache when an authorization response:
// 1. is an OK response.
// 2. has NO headers to append.
// 3. has NO headers to add.
// 4. has NO headers to remove.
TEST_F(HttpFilterTest, NoClearCacheRoute) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  clear_route_cache: true
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_CALL(decoder_filter_callbacks_.downstream_callbacks_, clearRouteCache()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.ok")
                    .value());
  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// Verifies that the filter DOES NOT clear the route cache when clear_route_cache is set to false.
TEST_F(HttpFilterTest, NoClearCacheRouteConfig) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_CALL(decoder_filter_callbacks_.downstream_callbacks_, clearRouteCache()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_append = {{"foo", "bar"}};
  response.headers_to_set = {{"bar", "foo"}};
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.ok")
                    .value());
  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// Verifies that the filter DOES NOT clear the route cache when authorization response is NOT OK.
TEST_F(HttpFilterTest, NoClearCacheRouteDeniedResponse) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  clear_route_cache: true
  )EOF");

  prepareCheck();

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Unauthorized;
  response.headers_to_set = {{"foo", "bar"}};
  auto response_ptr = std::make_unique<Filters::Common::ExtAuthz::Response>(response);

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::move(response_ptr));
      }));
  EXPECT_CALL(decoder_filter_callbacks_.downstream_callbacks_, clearRouteCache()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.denied")
                    .value());
  EXPECT_EQ("ext_authz_denied", decoder_filter_callbacks_.details());
}

// Verifies that specified metadata is passed along in the check request
TEST_F(HttpFilterTest, MetadataContext) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  metadata_context_namespaces:
  - jazz.sax
  - rock.guitar
  - hiphop.drums
  - blues.piano
  typed_metadata_context_namespaces:
  - jazz.sax
  - blues.piano
  )EOF");

  const std::string yaml = R"EOF(
  filter_metadata:
    jazz.sax:
      coltrane: john
      parker: charlie
    jazz.piano:
      monk: thelonious
      hancock: herbie
    rock.guitar:
      hendrix: jimi
      richards: keith
  typed_filter_metadata:
    blues.piano:
      '@type': type.googleapis.com/helloworld.HelloRequest
      name: jack dupree
    jazz.sax:
      '@type': type.googleapis.com/helloworld.HelloRequest
      name: shorter wayne
    rock.bass:
      '@type': type.googleapis.com/helloworld.HelloRequest
      name: geddy lee
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(yaml, metadata);
  ON_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata())
      .WillByDefault(ReturnRef(metadata));

  prepareCheck();

  envoy::service::auth::v3::CheckRequest check_request;
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks&,
                     const envoy::service::auth::v3::CheckRequest& check_param, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { check_request = check_param; }));

  filter_->decodeHeaders(request_headers_, false);
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));

  EXPECT_EQ("john", check_request.attributes()
                        .metadata_context()
                        .filter_metadata()
                        .at("jazz.sax")
                        .fields()
                        .at("coltrane")
                        .string_value());

  EXPECT_EQ("jimi", check_request.attributes()
                        .metadata_context()
                        .filter_metadata()
                        .at("rock.guitar")
                        .fields()
                        .at("hendrix")
                        .string_value());

  EXPECT_EQ(0, check_request.attributes().metadata_context().filter_metadata().count("jazz.piano"));

  EXPECT_EQ(0,
            check_request.attributes().metadata_context().filter_metadata().count("hiphop.drums"));

  helloworld::HelloRequest hello;
  check_request.attributes()
      .metadata_context()
      .typed_filter_metadata()
      .at("blues.piano")
      .UnpackTo(&hello);
  EXPECT_EQ("jack dupree", hello.name());

  check_request.attributes()
      .metadata_context()
      .typed_filter_metadata()
      .at("jazz.sax")
      .UnpackTo(&hello);
  EXPECT_EQ("shorter wayne", hello.name());

  EXPECT_EQ(
      0, check_request.attributes().metadata_context().typed_filter_metadata().count("rock.bass"));
}

// Verifies that specified connection metadata is passed along in the check request
TEST_F(HttpFilterTest, ConnectionMetadataContext) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  metadata_context_namespaces:
  - connection.and.request.have.data
  - connection.has.data
  - request.has.data
  - neither.have.data
  - untyped.and.typed.connection.data
  - typed.connection.data
  - untyped.connection.data
  typed_metadata_context_namespaces:
  - untyped.and.typed.connection.data
  - typed.connection.data
  - untyped.connection.data
  )EOF");

  const std::string request_yaml = R"EOF(
  filter_metadata:
    connection.and.request.have.data:
      data: request
    request.has.data:
      data: request
  )EOF";

  const std::string connection_yaml = R"EOF(
  filter_metadata:
    connection.and.request.have.data:
      data: connection_untyped
    connection.has.data:
      data: connection_untyped
    untyped.and.typed.connection.data:
      data: connection_untyped
    untyped.connection.data:
      data: connection_untyped
    not.selected.data:
      data: connection_untyped
  typed_filter_metadata:
    untyped.and.typed.connection.data:
      '@type': type.googleapis.com/helloworld.HelloRequest
      name: connection_typed
    typed.connection.data:
      '@type': type.googleapis.com/helloworld.HelloRequest
      name: connection_typed
    not.selected.data:
      '@type': type.googleapis.com/helloworld.HelloRequest
      name: connection_typed
  )EOF";

  prepareCheck();

  envoy::config::core::v3::Metadata request_metadata, connection_metadata;
  TestUtility::loadFromYaml(request_yaml, request_metadata);
  TestUtility::loadFromYaml(connection_yaml, connection_metadata);
  ON_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata())
      .WillByDefault(ReturnRef(request_metadata));
  connection_.stream_info_.metadata_ = connection_metadata;

  envoy::service::auth::v3::CheckRequest check_request;
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks&,
                     const envoy::service::auth::v3::CheckRequest& check_param, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { check_request = check_param; }));

  filter_->decodeHeaders(request_headers_, false);
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));

  EXPECT_EQ("request", check_request.attributes()
                           .metadata_context()
                           .filter_metadata()
                           .at("connection.and.request.have.data")
                           .fields()
                           .at("data")
                           .string_value());

  EXPECT_EQ("request", check_request.attributes()
                           .metadata_context()
                           .filter_metadata()
                           .at("request.has.data")
                           .fields()
                           .at("data")
                           .string_value());

  EXPECT_EQ("connection_untyped", check_request.attributes()
                                      .metadata_context()
                                      .filter_metadata()
                                      .at("connection.has.data")
                                      .fields()
                                      .at("data")
                                      .string_value());

  EXPECT_EQ("connection_untyped", check_request.attributes()
                                      .metadata_context()
                                      .filter_metadata()
                                      .at("untyped.and.typed.connection.data")
                                      .fields()
                                      .at("data")
                                      .string_value());

  EXPECT_EQ(0, check_request.attributes().metadata_context().filter_metadata().count(
                   "neither.have.data"));

  EXPECT_EQ(0, check_request.attributes().metadata_context().filter_metadata().count(
                   "not.selected.data"));

  EXPECT_EQ(0, check_request.attributes().metadata_context().filter_metadata().count(
                   "typed.connection.data"));

  helloworld::HelloRequest hello;
  check_request.attributes()
      .metadata_context()
      .typed_filter_metadata()
      .at("typed.connection.data")
      .UnpackTo(&hello);
  EXPECT_EQ("connection_typed", hello.name());

  check_request.attributes()
      .metadata_context()
      .typed_filter_metadata()
      .at("untyped.and.typed.connection.data")
      .UnpackTo(&hello);
  EXPECT_EQ("connection_typed", hello.name());

  EXPECT_EQ(0, check_request.attributes().metadata_context().typed_filter_metadata().count(
                   "untyped.connection.data"));

  EXPECT_EQ(0, check_request.attributes().metadata_context().typed_filter_metadata().count(
                   "not.selected.data"));
}

// Verifies that specified route metadata is passed along in the check request
TEST_F(HttpFilterTest, RouteMetadataContext) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  route_metadata_context_namespaces:
  - request.connection.route.have.data
  - request.route.have.data
  - connection.route.have.data
  - route.has.data
  - request.has.data
  - untyped.and.typed.route.data
  - typed.route.data
  - untyped.route.data
  route_typed_metadata_context_namespaces:
  - untyped.and.typed.route.data
  - typed.route.data
  - untyped.route.data
  metadata_context_namespaces:
  - request.connection.route.have.data
  - request.route.have.data
  - connection.route.have.data
  - connection.has.data
  - route.has.data
  )EOF");

  const std::string route_yaml = R"EOF(
  filter_metadata:
    request.connection.route.have.data:
      data: route
    request.route.have.data:
      data: route
    connection.route.have.data:
      data: route
    route.has.data:
      data: route
    untyped.and.typed.route.data:
      data: route_untyped
    untyped.route.data:
      data: route_untyped
  typed_filter_metadata:
    untyped.and.typed.route.data:
      '@type': type.googleapis.com/helloworld.HelloRequest
      name: route_typed
    typed.route.data:
      '@type': type.googleapis.com/helloworld.HelloRequest
      name: route_typed
  )EOF";

  const std::string request_yaml = R"EOF(
  filter_metadata:
    request.connection.route.have.data:
      data: request
    request.route.have.data:
      data: request
  )EOF";

  const std::string connection_yaml = R"EOF(
  filter_metadata:
    request.connection.route.have.data:
      data: connection
    connection.route.have.data:
      data: connection
    connection.has.data:
      data: connection
  )EOF";

  prepareCheck();

  envoy::config::core::v3::Metadata request_metadata, connection_metadata, route_metadata;
  TestUtility::loadFromYaml(request_yaml, request_metadata);
  TestUtility::loadFromYaml(connection_yaml, connection_metadata);
  TestUtility::loadFromYaml(route_yaml, route_metadata);
  ON_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata())
      .WillByDefault(ReturnRef(request_metadata));
  connection_.stream_info_.metadata_ = connection_metadata;
  ON_CALL(*decoder_filter_callbacks_.route_, metadata()).WillByDefault(ReturnRef(route_metadata));

  envoy::service::auth::v3::CheckRequest check_request;
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks&,
                     const envoy::service::auth::v3::CheckRequest& check_param, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { check_request = check_param; }));

  filter_->decodeHeaders(request_headers_, false);
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));

  for (const auto& namespace_from_route : std::vector<std::string>{
           "request.connection.route.have.data",
           "request.route.have.data",
           "connection.route.have.data",
           "route.has.data",
       }) {
    ASSERT_THAT(check_request.attributes().route_metadata_context().filter_metadata(),
                Contains(Key(namespace_from_route)));
    EXPECT_EQ("route", check_request.attributes()
                           .route_metadata_context()
                           .filter_metadata()
                           .at(namespace_from_route)
                           .fields()
                           .at("data")
                           .string_value());
  }
  EXPECT_THAT(check_request.attributes().route_metadata_context().filter_metadata(),
              Not(Contains(Key("request.has.data"))));

  for (const auto& namespace_from_request :
       std::vector<std::string>{"request.connection.route.have.data", "request.route.have.data"}) {
    ASSERT_THAT(check_request.attributes().metadata_context().filter_metadata(),
                Contains(Key(namespace_from_request)));
    EXPECT_EQ("request", check_request.attributes()
                             .metadata_context()
                             .filter_metadata()
                             .at(namespace_from_request)
                             .fields()
                             .at("data")
                             .string_value());
  }
  for (const auto& namespace_from_connection :
       std::vector<std::string>{"connection.route.have.data", "connection.has.data"}) {
    ASSERT_THAT(check_request.attributes().metadata_context().filter_metadata(),
                Contains(Key(namespace_from_connection)));
    EXPECT_EQ("connection", check_request.attributes()
                                .metadata_context()
                                .filter_metadata()
                                .at(namespace_from_connection)
                                .fields()
                                .at("data")
                                .string_value());
  }
  EXPECT_THAT(check_request.attributes().metadata_context().filter_metadata(),
              Not(Contains(Key("route.has.data"))));

  for (const auto& namespace_from_route_untyped :
       std::vector<std::string>{"untyped.and.typed.route.data", "untyped.route.data"}) {
    ASSERT_THAT(check_request.attributes().route_metadata_context().filter_metadata(),
                Contains(Key(namespace_from_route_untyped)));
    EXPECT_EQ("route_untyped", check_request.attributes()
                                   .route_metadata_context()
                                   .filter_metadata()
                                   .at(namespace_from_route_untyped)
                                   .fields()
                                   .at("data")
                                   .string_value());
  }
  EXPECT_THAT(check_request.attributes().route_metadata_context().filter_metadata(),
              Not(Contains(Key("typed.route.data"))));

  for (const auto& namespace_from_route_typed :
       std::vector<std::string>{"untyped.and.typed.route.data", "typed.route.data"}) {
    ASSERT_THAT(check_request.attributes().route_metadata_context().typed_filter_metadata(),
                Contains(Key(namespace_from_route_typed)));
    helloworld::HelloRequest hello;
    EXPECT_TRUE(check_request.attributes()
                    .route_metadata_context()
                    .typed_filter_metadata()
                    .at(namespace_from_route_typed)
                    .UnpackTo(&hello));
    EXPECT_EQ("route_typed", hello.name());
  }
  EXPECT_THAT(check_request.attributes().route_metadata_context().typed_filter_metadata(),
              Not(Contains(Key("untyped.route.data"))));
}

// Test that filter can be disabled via the filter_enabled field.
TEST_F(HttpFilterTest, FilterDisabled) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  filter_enabled:
    runtime_key: "http.ext_authz.enabled"
    default_value:
      numerator: 0
      denominator: HUNDRED
  emit_filter_state_stats: true
  )EOF");

  ON_CALL(factory_context_.runtime_loader_.snapshot_,
          featureEnabled("http.ext_authz.enabled",
                         testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(0))))
      .WillByDefault(Return(false));

  // Make sure check is not called.
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  // The stats / logging filter state should not be added if no request is sent.
  auto filter_state = decoder_filter_callbacks_.streamInfo().filterState();
  EXPECT_FALSE(filter_state->hasData<ExtAuthzLoggingInfo>(FilterConfigName));
}

// Test that filter can be enabled via the filter_enabled field.
TEST_F(HttpFilterTest, FilterEnabled) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  filter_enabled:
    runtime_key: "http.ext_authz.enabled"
    default_value:
      numerator: 100
      denominator: HUNDRED
  )EOF");

  prepareCheck();

  ON_CALL(factory_context_.runtime_loader_.snapshot_,
          featureEnabled("http.ext_authz.enabled",
                         testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillByDefault(Return(true));

  // Make sure check is called once.
  EXPECT_CALL(*client_, check(_, _, _, _));
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
}

// Test that filter can be disabled via the filter_enabled_metadata field.
TEST_F(HttpFilterTest, MetadataDisabled) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  filter_enabled_metadata:
    filter: "abc.xyz"
    path:
    - key: "k1"
    value:
      string_match:
        exact: "check"
  )EOF");

  // Disable in filter_enabled.
  const std::string yaml = R"EOF(
  filter_metadata:
    abc.xyz:
      k1: skip
  )EOF";
  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(yaml, metadata);
  ON_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata())
      .WillByDefault(ReturnRef(metadata));

  // Make sure check is not called.
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

// Test that filter can be enabled via the filter_enabled_metadata field.
TEST_F(HttpFilterTest, MetadataEnabled) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  filter_enabled_metadata:
    filter: "abc.xyz"
    path:
    - key: "k1"
    value:
      string_match:
        exact: "check"
  )EOF");

  // Enable in filter_enabled.
  const std::string yaml = R"EOF(
  filter_metadata:
    abc.xyz:
      k1: check
  )EOF";
  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(yaml, metadata);
  ON_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata())
      .WillByDefault(ReturnRef(metadata));

  prepareCheck();

  // Make sure check is called once.
  EXPECT_CALL(*client_, check(_, _, _, _));
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
}

// Test that the filter is disabled if one of the filter_enabled and filter_enabled_metadata field
// is disabled.
TEST_F(HttpFilterTest, FilterEnabledButMetadataDisabled) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  filter_enabled:
    runtime_key: "http.ext_authz.enabled"
    default_value:
      numerator: 100
      denominator: HUNDRED
  filter_enabled_metadata:
    filter: "abc.xyz"
    path:
    - key: "k1"
    value:
      string_match:
        exact: "check"
  )EOF");

  // Enable in filter_enabled.
  ON_CALL(factory_context_.runtime_loader_.snapshot_,
          featureEnabled("http.ext_authz.enabled",
                         testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillByDefault(Return(true));

  // Disable in filter_enabled_metadata.
  const std::string yaml = R"EOF(
  filter_metadata:
    abc.xyz:
      k1: skip
  )EOF";
  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(yaml, metadata);
  ON_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata())
      .WillByDefault(ReturnRef(metadata));

  // Make sure check is not called.
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

// Test that the filter is disabled if one of the filter_enabled and filter_enabled_metadata field
// is disabled.
TEST_F(HttpFilterTest, FilterDisabledButMetadataEnabled) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  filter_enabled:
    runtime_key: "http.ext_authz.enabled"
    default_value:
      numerator: 0
      denominator: HUNDRED
  filter_enabled_metadata:
    filter: "abc.xyz"
    path:
    - key: "k1"
    value:
      string_match:
        exact: "check"
  )EOF");

  // Disable in filter_enabled.
  ON_CALL(factory_context_.runtime_loader_.snapshot_,
          featureEnabled("http.ext_authz.enabled",
                         testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(0))))
      .WillByDefault(Return(false));

  // Enable in filter_enabled_metadata.
  const std::string yaml = R"EOF(
  filter_metadata:
    abc.xyz:
      k1: check
  )EOF";
  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(yaml, metadata);
  ON_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata())
      .WillByDefault(ReturnRef(metadata));

  // Make sure check is not called.
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

// Test that the filter is enabled if both the filter_enabled and filter_enabled_metadata field
// is enabled.
TEST_F(HttpFilterTest, FilterEnabledAndMetadataEnabled) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  filter_enabled:
    runtime_key: "http.ext_authz.enabled"
    default_value:
      numerator: 100
      denominator: HUNDRED
  filter_enabled_metadata:
    filter: "abc.xyz"
    path:
    - key: "k1"
    value:
      string_match:
        exact: "check"
  )EOF");

  // Enable in filter_enabled.
  ON_CALL(factory_context_.runtime_loader_.snapshot_,
          featureEnabled("http.ext_authz.enabled",
                         testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillByDefault(Return(true));

  // Enable in filter_enabled_metadata.
  const std::string yaml = R"EOF(
  filter_metadata:
    abc.xyz:
      k1: check
  )EOF";
  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(yaml, metadata);
  ON_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata())
      .WillByDefault(ReturnRef(metadata));

  prepareCheck();

  // Make sure check is called once.
  EXPECT_CALL(*client_, check(_, _, _, _));
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
}

// Test that filter can deny for protected path when filter is disabled via filter_enabled field.
TEST_F(HttpFilterTest, FilterDenyAtDisable) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  filter_enabled:
    runtime_key: "http.ext_authz.enabled"
    default_value:
      numerator: 0
      denominator: HUNDRED
  deny_at_disable:
    runtime_key: "http.ext_authz.deny_at_disable"
    default_value:
      value: true
  )EOF");

  ON_CALL(factory_context_.runtime_loader_.snapshot_,
          featureEnabled("http.ext_authz.enabled",
                         testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(0))))
      .WillByDefault(Return(false));

  ON_CALL(factory_context_.runtime_loader_.snapshot_,
          featureEnabled("http.ext_authz.enabled", false))
      .WillByDefault(Return(true));

  // Make sure check is not called.
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
}

// Test that filter allows for protected path when filter is disabled via filter_enabled field.
TEST_F(HttpFilterTest, FilterAllowAtDisable) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  filter_enabled:
    runtime_key: "http.ext_authz.enabled"
    default_value:
      numerator: 0
      denominator: HUNDRED
  deny_at_disable:
    runtime_key: "http.ext_authz.deny_at_disable"
    default_value:
      value: false
  )EOF");

  ON_CALL(factory_context_.runtime_loader_.snapshot_,
          featureEnabled("http.ext_authz.enabled",
                         testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(0))))
      .WillByDefault(Return(false));

  ON_CALL(factory_context_.runtime_loader_.snapshot_,
          featureEnabled("http.ext_authz.enabled", false))
      .WillByDefault(Return(false));

  // Make sure check is not called.
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

// -------------------
// Parameterized Tests
// -------------------

// Test that context extensions make it into the check request.
TEST_P(HttpFilterTestParam, ContextExtensions) {
  // Place something in the context extensions on the virtualhost.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settingsvhost;
  (*settingsvhost.mutable_check_settings()->mutable_context_extensions())["key_vhost"] =
      "value_vhost";
  // add a default route value to see it overridden
  (*settingsvhost.mutable_check_settings()->mutable_context_extensions())["key_route"] =
      "default_route_value";
  // Initialize the virtual host's per filter config.
  FilterConfigPerRoute auth_per_vhost(settingsvhost);

  // Place something in the context extensions on the route.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settingsroute;
  (*settingsroute.mutable_check_settings()->mutable_context_extensions())["key_route"] =
      "value_route";
  // Initialize the route's per filter config.
  FilterConfigPerRoute auth_per_route(settingsroute);

  EXPECT_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillOnce(Return(&auth_per_route));
  EXPECT_CALL(*decoder_filter_callbacks_.route_, perFilterConfigs(_))
      .WillOnce(Invoke([&](absl::string_view) -> Router::RouteSpecificFilterConfigs {
        return {&auth_per_vhost, &auth_per_route};
      }));

  prepareCheck();

  // Save the check request from the check call.
  envoy::service::auth::v3::CheckRequest check_request;
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks&,
                     const envoy::service::auth::v3::CheckRequest& check_param, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { check_request = check_param; }));

  // Engage the filter so that check is called.
  filter_->decodeHeaders(request_headers_, false);
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));

  // Make sure that the extensions appear in the check request issued by the filter.
  EXPECT_EQ("value_vhost", check_request.attributes().context_extensions().at("key_vhost"));
  EXPECT_EQ("value_route", check_request.attributes().context_extensions().at("key_route"));
}

// Test that filter can be disabled with route config.
TEST_P(HttpFilterTestParam, DisabledOnRoute) {
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settings;
  std::unique_ptr<FilterConfigPerRoute> auth_per_route =
      std::make_unique<FilterConfigPerRoute>(settings);

  prepareCheck();

  auto test_disable = [&](bool disabled) {
    initialize("");
    // Set disabled
    settings.set_disabled(disabled);
    // Initialize the route's per filter config.
    auth_per_route = std::make_unique<FilterConfigPerRoute>(settings);
    // Update the mock to return the new pointer
    ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
        .WillByDefault(Return(auth_per_route.get()));
  };

  // baseline: make sure that when not disabled, check is called
  test_disable(false);
  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _));
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  // test that disabling works
  test_disable(true);
  // Make sure check is not called.
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

// Test that filter can be disabled with route config.
TEST_P(HttpFilterTestParam, DisabledOnRouteWithRequestBody) {
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settings;
  std::unique_ptr<FilterConfigPerRoute> auth_per_route =
      std::make_unique<FilterConfigPerRoute>(settings);

  auto test_disable = [&](bool disabled) {
    initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  with_request_body:
    max_request_bytes: 1
    allow_partial_message: false
  )EOF");

    // Set the filter disabled setting.
    settings.set_disabled(disabled);
    // Initialize the route's per filter config.
    auth_per_route = std::make_unique<FilterConfigPerRoute>(settings);
    // Update the mock to return the new pointer.
    ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
        .WillByDefault(Return(auth_per_route.get()));
  };

  test_disable(false);
  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  // When filter is not disabled, setDecoderBufferLimit is called.
  EXPECT_CALL(decoder_filter_callbacks_, setDecoderBufferLimit(_));
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data_, false));

  // To test that disabling the filter works.
  test_disable(true);
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  // Make sure that setDecoderBufferLimit is skipped.
  EXPECT_CALL(decoder_filter_callbacks_, setDecoderBufferLimit(_)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
}

// Test that authentication will do when the filter_callbacks has no route.(both
// direct response and redirect have no route)
TEST_P(HttpFilterTestParam, NoRoute) {
  EXPECT_CALL(*decoder_filter_callbacks_.route_, routeEntry()).WillRepeatedly(Return(nullptr));
  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
}

// Test that the request is stopped till there is an OK response back after which it continues on.
TEST_P(HttpFilterTestParam, OkResponse) {
  InSequence s;

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  // Send an OK response Without setting the dynamic metadata field.
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_, setDynamicMetadata(_, _)).Times(0);
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.ok")
                    .value());
  EXPECT_EQ(1U, config_->stats().ok_.value());
  // decodeData() and decodeTrailers() are called after continueDecoding().
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
}

TEST_P(HttpFilterTestParam, RequestHeaderMatchersForGrpcService) {
  initialize(R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: "ext_authz_server"
    )EOF");

  EXPECT_TRUE(config_->allowedHeadersMatcher() == nullptr);
}

TEST_P(HttpFilterTestParam, RequestHeaderMatchersForHttpService) {
  initialize(R"EOF(
    http_service:
      server_uri:
        uri: "ext_authz:9000"
        cluster: "ext_authz"
        timeout: 0.25s
    )EOF");

  EXPECT_TRUE(config_->allowedHeadersMatcher() != nullptr);
  EXPECT_TRUE(config_->allowedHeadersMatcher()->matches(Http::Headers::get().Method.get()));
  EXPECT_TRUE(config_->allowedHeadersMatcher()->matches(Http::Headers::get().Host.get()));
  EXPECT_TRUE(config_->allowedHeadersMatcher()->matches(Http::Headers::get().Path.get()));
  EXPECT_TRUE(
      config_->allowedHeadersMatcher()->matches(Http::CustomHeaders::get().Authorization.get()));
}

TEST_P(HttpFilterTestParam, RequestHeaderMatchersForGrpcServiceWithAllowedHeaders) {
  const Http::LowerCaseString foo{"foo"};
  initialize(R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: "ext_authz_server"
    allowed_headers:
      patterns:
      - exact: Foo
        ignore_case: true
    )EOF");

  EXPECT_TRUE(config_->allowedHeadersMatcher() != nullptr);
  EXPECT_TRUE(config_->allowedHeadersMatcher()->matches(foo.get()));
}

TEST_P(HttpFilterTestParam, RequestHeaderMatchersForGrpcServiceWithDisallowedHeaders) {
  const Http::LowerCaseString foo{"foo"};
  initialize(R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: "ext_authz_server"
    disallowed_headers:
      patterns:
      - exact: Foo
        ignore_case: true
    )EOF");

  EXPECT_TRUE(config_->disallowedHeadersMatcher() != nullptr);
  EXPECT_TRUE(config_->disallowedHeadersMatcher()->matches(foo.get()));
}

TEST_P(HttpFilterTestParam, RequestHeaderMatchersForHttpServiceWithAllowedHeaders) {
  const Http::LowerCaseString foo{"foo"};
  initialize(R"EOF(
    http_service:
      server_uri:
        uri: "ext_authz:9000"
        cluster: "ext_authz"
        timeout: 0.25s
    allowed_headers:
      patterns:
      - exact: Foo
        ignore_case: true
    )EOF");

  EXPECT_TRUE(config_->allowedHeadersMatcher() != nullptr);
  EXPECT_TRUE(config_->allowedHeadersMatcher()->matches(Http::Headers::get().Method.get()));
  EXPECT_TRUE(config_->allowedHeadersMatcher()->matches(Http::Headers::get().Host.get()));
  EXPECT_TRUE(
      config_->allowedHeadersMatcher()->matches(Http::CustomHeaders::get().Authorization.get()));
  EXPECT_FALSE(config_->allowedHeadersMatcher()->matches(Http::Headers::get().ContentLength.get()));
  EXPECT_TRUE(config_->allowedHeadersMatcher()->matches(foo.get()));
}

TEST_P(HttpFilterTestParam, RequestHeaderMatchersForHttpServiceWithDisallowedHeaders) {
  const Http::LowerCaseString foo{"foo"};
  initialize(R"EOF(
    http_service:
      server_uri:
        uri: "ext_authz:9000"
        cluster: "ext_authz"
        timeout: 0.25s
    disallowed_headers:
      patterns:
      - exact: Foo
        ignore_case: true
    )EOF");

  EXPECT_TRUE(config_->disallowedHeadersMatcher() != nullptr);
  EXPECT_TRUE(config_->disallowedHeadersMatcher()->matches(foo.get()));
}

TEST_P(HttpFilterTestParam,
       DEPRECATED_FEATURE_TEST(RequestHeaderMatchersForHttpServiceWithLegacyAllowedHeaders)) {
  const Http::LowerCaseString foo{"foo"};
  initialize(R"EOF(
    http_service:
      server_uri:
        uri: "ext_authz:9000"
        cluster: "ext_authz"
        timeout: 0.25s
      authorization_request:
        allowed_headers:
          patterns:
          - exact: Foo
            ignore_case: true
    )EOF");

  EXPECT_TRUE(config_->allowedHeadersMatcher() != nullptr);
  EXPECT_TRUE(config_->allowedHeadersMatcher()->matches(Http::Headers::get().Method.get()));
  EXPECT_TRUE(config_->allowedHeadersMatcher()->matches(Http::Headers::get().Host.get()));
  EXPECT_TRUE(
      config_->allowedHeadersMatcher()->matches(Http::CustomHeaders::get().Authorization.get()));
  EXPECT_FALSE(config_->allowedHeadersMatcher()->matches(Http::Headers::get().ContentLength.get()));
  EXPECT_TRUE(config_->allowedHeadersMatcher()->matches(foo.get()));
}

TEST_P(HttpFilterTestParam, DEPRECATED_FEATURE_TEST(DuplicateAllowedHeadersConfigIsInvalid)) {
  EXPECT_THROW(initialize(R"EOF(
  http_service:
    server_uri:
      uri: "ext_authz:9000"
      cluster: "ext_authz"
      timeout: 0.25s
    authorization_request:
      allowed_headers:
        patterns:
        - exact: Foo
          ignore_case: true
  allowed_headers:
    patterns:
    - exact: Bar
      ignore_case: true
  failure_mode_allow: true
  )EOF"),
               EnvoyException);
}

// Test that an synchronous OK response from the authorization service, on the call stack, results
// in request continuing on.
TEST_P(HttpFilterTestParam, ImmediateOkResponse) {
  InSequence s;

  prepareCheck();

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.ok")
                    .value());
  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// Test that an synchronous denied response from the authorization service passing additional HTTP
// attributes to the downstream.
TEST_P(HttpFilterTestParam, ImmediateDeniedResponseWithHttpAttributes) {
  InSequence s;

  prepareCheck();

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Unauthorized;
  response.headers_to_set = {{"foo", "bar"}};
  response.body = std::string{"baz"};

  auto response_ptr = std::make_unique<Filters::Common::ExtAuthz::Response>(response);

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::move(response_ptr));
      }));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.denied")
                    .value());
  EXPECT_EQ(1U, config_->stats().denied_.value());
  // When request is denied, no call to continueDecoding(). As a result, decodeData() and
  // decodeTrailer() will not be called.
}

// Test that an synchronous ok response from the authorization service passing additional HTTP
// attributes to the upstream.
TEST_P(HttpFilterTestParam, ImmediateOkResponseWithHttpAttributes) {
  InSequence s;

  // `bar` will be appended to this header.
  const Http::LowerCaseString request_header_key{"baz"};
  request_headers_.addCopy(request_header_key, "foo");

  // `foo` will be added to this key.
  const Http::LowerCaseString key_to_add{"bar"};

  // `foo` will be override with `bar`.
  const Http::LowerCaseString key_to_override{"foobar"};
  request_headers_.addCopy("foobar", "foo");

  // `remove-me` will be removed
  const Http::LowerCaseString key_to_remove("remove-me");
  request_headers_.addCopy(key_to_remove, "upstream-should-not-see-me");

  prepareCheck();

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_append = {{request_header_key.get(), "bar"}};
  response.headers_to_set = {{key_to_add.get(), "foo"}, {key_to_override.get(), "bar"}};
  response.headers_to_remove = {key_to_remove.get()};
  // This cookie will be appended to the encoded headers.
  response.response_headers_to_add = {{"set-cookie", "cookie2=gingerbread"}};
  // This "should-be-overridden" header value from the auth server will override the
  // "should-be-overridden" entry from the upstream server.
  response.response_headers_to_set = {{"should-be-overridden", "finally-set-by-auth-server"}};

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
  EXPECT_EQ(request_headers_.get_(request_header_key), "foo,bar");
  EXPECT_EQ(request_headers_.get_(key_to_add), "foo");
  EXPECT_EQ(request_headers_.get_(key_to_override), "bar");
  EXPECT_EQ(request_headers_.has(key_to_remove), false);

  Buffer::OwnedImpl response_data{};
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"set-cookie", "cookie1=snickerdoodle"},
      {"should-be-overridden", "originally-set-by-upstream"}};
  Http::TestResponseTrailerMapImpl response_trailers{};
  Http::MetadataMap response_metadata{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(response_metadata));
  EXPECT_EQ(Http::HeaderUtility::getAllOfHeaderAsString(response_headers,
                                                        Http::LowerCaseString{"set-cookie"})
                .result()
                .value(),
            "cookie1=snickerdoodle,cookie2=gingerbread");
  EXPECT_EQ(response_headers.get_("should-be-overridden"), "finally-set-by-auth-server");
}

TEST_P(HttpFilterTestParam, OkWithResponseHeadersAndAppendActions) {
  InSequence s;

  prepareCheck();

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.response_headers_to_add_if_absent = {{"header-to-add-if-absent", "new-value"}};
  response.response_headers_to_overwrite_if_exists = {
      {"header-to-overwrite-if-exists", "new-value"}};

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

  Buffer::OwnedImpl response_data{};
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"}, {"header-to-overwrite-if-exists", "original-value"}};
  Http::TestResponseTrailerMapImpl response_trailers{};
  Http::MetadataMap response_metadata{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(response_metadata));
  EXPECT_EQ(response_headers.get_("header-to-add-if-absent"), "new-value");
  EXPECT_EQ(response_headers.get_("header-to-overwrite-if-exists"), "new-value");
}

TEST_P(HttpFilterTestParam, OkWithResponseHeadersAndAppendActionsDoNotTakeEffect) {
  InSequence s;

  prepareCheck();

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.response_headers_to_add_if_absent = {{"header-to-add-if-absent", "new-value"}};
  response.response_headers_to_overwrite_if_exists = {
      {"header-to-overwrite-if-exists", "new-value"}};

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

  Buffer::OwnedImpl response_data{};
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"header-to-add-if-absent", "original-value"}};
  Http::TestResponseTrailerMapImpl response_trailers{};
  Http::MetadataMap response_metadata{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(response_metadata));
  EXPECT_EQ(response_headers.get_("header-to-add-if-absent"), "original-value");
  EXPECT_FALSE(response_headers.has("header-to-overwrite-if-exists"));
}

TEST_P(HttpFilterTestParam, ImmediateOkResponseWithUnmodifiedQueryParameters) {
  const std::string original_path{"/users?leave-me=alone"};
  const std::string expected_path{"/users?leave-me=alone"};
  const Http::Utility::QueryParamsVector add_me{};
  const std::vector<std::string> remove_me{"remove-me"};
  queryParameterTest(original_path, expected_path, add_me, remove_me);
}

TEST_P(HttpFilterTestParam, ImmediateOkResponseWithRepeatedUnmodifiedQueryParameters) {
  const std::string original_path{"/users?leave-me=alone&leave-me=in-peace"};
  const std::string expected_path{"/users?leave-me=alone&leave-me=in-peace"};
  const Http::Utility::QueryParamsVector add_me{};
  const std::vector<std::string> remove_me{"remove-me"};
  queryParameterTest(original_path, expected_path, add_me, remove_me);
}

TEST_P(HttpFilterTestParam, ImmediateOkResponseWithAddedQueryParameters) {
  const std::string original_path{"/users"};
  const std::string expected_path{"/users?add-me=123"};
  const Http::Utility::QueryParamsVector add_me{{"add-me", "123"}};
  const std::vector<std::string> remove_me{};
  queryParameterTest(original_path, expected_path, add_me, remove_me);
}

TEST_P(HttpFilterTestParam, ImmediateOkResponseWithAddedAndRemovedQueryParameters) {
  const std::string original_path{"/users?remove-me=123"};
  const std::string expected_path{"/users?add-me=456"};
  const Http::Utility::QueryParamsVector add_me{{"add-me", "456"}};
  const std::vector<std::string> remove_me{{"remove-me"}};
  queryParameterTest(original_path, expected_path, add_me, remove_me);
}

TEST_P(HttpFilterTestParam, ImmediateOkResponseWithRemovedQueryParameters) {
  const std::string original_path{"/users?remove-me=definitely"};
  const std::string expected_path{"/users"};
  const Http::Utility::QueryParamsVector add_me{};
  const std::vector<std::string> remove_me{{"remove-me"}};
  queryParameterTest(original_path, expected_path, add_me, remove_me);
}

TEST_P(HttpFilterTestParam, ImmediateOkResponseWithOverwrittenQueryParameters) {
  const std::string original_path{"/users?overwrite-me=original"};
  const std::string expected_path{"/users?overwrite-me=new"};
  const Http::Utility::QueryParamsVector add_me{{"overwrite-me", "new"}};
  const std::vector<std::string> remove_me{};
  queryParameterTest(original_path, expected_path, add_me, remove_me);
}

TEST_P(HttpFilterTestParam, ImmediateOkResponseWithManyModifiedQueryParameters) {
  const std::string original_path{"/users?remove-me=1&overwrite-me=2&leave-me=3"};
  const std::string expected_path{"/users?add-me=9&leave-me=3&overwrite-me=new"};
  const Http::Utility::QueryParamsVector add_me{{"add-me", "9"}, {"overwrite-me", "new"}};
  const std::vector<std::string> remove_me{{"remove-me"}};
  queryParameterTest(original_path, expected_path, add_me, remove_me);
}

// Test that an synchronous denied response from the authorization service, on the call stack,
// results in request not continuing.
TEST_P(HttpFilterTestParam, ImmediateDeniedResponse) {
  InSequence s;

  prepareCheck();

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.denied")
                    .value());
  EXPECT_EQ(1U, config_->stats().denied_.value());
  // When request is denied, no call to continueDecoding(). As a result, decodeData() and
  // decodeTrailer() will not be called.
}

// Test that a denied response results in the connection closing with a 401 response to the client.
TEST_P(HttpFilterTestParam, DeniedResponseWith401) {
  InSequence s;

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "401"}};
  EXPECT_CALL(decoder_filter_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&response_headers), true));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Unauthorized;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.denied")
                    .value());
  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("upstream_rq_4xx")
                    .value());
}

// Test that a denied response results in the connection closing with a 401 response to the client.
TEST_P(HttpFilterTestParam, DeniedResponseWith401NoClusterResponseCodeStats) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  charge_cluster_response_stats:
    value: false
  )EOF");

  InSequence s;

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "401"}};
  EXPECT_CALL(decoder_filter_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&response_headers), true));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Unauthorized;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.denied")
                    .value());
  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(0, decoder_filter_callbacks_.clusterInfo()
                   ->statsScope()
                   .counterFromString("upstream_rq_4xx")
                   .value());
}

// Test that a denied response results in the connection closing with a 403 response to the client.
TEST_P(HttpFilterTestParam, DeniedResponseWith403) {
  InSequence s;

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "403"}};
  EXPECT_CALL(decoder_filter_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&response_headers), true));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Forbidden;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.denied")
                    .value());
  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("upstream_rq_4xx")
                    .value());
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("upstream_rq_403")
                    .value());
}

// Verify that authz response memory is not used after free.
TEST_P(HttpFilterTestParam, DestroyResponseBeforeSendLocalReply) {
  InSequence s;

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Forbidden;
  response.body = std::string{"foo"};
  response.headers_to_set = {{"foo", "bar"}, {"bar", "foo"}};
  Filters::Common::ExtAuthz::ResponsePtr response_ptr =
      std::make_unique<Filters::Common::ExtAuthz::Response>(response);

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "403"},
                                                   {"content-length", "3"},
                                                   {"content-type", "text/plain"},
                                                   {"foo", "bar"},
                                                   {"bar", "foo"}};
  Http::HeaderMap* saved_headers;
  EXPECT_CALL(decoder_filter_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&response_headers), false))
      .WillOnce(Invoke([&](Http::HeaderMap& headers, bool) { saved_headers = &headers; }));
  EXPECT_CALL(decoder_filter_callbacks_, encodeData(_, true))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        response_ptr.reset();
        Http::TestRequestHeaderMapImpl test_headers{*saved_headers};
        EXPECT_EQ(test_headers.get_("foo"), "bar");
        EXPECT_EQ(test_headers.get_("bar"), "foo");
        EXPECT_EQ(data.toString(), "foo");
      }));

  request_callbacks_->onComplete(std::move(response_ptr));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.denied")
                    .value());
  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("upstream_rq_4xx")
                    .value());
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("upstream_rq_403")
                    .value());
}

// Verify that authz denied response headers overrides the existing encoding headers,
// and that it adds repeated header names using the standard method of comma concatenation of values
// for predefined inline headers while repeating other headers
TEST_P(HttpFilterTestParam, OverrideEncodingHeaders) {
  InSequence s;

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Forbidden;
  response.body = std::string{"foo"};
  response.headers_to_set = {{"foo", "bar"},
                             {"bar", "foo"},
                             {"set-cookie", "cookie1=value"},
                             {"set-cookie", "cookie2=value"},
                             {"accept-encoding", "gzip,deflate"}};
  Filters::Common::ExtAuthz::ResponsePtr response_ptr =
      std::make_unique<Filters::Common::ExtAuthz::Response>(response);

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "403"},
                                                   {"content-length", "3"},
                                                   {"content-type", "text/plain"},
                                                   {"foo", "bar"},
                                                   {"bar", "foo"},
                                                   {"set-cookie", "cookie1=value"},
                                                   {"set-cookie", "cookie2=value"},
                                                   {"accept-encoding", "gzip,deflate"}};
  Http::HeaderMap* saved_headers;
  EXPECT_CALL(decoder_filter_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&response_headers), false))
      .WillOnce(Invoke([&](Http::HeaderMap& headers, bool) {
        headers.addCopy(Http::LowerCaseString{"foo"}, std::string{"OVERRIDE_WITH_bar"});
        headers.addCopy(Http::LowerCaseString{"foobar"}, std::string{"DO_NOT_OVERRIDE"});
        saved_headers = &headers;
      }));
  EXPECT_CALL(decoder_filter_callbacks_, encodeData(_, true))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        response_ptr.reset();
        Http::TestRequestHeaderMapImpl test_headers{*saved_headers};
        EXPECT_EQ(test_headers.get_("foo"), "bar");
        EXPECT_EQ(test_headers.get_("bar"), "foo");
        EXPECT_EQ(test_headers.get_("foobar"), "DO_NOT_OVERRIDE");
        EXPECT_EQ(test_headers.get_("accept-encoding"), "gzip,deflate");
        EXPECT_EQ(data.toString(), "foo");
        EXPECT_EQ(Http::HeaderUtility::getAllOfHeaderAsString(test_headers,
                                                              Http::LowerCaseString{"set-cookie"})
                      .result()
                      .value(),
                  "cookie1=value,cookie2=value");
      }));

  request_callbacks_->onComplete(std::move(response_ptr));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.denied")
                    .value());
  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("upstream_rq_4xx")
                    .value());
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("upstream_rq_403")
                    .value());
}

// Verify that when returning an OK response with dynamic_metadata field set, the filter emits
// dynamic metadata.
TEST_F(HttpFilterTest, EmitDynamicMetadata) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  decoder_filter_callbacks_.dispatcher_.globalTimeSystem().advanceTimeWait(
      std::chrono::milliseconds(10));
  Protobuf::Value ext_authz_duration_value;
  ext_authz_duration_value.set_number_value(10);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_set = {{"foo", "bar"}};
  (*response.dynamic_metadata.mutable_fields())["ext_authz_duration"] = ext_authz_duration_value;

  initializeMetadata(response);

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_, setDynamicMetadata(_, _))
      .WillOnce(Invoke([&response](const std::string& ns,
                                   const Protobuf::Struct& returned_dynamic_metadata) {
        EXPECT_EQ(ns, "envoy.filters.http.ext_authz");
        // Check timing metadata correctness
        EXPECT_TRUE(returned_dynamic_metadata.fields().at("ext_authz_duration").has_number_value());

        EXPECT_TRUE(TestUtility::protoEqual(returned_dynamic_metadata, response.dynamic_metadata));
        EXPECT_EQ(response.dynamic_metadata.fields().at("ext_authz_duration").number_value(),
                  returned_dynamic_metadata.fields().at("ext_authz_duration").number_value());
      }));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService))
      .Times(0);
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.ok")
                    .value());
  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// Verify that when returning a Denied response with dynamic_metadata field set, the filter emits
// dynamic metadata.
TEST_F(HttpFilterTest, EmitDynamicMetadataWhenDenied) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  )EOF");

  prepareCheck();
  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Unauthorized;
  response.headers_to_set = {{"foo", "bar"}};

  initializeMetadata(response);

  auto response_ptr = std::make_unique<Filters::Common::ExtAuthz::Response>(response);

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        request_callbacks_ = &callbacks;
        callbacks.onComplete(std::move(response_ptr));
      }));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_, setDynamicMetadata(_, _))
      .WillOnce(Invoke([&response](const std::string& ns,
                                   const Protobuf::Struct& returned_dynamic_metadata) {
        EXPECT_EQ(ns, "envoy.filters.http.ext_authz");
        // Check timing metadata correctness
        EXPECT_FALSE(returned_dynamic_metadata.fields().contains("ext_authz_duration"));
        EXPECT_FALSE(response.dynamic_metadata.fields().contains("ext_authz_duration"));

        EXPECT_TRUE(TestUtility::protoEqual(returned_dynamic_metadata, response.dynamic_metadata));
      }));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.denied")
                    .value());
  EXPECT_EQ("ext_authz_denied", decoder_filter_callbacks_.details());
}

// Verify that the filter emits metadata if the ext_authz client responds with an error and provides
// metadata.
TEST_F(HttpFilterTest, EmittingMetadataWhenError) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  )EOF");

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  // When the response check status is error, we skip emitting dynamic metadata.
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_, setDynamicMetadata(_, _));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;

  // Set the response metadata.
  initializeMetadata(response);
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.error")
                    .value());
}

// Test that when a connection awaiting a authorization response is canceled then the
// authorization call is closed.
TEST_P(HttpFilterTestParam, ResetDuringCall) {
  InSequence s;

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(*client_, cancel());
  filter_->onDestroy();
}

// Regression test for https://github.com/envoyproxy/envoy/pull/8436.
// Test that ext_authz filter is not in noop mode when cluster is not specified per route
// (this could be the case when route is configured with redirect or direct response action).
TEST_P(HttpFilterTestParam, NoCluster) {

  ON_CALL(decoder_filter_callbacks_, clusterInfo()).WillByDefault(Return(nullptr));

  // Place something in the context extensions on the route.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settingsroute;
  (*settingsroute.mutable_check_settings()->mutable_context_extensions())["key_route"] =
      "value_route";
  // Initialize the route's per filter config.
  FilterConfigPerRoute auth_per_route(settingsroute);
  ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(&auth_per_route));

  prepareCheck();

  // Save the check request from the check call.
  envoy::service::auth::v3::CheckRequest check_request;

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks&,
                     const envoy::service::auth::v3::CheckRequest& check_param, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { check_request = check_param; }));
  // Make sure that filter chain is not continued and the call has been invoked.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  // Engage the filter so that check is called.
  filter_->decodeHeaders(request_headers_, false);
}

// Check that config validation for per-route filter works as expected.
TEST_F(HttpFilterTest, PerRouteCheckSettingsConfigCheck) {
  // Set allow_partial_message to true and max_request_bytes to 5 on the per-route filter.
  envoy::extensions::filters::http::ext_authz::v3::BufferSettings buffer_settings;
  buffer_settings.set_max_request_bytes(5);        // Set the max_request_bytes value
  buffer_settings.set_allow_partial_message(true); // Set the allow_partial_message value
  // Set the per-route filter config.
  envoy::extensions::filters::http::ext_authz::v3::CheckSettings check_settings;
  check_settings.mutable_with_request_body()->CopyFrom(buffer_settings);
  check_settings.set_disable_request_body_buffering(true);
  // Initialize the route's per filter config.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settings;
  settings.mutable_check_settings()->CopyFrom(check_settings);

  // Expect an exception while initializing the route's per filter config.
  EXPECT_THROW_WITH_MESSAGE((FilterConfigPerRoute(settings)), EnvoyException,
                            "Invalid configuration for check_settings. Only one of "
                            "disable_request_body_buffering or with_request_body can be set.");
}

// Checks that the per-route filter can override the check_settings set on the main filter.
TEST_F(HttpFilterTest, PerRouteCheckSettingsWorks) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  )EOF");

  // Set allow_partial_message to true and max_request_bytes to 5 on the per-route filter.
  envoy::extensions::filters::http::ext_authz::v3::BufferSettings buffer_settings;
  buffer_settings.set_max_request_bytes(5);        // Set the max_request_bytes value
  buffer_settings.set_allow_partial_message(true); // Set the allow_partial_message value
  // Set the per-route filter config.
  envoy::extensions::filters::http::ext_authz::v3::CheckSettings check_settings;
  check_settings.mutable_with_request_body()->CopyFrom(buffer_settings);
  // Initialize the route's per filter config.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settings;
  settings.mutable_check_settings()->CopyFrom(check_settings);
  FilterConfigPerRoute auth_per_route(settings);

  ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(&auth_per_route));
  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  ON_CALL(decoder_filter_callbacks_, decodingBuffer()).WillByDefault(Return(&data_));
  ON_CALL(decoder_filter_callbacks_, addDecodedData(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& data, bool) { data_.add(data); }));
  EXPECT_CALL(decoder_filter_callbacks_, setDecoderBufferLimit(_)).Times(0);
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl buffer1("foo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer1, false));
  data_.add(buffer1.toString());

  Buffer::OwnedImpl buffer2("bar");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer2, true));
  data_.add(buffer2.toString());

  Buffer::OwnedImpl buffer3("barfoo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer3, true));
  data_.add(buffer3.toString());

  Buffer::OwnedImpl buffer4("more data after watermark is set is possible");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer4, true));

  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
}

// Checks that the per-route filter can override the check_settings set on the main filter.
TEST_F(HttpFilterTest, NullRouteSkipsCheck) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  stat_prefix: "ext_authz"
  )EOF");

  prepareCheck();

  // Set up a null route return value.
  ON_CALL(decoder_filter_callbacks_, route()).WillByDefault(Return(nullptr));

  // With null route, no authorization check should be performed.
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);

  // Call the filter directly.
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {"host", "example.com"}};

  // With null route, the filter should continue without an auth check.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
}

TEST_F(HttpFilterTest, PerRouteCheckSettingsOverrideWorks) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  with_request_body:
    max_request_bytes: 1
    allow_partial_message: false
  )EOF");

  // Set allow_partial_message to true and max_request_bytes to 10 on the per-route filter.
  envoy::extensions::filters::http::ext_authz::v3::BufferSettings buffer_settings;
  buffer_settings.set_max_request_bytes(10);       // Set the max_request_bytes value
  buffer_settings.set_allow_partial_message(true); // Set the allow_partial_message value
  // Set the per-route filter config.
  envoy::extensions::filters::http::ext_authz::v3::CheckSettings check_settings;
  check_settings.mutable_with_request_body()->CopyFrom(buffer_settings);
  // Initialize the route's per filter config.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settings;
  settings.mutable_check_settings()->CopyFrom(check_settings);
  FilterConfigPerRoute auth_per_route(settings);

  ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(&auth_per_route));
  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  ON_CALL(decoder_filter_callbacks_, decodingBuffer()).WillByDefault(Return(&data_));
  ON_CALL(decoder_filter_callbacks_, addDecodedData(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& data, bool) { data_.add(data); }));
  EXPECT_CALL(decoder_filter_callbacks_, setDecoderBufferLimit(_)).Times(0);
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl buffer1("foo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer1, false));
  data_.add(buffer1.toString());

  Buffer::OwnedImpl buffer2("bar");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer2, false));
  data_.add(buffer2.toString());

  Buffer::OwnedImpl buffer3("barfoo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer3, true));
  data_.add(buffer3.toString());

  Buffer::OwnedImpl buffer4("more data after watermark is set is possible");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer4, true));

  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
}

// Verify that request body buffering can be skipped per route.
TEST_P(HttpFilterTestParam, DisableRequestBodyBufferingOnRoute) {
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settings;
  std::unique_ptr<FilterConfigPerRoute> auth_per_route =
      std::make_unique<FilterConfigPerRoute>(settings);

  auto test_disable_request_body_buffering = [&](bool bypass) {
    initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  with_request_body:
    max_request_bytes: 1
    allow_partial_message: false
  )EOF");

    // Set bypass request body buffering for this route.
    settings.mutable_check_settings()->set_disable_request_body_buffering(bypass);
    // Initialize the route's per filter config.
    auth_per_route = std::make_unique<FilterConfigPerRoute>(settings);
    // Update the mock to return the new pointer.
    ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
        .WillByDefault(Return(auth_per_route.get()));
  };

  test_disable_request_body_buffering(false);
  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  // When request body buffering is not skipped, setDecoderBufferLimit is called.
  EXPECT_CALL(decoder_filter_callbacks_, setDecoderBufferLimit(_));
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data_, false));

  test_disable_request_body_buffering(true);
  // When request body buffering is skipped, setDecoderBufferLimit is not called.
  EXPECT_CALL(decoder_filter_callbacks_, setDecoderBufferLimit(_)).Times(0);
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
}

TEST_P(EmitFilterStateTest, OkResponse) {
  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  if (!std::get<0>(GetParam()) && std::get<1>(GetParam())) {
    response.grpc_status = Grpc::Status::WellKnownGrpcStatus::Ok;
    expected_output_.setGrpcStatus(Grpc::Status::WellKnownGrpcStatus::Ok);
  }

  test(response);
}

TEST_P(EmitFilterStateTest, Error) {
  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  if (!std::get<0>(GetParam()) && std::get<1>(GetParam())) {
    response.grpc_status = Grpc::Status::WellKnownGrpcStatus::Canceled;
    expected_output_.setGrpcStatus(Grpc::Status::WellKnownGrpcStatus::Canceled);
  }

  test(response);
}

TEST_P(EmitFilterStateTest, Denied) {
  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  if (!std::get<0>(GetParam()) && std::get<1>(GetParam())) {
    response.grpc_status = Grpc::Status::WellKnownGrpcStatus::PermissionDenied;
    expected_output_.setGrpcStatus(Grpc::Status::WellKnownGrpcStatus::PermissionDenied);
  }

  test(response);
}

// Tests that if for whatever reason the client's stream info is null, it doesn't result in a null
// pointer dereference or other issue.
TEST_P(EmitFilterStateTest, NullStreamInfo) {
  stream_info_ = nullptr;

  // Everything except latency will be empty.
  expected_output_.clearUpstreamHost();
  expected_output_.clearClusterInfo();
  expected_output_.clearBytesSent();
  expected_output_.clearBytesReceived();

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  if (!std::get<0>(GetParam()) && std::get<1>(GetParam())) {
    response.grpc_status = Grpc::Status::WellKnownGrpcStatus::Ok;
    expected_output_.setGrpcStatus(Grpc::Status::WellKnownGrpcStatus::Ok);
  }

  test(response);
}

// Tests that if any stream info fields are null, it doesn't result in a null pointer dereference or
// other issue.
TEST_P(EmitFilterStateTest, NullStreamInfoFields) {
  stream_info_->upstream_bytes_meter_ = nullptr;
  stream_info_->upstream_info_ = nullptr;
  stream_info_->upstream_cluster_info_ = nullptr;

  // Everything except latency will be empty.
  expected_output_.clearUpstreamHost();
  expected_output_.clearClusterInfo();
  expected_output_.clearBytesSent();
  expected_output_.clearBytesReceived();

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  if (!std::get<0>(GetParam()) && std::get<1>(GetParam())) {
    response.grpc_status = Grpc::Status::WellKnownGrpcStatus::Ok;
    expected_output_.setGrpcStatus(Grpc::Status::WellKnownGrpcStatus::Ok);
  }

  test(response);
}

// Tests that if upstream host is null, it doesn't result in a null pointer dereference or other
// issue.
TEST_P(EmitFilterStateTest, NullUpstreamHost) {
  auto upstream_info = std::make_shared<NiceMock<StreamInfo::MockUpstreamInfo>>();
  upstream_info->upstream_host_ = nullptr;
  stream_info_->upstream_info_ = upstream_info;

  expected_output_.clearUpstreamHost();

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  if (!std::get<0>(GetParam()) && std::get<1>(GetParam())) {
    response.grpc_status = Grpc::Status::WellKnownGrpcStatus::Ok;
    expected_output_.setGrpcStatus(Grpc::Status::WellKnownGrpcStatus::Ok);
  }

  test(response);
}

// hasData<ExtAuthzLoggingInfo>() will return false, setData() will succeed because this is
// mutable, thus getMutableData<ExtAuthzLoggingInfo> will not be nullptr and the naming collision
// is silently ignored.
TEST_P(EmitFilterStateTest, PreexistingFilterStateDifferentTypeMutable) {
  class TestObject : public Envoy::StreamInfo::FilterState::Object {};
  decoder_filter_callbacks_.stream_info_.filter_state_->setData(
      FilterConfigName,
      // This will not cast to ExtAuthzLoggingInfo, so when the filter tries to
      // getMutableData<ExtAuthzLoggingInfo>(...), it will return nullptr.
      std::make_shared<TestObject>(), Envoy::StreamInfo::FilterState::StateType::Mutable,
      Envoy::StreamInfo::FilterState::LifeSpan::Request);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  if (!std::get<0>(GetParam()) && std::get<1>(GetParam())) {
    response.grpc_status = Grpc::Status::WellKnownGrpcStatus::Ok;
    expected_output_.setGrpcStatus(Grpc::Status::WellKnownGrpcStatus::Ok);
  }

  test(response);
}

// hasData<ExtAuthzLoggingInfo>() will return true so the filter will not try to override the data.
TEST_P(EmitFilterStateTest, PreexistingFilterStateSameTypeMutable) {
  class TestObject : public Envoy::StreamInfo::FilterState::Object {};
  decoder_filter_callbacks_.stream_info_.filter_state_->setData(
      FilterConfigName,
      // This will not cast to ExtAuthzLoggingInfo, so when the filter tries to
      // getMutableData<ExtAuthzLoggingInfo>(...), it will return nullptr.
      std::make_shared<ExtAuthzLoggingInfo>(absl::nullopt),
      Envoy::StreamInfo::FilterState::StateType::Mutable,
      Envoy::StreamInfo::FilterState::LifeSpan::Request);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  if (!std::get<0>(GetParam()) && std::get<1>(GetParam())) {
    response.grpc_status = Grpc::Status::WellKnownGrpcStatus::Ok;
    expected_output_.setGrpcStatus(Grpc::Status::WellKnownGrpcStatus::Ok);
  }

  test(response);
}

TEST_P(ExtAuthzLoggingInfoTest, FieldTest) { test(); }

// Test per-route gRPC service override with null server context (fallback to default client)
TEST_P(HttpFilterTestParam, PerRouteGrpcServiceOverrideWithNullServerContext) {
  if (std::get<1>(GetParam())) {
    // Skip HTTP client test - per-route gRPC service only applies to gRPC clients
    return;
  }

  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute per_route_config;
  per_route_config.mutable_check_settings()
      ->mutable_grpc_service()
      ->mutable_envoy_grpc()
      ->set_cluster_name("per_route_ext_authz_cluster");

  std::unique_ptr<FilterConfigPerRoute> per_route_filter_config =
      std::make_unique<FilterConfigPerRoute>(per_route_config);

  // Set up route to return per-route config
  ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(per_route_filter_config.get()));

  prepareCheck();

  // Mock the default client check call (should fall back to default since server context is null)
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        Filters::Common::ExtAuthz::Response response{};
        response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

// Test per-route configuration merging with context extensions
TEST_P(HttpFilterTestParam, PerRouteConfigurationMergingWithContextExtensions) {
  if (std::get<1>(GetParam())) {
    // Skip HTTP client test - configuration merging applies to gRPC clients
    return;
  }

  // Create base configuration with context extensions
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute base_config;
  base_config.mutable_check_settings()->mutable_context_extensions()->insert(
      {"base_key", "base_value"});
  base_config.mutable_check_settings()->mutable_context_extensions()->insert(
      {"shared_key", "base_shared_value"});

  // Create more specific configuration with context extensions
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute specific_config;
  specific_config.mutable_check_settings()->mutable_context_extensions()->insert(
      {"specific_key", "specific_value"});
  specific_config.mutable_check_settings()->mutable_context_extensions()->insert(
      {"shared_key", "specific_shared_value"});

  // Test merging using the merge constructor
  FilterConfigPerRoute base_filter_config(base_config);
  FilterConfigPerRoute specific_filter_config(specific_config);
  FilterConfigPerRoute merged_config(base_filter_config, specific_filter_config);

  // Verify merged context extensions
  const auto& merged_extensions = merged_config.contextExtensions();
  EXPECT_EQ(merged_extensions.size(), 3);
  EXPECT_EQ(merged_extensions.at("base_key"), "base_value");
  EXPECT_EQ(merged_extensions.at("specific_key"), "specific_value");
  EXPECT_EQ(merged_extensions.at("shared_key"), "specific_shared_value"); // More specific wins
}

// Test per-route configuration merging with gRPC service override
TEST_P(HttpFilterTestParam, PerRouteConfigurationMergingWithGrpcServiceOverride) {
  if (std::get<1>(GetParam())) {
    // Skip HTTP client test - gRPC service override applies to gRPC clients
    return;
  }

  // Create base configuration without gRPC service
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute base_config;
  base_config.mutable_check_settings()->mutable_context_extensions()->insert(
      {"base_key", "base_value"});

  // Create more specific configuration with gRPC service
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute specific_config;
  specific_config.mutable_check_settings()
      ->mutable_grpc_service()
      ->mutable_envoy_grpc()
      ->set_cluster_name("specific_cluster");
  specific_config.mutable_check_settings()->mutable_context_extensions()->insert(
      {"specific_key", "specific_value"});

  // Test merging using the merge constructor
  FilterConfigPerRoute base_filter_config(base_config);
  FilterConfigPerRoute specific_filter_config(specific_config);
  FilterConfigPerRoute merged_config(base_filter_config, specific_filter_config);

  // Verify gRPC service override is from more specific config
  EXPECT_TRUE(merged_config.grpcService().has_value());
  EXPECT_EQ(merged_config.grpcService().value().envoy_grpc().cluster_name(), "specific_cluster");

  // Verify context extensions are merged
  const auto& merged_extensions = merged_config.contextExtensions();
  EXPECT_EQ(merged_extensions.size(), 2);
  EXPECT_EQ(merged_extensions.at("base_key"), "base_value");
  EXPECT_EQ(merged_extensions.at("specific_key"), "specific_value");
}

// Test per-route configuration merging with request body settings
TEST_P(HttpFilterTestParam, PerRouteConfigurationMergingWithRequestBodySettings) {
  if (std::get<1>(GetParam())) {
    // Skip HTTP client test - request body settings apply to gRPC clients
    return;
  }

  // Create base configuration with request body settings
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute base_config;
  base_config.mutable_check_settings()->mutable_with_request_body()->set_max_request_bytes(1000);
  base_config.mutable_check_settings()->mutable_with_request_body()->set_allow_partial_message(
      true);

  // Create more specific configuration with different request body settings
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute specific_config;
  specific_config.mutable_check_settings()->mutable_with_request_body()->set_max_request_bytes(
      2000);
  specific_config.mutable_check_settings()->mutable_with_request_body()->set_allow_partial_message(
      false);

  // Test merging using the merge constructor
  FilterConfigPerRoute base_filter_config(base_config);
  FilterConfigPerRoute specific_filter_config(specific_config);
  FilterConfigPerRoute merged_config(base_filter_config, specific_filter_config);

  // Verify request body settings are from more specific config
  const auto& merged_check_settings = merged_config.checkSettings();
  EXPECT_TRUE(merged_check_settings.has_with_request_body());
  EXPECT_EQ(merged_check_settings.with_request_body().max_request_bytes(), 2000);
  EXPECT_EQ(merged_check_settings.with_request_body().allow_partial_message(), false);
}

// Test per-route configuration merging with disable_request_body_buffering
TEST_P(HttpFilterTestParam, PerRouteConfigurationMergingWithDisableRequestBodyBuffering) {
  if (std::get<1>(GetParam())) {
    // Skip HTTP client test - disable request body buffering applies to gRPC clients
    return;
  }

  // Create base configuration without disable_request_body_buffering
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute base_config;
  base_config.mutable_check_settings()->mutable_context_extensions()->insert(
      {"base_key", "base_value"});

  // Create more specific configuration with disable_request_body_buffering
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute specific_config;
  specific_config.mutable_check_settings()->set_disable_request_body_buffering(true);

  // Test merging using the merge constructor
  FilterConfigPerRoute base_filter_config(base_config);
  FilterConfigPerRoute specific_filter_config(specific_config);
  FilterConfigPerRoute merged_config(base_filter_config, specific_filter_config);

  // Verify disable_request_body_buffering is from more specific config
  const auto& merged_check_settings = merged_config.checkSettings();
  EXPECT_TRUE(merged_check_settings.disable_request_body_buffering());
}

// Test per-route configuration merging with multiple levels
TEST_P(HttpFilterTestParam, PerRouteConfigurationMergingMultipleLevels) {
  if (std::get<1>(GetParam())) {
    // Skip HTTP client test - configuration merging applies to gRPC clients
    return;
  }

  // Create virtual host level configuration
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute vh_config;
  vh_config.mutable_check_settings()->mutable_context_extensions()->insert({"vh_key", "vh_value"});
  vh_config.mutable_check_settings()->mutable_context_extensions()->insert(
      {"shared_key", "vh_shared_value"});

  // Create route level configuration
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute route_config;
  route_config.mutable_check_settings()->mutable_context_extensions()->insert(
      {"route_key", "route_value"});
  route_config.mutable_check_settings()->mutable_context_extensions()->insert(
      {"shared_key", "route_shared_value"});
  route_config.mutable_check_settings()
      ->mutable_grpc_service()
      ->mutable_envoy_grpc()
      ->set_cluster_name("route_cluster");

  // Create weighted cluster level configuration
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute wc_config;
  wc_config.mutable_check_settings()->mutable_context_extensions()->insert({"wc_key", "wc_value"});
  wc_config.mutable_check_settings()->mutable_context_extensions()->insert(
      {"shared_key", "wc_shared_value"});

  // Test merging from least specific to most specific
  FilterConfigPerRoute vh_filter_config(vh_config);
  FilterConfigPerRoute route_filter_config(route_config);
  FilterConfigPerRoute wc_filter_config(wc_config);

  // First merge: vh + route
  FilterConfigPerRoute vh_route_merged(vh_filter_config, route_filter_config);

  // Second merge: (vh + route) + weighted cluster
  FilterConfigPerRoute final_merged(vh_route_merged, wc_filter_config);

  // Verify final merged context extensions
  const auto& merged_extensions = final_merged.contextExtensions();
  EXPECT_EQ(merged_extensions.size(), 4);
  EXPECT_EQ(merged_extensions.at("vh_key"), "vh_value");
  EXPECT_EQ(merged_extensions.at("route_key"), "route_value");
  EXPECT_EQ(merged_extensions.at("wc_key"), "wc_value");
  EXPECT_EQ(merged_extensions.at("shared_key"), "wc_shared_value"); // Most specific wins

  // Verify gRPC service override is NOT inherited from less specific levels.
  EXPECT_FALSE(final_merged.grpcService().has_value());
}

// Test per-route context extensions take precedence over check_settings context extensions.
TEST_P(HttpFilterTestParam, PerRouteContextExtensionsPrecedence) {
  if (std::get<1>(GetParam())) {
    // Skip HTTP client test as context extensions apply to gRPC clients.
    return;
  }

  // Create configuration with context extensions in both places.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute base_config;
  base_config.mutable_check_settings()->mutable_context_extensions()->insert(
      {"check_key", "check_value"});
  base_config.mutable_check_settings()->mutable_context_extensions()->insert(
      {"shared_key", "check_shared_value"});

  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute specific_config;
  specific_config.mutable_check_settings()->mutable_context_extensions()->insert(
      {"specific_check_key", "specific_check_value"});
  specific_config.mutable_check_settings()->mutable_context_extensions()->insert(
      {"shared_key", "specific_check_shared_value"});

  // Test merging using the merge constructor.
  FilterConfigPerRoute base_filter_config(base_config);
  FilterConfigPerRoute specific_filter_config(specific_config);
  FilterConfigPerRoute merged_config(base_filter_config, specific_filter_config);

  // Verify context extensions are properly merged.
  const auto& merged_extensions = merged_config.contextExtensions();
  EXPECT_EQ(merged_extensions.size(), 3);
  EXPECT_EQ(merged_extensions.at("check_key"), "check_value");
  EXPECT_EQ(merged_extensions.at("specific_check_key"), "specific_check_value");
  EXPECT_EQ(merged_extensions.at("shared_key"),
            "specific_check_shared_value"); // More specific wins
}

// Test per-route Google gRPC service configuration.
TEST_P(HttpFilterTestParam, PerRouteGoogleGrpcServiceConfiguration) {
  if (std::get<1>(GetParam())) {
    // Skip HTTP client test as per-route gRPC service only applies to gRPC clients.
    return;
  }

  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute per_route_config;
  per_route_config.mutable_check_settings()
      ->mutable_grpc_service()
      ->mutable_google_grpc()
      ->set_target_uri("https://ext-authz.googleapis.com");

  std::unique_ptr<FilterConfigPerRoute> per_route_filter_config =
      std::make_unique<FilterConfigPerRoute>(per_route_config);

  // Verify Google gRPC service is properly configured
  EXPECT_TRUE(per_route_filter_config->grpcService().has_value());
  EXPECT_TRUE(per_route_filter_config->grpcService().value().has_google_grpc());
  EXPECT_EQ(per_route_filter_config->grpcService().value().google_grpc().target_uri(),
            "https://ext-authz.googleapis.com");
}

// Test existing functionality still works with new logic.
TEST_P(HttpFilterTestParam, ExistingFunctionalityWithNewLogic) {
  // Test that the existing functionality still works with our new per-route merging logic.
  prepareCheck();

  // Mock the default client check call (no per-route config).
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        Filters::Common::ExtAuthz::Response response{};
        response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

// Test per-route configuration merging with empty configurations.
TEST_P(HttpFilterTestParam, PerRouteConfigurationMergingWithEmptyConfigurations) {
  if (std::get<1>(GetParam())) {
    // Skip HTTP client test as configuration merging applies to gRPC clients.
    return;
  }

  // Create empty base configuration.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute base_config;

  // Create empty specific configuration.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute specific_config;

  // Test merging using the merge constructor.
  FilterConfigPerRoute base_filter_config(base_config);
  FilterConfigPerRoute specific_filter_config(specific_config);
  FilterConfigPerRoute merged_config(base_filter_config, specific_filter_config);

  // Verify merged configuration has empty context extensions.
  const auto& merged_extensions = merged_config.contextExtensions();
  EXPECT_EQ(merged_extensions.size(), 0);

  // Verify no gRPC service override
  EXPECT_FALSE(merged_config.grpcService().has_value());
}

// Test per-route gRPC service configuration merging functionality.
TEST_P(HttpFilterTestParam, PerRouteGrpcServiceMergingWithBaseConfiguration) {
  if (std::get<1>(GetParam())) {
    // Skip HTTP client test as per-route gRPC service only applies to gRPC clients.
    return;
  }

  // Create base per-route configuration.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute base_config;
  (*base_config.mutable_check_settings()->mutable_context_extensions())["base"] = "value";
  FilterConfigPerRoute base_filter_config(base_config);

  // Create per-route configuration with gRPC service.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute per_route_config;
  per_route_config.mutable_check_settings()
      ->mutable_grpc_service()
      ->mutable_envoy_grpc()
      ->set_cluster_name("per_route_cluster");
  (*per_route_config.mutable_check_settings()->mutable_context_extensions())["route"] = "override";

  // Test merging constructor.
  FilterConfigPerRoute merged_config(base_filter_config, per_route_config);

  // Verify the merged configuration has the gRPC service from the per-route config.
  EXPECT_TRUE(merged_config.grpcService().has_value());
  EXPECT_TRUE(merged_config.grpcService().value().has_envoy_grpc());
  EXPECT_EQ(merged_config.grpcService().value().envoy_grpc().cluster_name(), "per_route_cluster");

  // Verify that context extensions are properly merged.
  const auto& merged_settings = merged_config.checkSettings();
  EXPECT_TRUE(merged_settings.context_extensions().contains("route"));
  EXPECT_EQ(merged_settings.context_extensions().at("route"), "override");
}

// Test focused integration test to verify per-route configuration is processed correctly.
TEST_P(HttpFilterTestParam, PerRouteConfigurationIntegrationTest) {
  if (std::get<1>(GetParam())) {
    // Skip HTTP client test - per-route gRPC service only applies to gRPC clients.
    return;
  }

  // This test covers the per-route configuration processing in initiateCall
  // which exercises the lines where getAllPerFilterConfig is called and processed.

  // Set up per-route configuration with gRPC service override
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute per_route_config;
  per_route_config.mutable_check_settings()
      ->mutable_grpc_service()
      ->mutable_envoy_grpc()
      ->set_cluster_name("per_route_cluster");

  // Add context extensions to test that path too.
  (*per_route_config.mutable_check_settings()->mutable_context_extensions())["test_key"] =
      "test_value";

  std::unique_ptr<FilterConfigPerRoute> per_route_filter_config =
      std::make_unique<FilterConfigPerRoute>(per_route_config);

  // Mock decoder callbacks to return per-route config.
  ON_CALL(decoder_filter_callbacks_, mostSpecificPerFilterConfig())
      .WillByDefault(Return(per_route_filter_config.get()));

  // Mock perFilterConfigs to return the per-route config vector.
  Router::RouteSpecificFilterConfigs per_route_configs;
  per_route_configs.push_back(per_route_filter_config.get());
  ON_CALL(decoder_filter_callbacks_, perFilterConfigs()).WillByDefault(Return(per_route_configs));

  // Set up basic request headers.
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "https"}, {"host", "example.com"}};

  prepareCheck();

  // Create a new filter with server context to enable per-route client creation.
  // We'll mock the gRPC client manager to return a controlled mock client.
  auto new_client = std::make_unique<Filters::Common::ExtAuthz::MockClient>();
  auto* new_client_ptr = new_client.get();
  auto new_filter = std::make_unique<Filter>(config_, std::move(new_client), factory_context_);
  new_filter->setDecoderFilterCallbacks(decoder_filter_callbacks_);

  // Mock the cluster manager to successfully create a per-route gRPC client
  // but use a mock raw gRPC client that we can control.
  ON_CALL(factory_context_, clusterManager()).WillByDefault(ReturnRef(cm_));
  auto mock_grpc_client_manager = std::make_shared<Grpc::MockAsyncClientManager>();
  ON_CALL(cm_, grpcAsyncClientManager()).WillByDefault(ReturnRef(*mock_grpc_client_manager));

  // Return a mock raw gRPC client for per-route client creation.
  auto mock_raw_grpc_client = std::make_shared<Grpc::MockAsyncClient>();
  EXPECT_CALL(*mock_grpc_client_manager, getOrCreateRawAsyncClientWithHashKey(_, _, true))
      .WillOnce(Return(absl::StatusOr<Grpc::RawAsyncClientSharedPtr>(mock_raw_grpc_client)));

  // Mock the sendRaw call with matcher-based validation for the gRPC authorization check.
  EXPECT_CALL(*mock_raw_grpc_client,
              sendRaw(_, _,
                      BufferString(AsCheckRequest(HasContextExtension("test_key", "test_value"))),
                      _, _, _))
      .WillOnce([&](absl::string_view /*service_full_name*/, absl::string_view /*method_name*/,
                    Buffer::InstancePtr&& /*request*/, Grpc::RawAsyncRequestCallbacks& callbacks,
                    Tracing::Span& parent_span,
                    const Http::AsyncClient::RequestOptions& /*options*/) -> Grpc::AsyncRequest* {
        // Create and send successful response.
        envoy::service::auth::v3::CheckResponse check_response;
        check_response.mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::Ok);
        check_response.mutable_ok_response();

        std::string serialized_response;
        check_response.SerializeToString(&serialized_response);
        auto response = std::make_unique<Buffer::OwnedImpl>(serialized_response);

        callbacks.onSuccessRaw(std::move(response), parent_span);
        return nullptr; // No async request handle needed for immediate response.
      });

  // Since we're using the per-route client, the default client should not be called.
  EXPECT_CALL(*new_client_ptr, check(_, _, _, _)).Times(0);

  // This exercises the per-route configuration processing logic which includes
  // the getAllPerFilterConfig call and per-route gRPC service detection.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, new_filter->decodeHeaders(headers, true));
}

// Test per-route gRPC client creation and usage.
TEST_P(HttpFilterTestParam, PerRouteGrpcClientCreationAndUsage) {
  if (std::get<1>(GetParam())) {
    // Skip HTTP client test as per-route gRPC service only applies to gRPC clients.
    return;
  }

  // Create per-route configuration with valid gRPC service.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute per_route_config;
  per_route_config.mutable_check_settings()
      ->mutable_grpc_service()
      ->mutable_envoy_grpc()
      ->set_cluster_name("per_route_ext_authz_cluster");

  // Add context extensions to test merging.
  (*per_route_config.mutable_check_settings()->mutable_context_extensions())["test_key"] =
      "test_value";

  std::unique_ptr<FilterConfigPerRoute> per_route_filter_config =
      std::make_unique<FilterConfigPerRoute>(per_route_config);

  // Set up route to return per-route config.
  ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(per_route_filter_config.get()));

  // Mock perFilterConfigs to return the per-route config vector which exercises
  // getAllPerFilterConfig.
  Router::RouteSpecificFilterConfigs per_route_configs;
  per_route_configs.push_back(per_route_filter_config.get());
  ON_CALL(decoder_filter_callbacks_, perFilterConfigs()).WillByDefault(Return(per_route_configs));

  prepareCheck();

  // Create a filter with server context for per-route gRPC client creation.
  auto new_client = std::make_unique<Filters::Common::ExtAuthz::MockClient>();
  auto* new_client_ptr = new_client.get();
  auto new_filter = std::make_unique<Filter>(config_, std::move(new_client), factory_context_);
  new_filter->setDecoderFilterCallbacks(decoder_filter_callbacks_);

  // Mock successful gRPC async client manager access.
  auto mock_grpc_client_manager = std::make_shared<Grpc::MockAsyncClientManager>();
  ON_CALL(factory_context_, clusterManager()).WillByDefault(ReturnRef(cm_));
  ON_CALL(cm_, grpcAsyncClientManager()).WillByDefault(ReturnRef(*mock_grpc_client_manager));

  // Mock successful raw gRPC client creation which exercises createPerRouteGrpcClient.
  auto mock_raw_grpc_client = std::make_shared<Grpc::MockAsyncClient>();
  EXPECT_CALL(*mock_grpc_client_manager, getOrCreateRawAsyncClientWithHashKey(_, _, true))
      .WillOnce(Return(absl::StatusOr<Grpc::RawAsyncClientSharedPtr>(mock_raw_grpc_client)));

  // Set up expectations for the sendRaw call that will be made by the GrpcClientImpl.
  EXPECT_CALL(*mock_raw_grpc_client, sendRaw(_, _, _, _, _, _))
      .WillOnce([](absl::string_view /*service_full_name*/, absl::string_view /*method_name*/,
                   Buffer::InstancePtr&& /*request*/, Grpc::RawAsyncRequestCallbacks& callbacks,
                   Tracing::Span& parent_span,
                   const Http::AsyncClient::RequestOptions& /*options*/) -> Grpc::AsyncRequest* {
        envoy::service::auth::v3::CheckResponse check_response;
        check_response.mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::Ok);
        check_response.mutable_ok_response();

        // Serialize the response to a buffer.
        std::string serialized_response;
        check_response.SerializeToString(&serialized_response);
        auto response = std::make_unique<Buffer::OwnedImpl>(serialized_response);

        callbacks.onSuccessRaw(std::move(response), parent_span);
        return nullptr; // No async request handle needed for immediate response.
      });

  // Since per-route gRPC client creation succeeds, the per-route client should be used
  // instead of the default client. We won't see a call to new_client_ptr.
  EXPECT_CALL(*new_client_ptr, check(_, _, _, _)).Times(0);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            new_filter->decodeHeaders(request_headers_, false));
}

// Test per-route HTTP service configuration parsing.
TEST_P(HttpFilterTestParam, PerRouteHttpServiceConfigurationParsing) {
  if (!std::get<1>(GetParam())) {
    // Skip gRPC client test as per-route HTTP service only applies to HTTP clients.
    return;
  }

  // Create per-route configuration with valid HTTP service.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute per_route_config;
  per_route_config.mutable_check_settings()->mutable_http_service()->mutable_server_uri()->set_uri(
      "https://per-route-ext-authz.example.com");
  per_route_config.mutable_check_settings()
      ->mutable_http_service()
      ->mutable_server_uri()
      ->set_cluster("per_route_http_cluster");
  per_route_config.mutable_check_settings()->mutable_http_service()->set_path_prefix(
      "/api/v2/auth");

  std::unique_ptr<FilterConfigPerRoute> per_route_filter_config =
      std::make_unique<FilterConfigPerRoute>(per_route_config);

  // Verify the per-route HTTP service configuration is correctly parsed
  EXPECT_TRUE(per_route_filter_config->httpService().has_value());
  EXPECT_FALSE(per_route_filter_config->grpcService().has_value());

  const auto& http_service = per_route_filter_config->httpService().value();
  EXPECT_EQ(http_service.server_uri().uri(), "https://per-route-ext-authz.example.com");
  EXPECT_EQ(http_service.server_uri().cluster(), "per_route_http_cluster");
  EXPECT_EQ(http_service.path_prefix(), "/api/v2/auth");
}

// Test error handling when server context is not available for per-route gRPC client.
TEST_P(HttpFilterTestParam, PerRouteGrpcClientCreationNoServerContext) {
  if (std::get<1>(GetParam())) {
    // Skip HTTP client test - per-route gRPC service only applies to gRPC clients.
    return;
  }

  // Create per-route configuration with gRPC service.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute per_route_config;
  per_route_config.mutable_check_settings()
      ->mutable_grpc_service()
      ->mutable_envoy_grpc()
      ->set_cluster_name("per_route_grpc_cluster");

  std::unique_ptr<FilterConfigPerRoute> per_route_filter_config =
      std::make_unique<FilterConfigPerRoute>(per_route_config);

  ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(per_route_filter_config.get()));

  Router::RouteSpecificFilterConfigs per_route_configs;
  per_route_configs.push_back(per_route_filter_config.get());
  ON_CALL(decoder_filter_callbacks_, perFilterConfigs()).WillByDefault(Return(per_route_configs));

  prepareCheck();

  // Create filter without server context. This should cause per-route client creation to fail.
  auto new_client = std::make_unique<Filters::Common::ExtAuthz::MockClient>();
  auto* new_client_ptr = new_client.get();
  auto new_filter = std::make_unique<Filter>(config_, std::move(new_client)); // No server context
  new_filter->setDecoderFilterCallbacks(decoder_filter_callbacks_);

  // Since per-route client creation fails (no server context), should fall back to default client.
  EXPECT_CALL(*new_client_ptr, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        // Verify this is using the default client.
        auto response = std::make_unique<Filters::Common::ExtAuthz::Response>();
        response->status = Filters::Common::ExtAuthz::CheckStatus::OK;
        callbacks.onComplete(std::move(response));
      }));

  Http::TestRequestHeaderMapImpl request_headers_{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {"host", "example.com"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            new_filter->decodeHeaders(request_headers_, false));
}

// Test error handling when server context is not available for per-route HTTP client.
TEST_P(HttpFilterTestParam, PerRouteHttpClientCreationNoServerContext) {
  if (!std::get<1>(GetParam())) {
    // Skip gRPC client test as per-route HTTP service only applies to HTTP clients.
    return;
  }

  // Create per-route configuration with HTTP service.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute per_route_config;
  per_route_config.mutable_check_settings()->mutable_http_service()->mutable_server_uri()->set_uri(
      "https://per-route-ext-authz.example.com");
  per_route_config.mutable_check_settings()
      ->mutable_http_service()
      ->mutable_server_uri()
      ->set_cluster("per_route_http_cluster");

  std::unique_ptr<FilterConfigPerRoute> per_route_filter_config =
      std::make_unique<FilterConfigPerRoute>(per_route_config);

  ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(per_route_filter_config.get()));

  Router::RouteSpecificFilterConfigs per_route_configs;
  per_route_configs.push_back(per_route_filter_config.get());
  ON_CALL(decoder_filter_callbacks_, perFilterConfigs()).WillByDefault(Return(per_route_configs));

  prepareCheck();

  // Create filter without server context.
  auto new_client = std::make_unique<Filters::Common::ExtAuthz::MockClient>();
  auto* new_client_ptr = new_client.get();
  auto new_filter = std::make_unique<Filter>(config_, std::move(new_client)); // No server context
  new_filter->setDecoderFilterCallbacks(decoder_filter_callbacks_);

  // Since per-route client creation fails, should fall back to default client.
  EXPECT_CALL(*new_client_ptr, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        auto response = std::make_unique<Filters::Common::ExtAuthz::Response>();
        response->status = Filters::Common::ExtAuthz::CheckStatus::OK;
        callbacks.onComplete(std::move(response));
      }));

  Http::TestRequestHeaderMapImpl request_headers_{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {"host", "example.com"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            new_filter->decodeHeaders(request_headers_, false));
}

// Test gRPC client error handling for per-route config.
TEST_F(HttpFilterTest, GrpcClientPerRouteError) {
  // Initialize with gRPC client configuration.
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  stat_prefix: "ext_authz"
  )EOF");

  prepareCheck();

  // Create per-route configuration with gRPC service override.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute per_route_config;
  auto* grpc_service = per_route_config.mutable_check_settings()->mutable_grpc_service();
  grpc_service->mutable_envoy_grpc()->set_cluster_name("nonexistent_cluster");

  FilterConfigPerRoute per_route_filter_config(per_route_config);

  // Set up route config to use the per-route configuration.
  ON_CALL(decoder_filter_callbacks_, mostSpecificPerFilterConfig())
      .WillByDefault(Return(&per_route_filter_config));

  // Since cluster doesn't exist, per-route client creation should fail
  // and we'll use the default client instead.
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        Filters::Common::ExtAuthz::Response response{};
        response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  // Verify filter processes the request with the default client.
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {"host", "example.com"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

// Test HTTP client with per-route configuration.
TEST_F(HttpFilterTest, HttpClientPerRouteOverride) {
  // Initialize with HTTP client configuration.
  initialize(R"EOF(
  http_service:
    server_uri:
      uri: "https://ext-authz.example.com"
      cluster: "ext_authz_server"
    path_prefix: "/api/v1/auth"
  failure_mode_allow: false
  stat_prefix: "ext_authz"
  )EOF");

  prepareCheck();

  // Create per-route configuration with HTTP service override.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute per_route_config;
  auto* http_service = per_route_config.mutable_check_settings()->mutable_http_service();
  http_service->mutable_server_uri()->set_uri("https://per-route-ext-authz.example.com");
  http_service->mutable_server_uri()->set_cluster("per_route_http_cluster");
  http_service->set_path_prefix("/api/v2/auth");

  FilterConfigPerRoute per_route_filter_config(per_route_config);

  // Set up route config to use the per-route configuration.
  ON_CALL(decoder_filter_callbacks_, mostSpecificPerFilterConfig())
      .WillByDefault(Return(&per_route_filter_config));

  // Set up a check expectation that will be satisfied by the default client.
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        Filters::Common::ExtAuthz::Response response{};
        response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  // Verify filter processes the request.
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {"host", "example.com"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

// Test invalid response header validation via response_headers_to_add.
TEST_F(InvalidMutationTest, InvalidResponseHeadersToAddName) {
  Filters::Common::ExtAuthz::Response r;
  r.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  r.response_headers_to_add = {{"invalid header name", "value"}};
  testResponse(r);
}

// Test invalid response header validation via response_headers_to_add value.
TEST_F(InvalidMutationTest, InvalidResponseHeadersToAddValue) {
  Filters::Common::ExtAuthz::Response r;
  r.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  r.response_headers_to_add = {{"valid-name", getInvalidValue()}};
  testResponse(r);
}

// Test per-route timeout configuration is correctly used in gRPC client creation.
TEST_P(HttpFilterTestParam, PerRouteGrpcClientTimeoutConfiguration) {
  if (std::get<1>(GetParam())) {
    // Skip HTTP client test as per-route gRPC service only applies to gRPC clients.
    return;
  }

  // Create per-route configuration with custom timeout.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute per_route_config;
  auto* grpc_service = per_route_config.mutable_check_settings()->mutable_grpc_service();
  grpc_service->mutable_envoy_grpc()->set_cluster_name("per_route_grpc_cluster");
  grpc_service->mutable_timeout()->set_seconds(30); // Custom 30s timeout

  std::unique_ptr<FilterConfigPerRoute> per_route_filter_config =
      std::make_unique<FilterConfigPerRoute>(per_route_config);

  ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(per_route_filter_config.get()));

  Router::RouteSpecificFilterConfigs per_route_configs;
  per_route_configs.push_back(per_route_filter_config.get());
  ON_CALL(decoder_filter_callbacks_, perFilterConfigs()).WillByDefault(Return(per_route_configs));

  prepareCheck();

  auto new_client = std::make_unique<Filters::Common::ExtAuthz::MockClient>();
  auto* new_client_ptr = new_client.get();
  auto new_filter = std::make_unique<Filter>(config_, std::move(new_client), factory_context_);
  new_filter->setDecoderFilterCallbacks(decoder_filter_callbacks_);

  // Mock gRPC client manager.
  auto mock_grpc_client_manager = std::make_shared<Grpc::MockAsyncClientManager>();
  ON_CALL(factory_context_, clusterManager()).WillByDefault(ReturnRef(cm_));
  ON_CALL(cm_, grpcAsyncClientManager()).WillByDefault(ReturnRef(*mock_grpc_client_manager));

  auto mock_raw_grpc_client = std::make_shared<Grpc::MockAsyncClient>();
  EXPECT_CALL(*mock_grpc_client_manager, getOrCreateRawAsyncClientWithHashKey(_, _, true))
      .WillOnce(Return(absl::StatusOr<Grpc::RawAsyncClientSharedPtr>(mock_raw_grpc_client)));

  // Mock the sendRaw call with matcher-based validation for timeout verification.
  EXPECT_CALL(*mock_raw_grpc_client, sendRaw(_, _, _, _, _, HasTimeout(30000)))
      .WillOnce([](absl::string_view /*service_full_name*/, absl::string_view /*method_name*/,
                   Buffer::InstancePtr&& /*request*/, Grpc::RawAsyncRequestCallbacks& callbacks,
                   Tracing::Span& parent_span,
                   const Http::AsyncClient::RequestOptions& /*options*/) -> Grpc::AsyncRequest* {
        envoy::service::auth::v3::CheckResponse check_response;
        check_response.mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::Ok);
        check_response.mutable_ok_response();

        std::string serialized_response;
        check_response.SerializeToString(&serialized_response);
        auto response = std::make_unique<Buffer::OwnedImpl>(serialized_response);

        callbacks.onSuccessRaw(std::move(response), parent_span);
        return nullptr;
      });

  EXPECT_CALL(*new_client_ptr, check(_, _, _, _)).Times(0);

  Http::TestRequestHeaderMapImpl request_headers_{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {"host", "example.com"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            new_filter->decodeHeaders(request_headers_, false));
}

// Verifies that the filter stops adding headers from `response_headers_to_add` once the header
// limit is reached.
TEST_F(HttpFilterTest, EncodeHeadersToAddExceedsLimit) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  enforce_response_header_limits: true
  )EOF");

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.response_headers_to_add.push_back({"key1", "value1"});
  response.response_headers_to_add.push_back({"key2", "value2"});
  response.response_headers_to_add.push_back({"key3", "value3"});

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));

  Http::TestResponseHeaderMapImpl response_headers(
      {{":status", "200"}, {"existing-header", "value"}}, /*max_headers_kb=*/99999,
      /*max_headers_count=*/3);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  EXPECT_EQ(response_headers.size(), 3);
  EXPECT_TRUE(response_headers.has("key1"));
  EXPECT_FALSE(response_headers.has("key2"));
  EXPECT_FALSE(response_headers.has("key3"));
  EXPECT_EQ(1U, config_->stats().omitted_response_headers_.value());
}

TEST_F(HttpFilterTest, EncodeHeadersToAddExceedsLimitDisabled) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  )EOF");

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.response_headers_to_add.push_back({"key1", "value1"});
  response.response_headers_to_add.push_back({"key2", "value2"});
  response.response_headers_to_add.push_back({"key3", "value3"});

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));

  Http::TestResponseHeaderMapImpl response_headers(
      {{":status", "200"}, {"existing-header", "value"}}, /*max_headers_kb=*/99999,
      /*max_headers_count=*/3);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  EXPECT_EQ(response_headers.size(), 5);
  EXPECT_TRUE(response_headers.has("key1"));
  EXPECT_TRUE(response_headers.has("key2"));
  EXPECT_TRUE(response_headers.has("key3"));
  EXPECT_EQ(0U, config_->stats().omitted_response_headers_.value());
}

// Verifies that the filter stops adding new headers from `response_headers_to_set` once the header
// limit is reached, but still allows overwriting existing ones.
TEST_F(HttpFilterTest, EncodeHeadersToSetExceedsLimit) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  enforce_response_header_limits: true
  )EOF");

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.response_headers_to_set.push_back({"existing-header-to-overwrite", "new-value"});
  response.response_headers_to_set.push_back({"new-header-to-add", "value"});
  response.response_headers_to_set.push_back({"another-new-header", "value"});

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));

  Http::TestResponseHeaderMapImpl response_headers(
      {{":status", "200"}, {"existing-header-to-overwrite", "old-value"}}, /*max_headers_kb=*/99999,
      /*max_headers_count=*/2);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  EXPECT_EQ(response_headers.size(), 2);
  EXPECT_EQ(response_headers.get(LowerCaseString("existing-header-to-overwrite"))[0]
                ->value()
                .getStringView(),
            "new-value");
  EXPECT_FALSE(response_headers.has("new-header-to-add"));
  EXPECT_FALSE(response_headers.has("another-new-header"));
  EXPECT_EQ(1U, config_->stats().omitted_response_headers_.value());
}

TEST_F(HttpFilterTest, EncodeHeadersToSetExceedsLimitDisabled) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  enforce_response_header_limits: false
  )EOF");

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.response_headers_to_set.push_back({"existing-header-to-overwrite", "new-value"});
  response.response_headers_to_set.push_back({"new-header-to-add", "value"});
  response.response_headers_to_set.push_back({"another-new-header", "value"});

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));

  Http::TestResponseHeaderMapImpl response_headers(
      {{":status", "200"}, {"existing-header-to-overwrite", "old-value"}}, /*max_headers_kb=*/99999,
      /*max_headers_count=*/2);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  EXPECT_EQ(response_headers.size(), 4);
  EXPECT_EQ(response_headers.get(LowerCaseString("existing-header-to-overwrite"))[0]
                ->value()
                .getStringView(),
            "new-value");
  EXPECT_TRUE(response_headers.has("new-header-to-add"));
  EXPECT_TRUE(response_headers.has("another-new-header"));
  EXPECT_EQ(0U, config_->stats().omitted_response_headers_.value());
}

// Verifies that the filter stops adding headers from `response_headers_to_add_if_absent` once the
// header limit is reached.
TEST_F(HttpFilterTest, EncodeHeadersToAddIfAbsentExceedsLimit) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  enforce_response_header_limits: true
  )EOF");

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.response_headers_to_add_if_absent.push_back({"key1", "value1"});
  response.response_headers_to_add_if_absent.push_back({"key2", "value2"});
  response.response_headers_to_add_if_absent.push_back({"existing-header", "value"});

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));

  Http::TestResponseHeaderMapImpl response_headers(
      {{":status", "200"}, {"existing-header", "value"}}, /*max_headers_kb=*/99999,
      /*max_headers_count=*/3);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  EXPECT_EQ(response_headers.size(), 3);
  EXPECT_TRUE(response_headers.has("key1"));
  EXPECT_FALSE(response_headers.has("key2"));
  EXPECT_EQ(1U, config_->stats().omitted_response_headers_.value());
}

TEST_F(HttpFilterTest, EncodeHeadersToAddIfAbsentExceedsLimitDisabled) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  enforce_response_header_limits: false
  )EOF");

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.response_headers_to_add_if_absent.push_back({"key1", "value1"});
  response.response_headers_to_add_if_absent.push_back({"key2", "value2"});
  response.response_headers_to_add_if_absent.push_back({"existing-header", "value"});

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));

  Http::TestResponseHeaderMapImpl response_headers(
      {{":status", "200"}, {"existing-header", "value"}}, /*max_headers_kb=*/99999,
      /*max_headers_count=*/3);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  EXPECT_EQ(response_headers.size(), 4);
  EXPECT_TRUE(response_headers.has("key1"));
  EXPECT_TRUE(response_headers.has("key2"));
  EXPECT_EQ(0U, config_->stats().omitted_response_headers_.value());
}

// Verifies that `response_headers_to_overwrite_if_exists` can still modify headers even when the
// limit is reached, as it does not add new headers.
TEST_F(HttpFilterTest, EncodeHeadersToOverwriteIfExistsNotLimited) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  enforce_response_header_limits: true
  )EOF");

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.response_headers_to_overwrite_if_exists.push_back(
      {"existing-header-to-overwrite", "new-value"});
  response.response_headers_to_overwrite_if_exists.push_back({"non-existing-header", "value"});

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));

  Http::TestResponseHeaderMapImpl response_headers({{":status", "200"},
                                                    {"existing-header", "value"},
                                                    {"existing-header-to-overwrite", "old-value"}},
                                                   /*max_headers_kb=*/99999,
                                                   /*max_headers_count=*/3);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  EXPECT_EQ(response_headers.size(), 3);
  EXPECT_EQ(response_headers.get(LowerCaseString("existing-header-to-overwrite"))[0]
                ->value()
                .getStringView(),
            "new-value");
  EXPECT_FALSE(response_headers.has("non-existing-header"));
  EXPECT_EQ(0U, config_->stats().omitted_response_headers_.value());
}

// Verifies that the filter stops adding headers to a local reply (when the ext_authz sends a
// Denied response) once the header limit is reached.
TEST_F(HttpFilterTest, DeniedResponseLocalReplyExceedsLimit) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  enforce_response_header_limits: true
  )EOF");

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Unauthorized;
  response.headers_to_set.push_back({"key1", "value1"});
  response.headers_to_set.push_back({"key2", "value2"});
  response.headers_to_set.push_back({"key3", "value3"});

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillOnce(
          Invoke([&](Http::Code, absl::string_view,
                     std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                     const absl::optional<Grpc::Status::GrpcStatus>, absl::string_view) -> void {
            Http::TestResponseHeaderMapImpl response_headers({}, 99999, /*max_headers_count=*/2);
            if (modify_headers) {
              modify_headers(response_headers);
            }
            EXPECT_EQ(response_headers.size(), 2);
            EXPECT_TRUE(response_headers.has("key1"));
            EXPECT_TRUE(response_headers.has("key2"));
            EXPECT_FALSE(response_headers.has("key3"));
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, true));
  EXPECT_EQ(1U, config_->stats().omitted_response_headers_.value());
}

TEST_F(HttpFilterTest, DeniedResponseLocalReplyExceedsLimitDisabled) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  enforce_response_header_limits: false
  )EOF");

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Unauthorized;
  response.headers_to_set.push_back({"key1", "value1"});
  response.headers_to_set.push_back({"key2", "value2"});
  response.headers_to_set.push_back({"key3", "value3"});

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillOnce(
          Invoke([&](Http::Code, absl::string_view,
                     std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                     const absl::optional<Grpc::Status::GrpcStatus>, absl::string_view) -> void {
            Http::TestResponseHeaderMapImpl response_headers({}, 99999, /*max_headers_count=*/2);
            if (modify_headers) {
              modify_headers(response_headers);
            }
            EXPECT_EQ(response_headers.size(), 3);
            EXPECT_TRUE(response_headers.has("key1"));
            EXPECT_TRUE(response_headers.has("key2"));
            EXPECT_TRUE(response_headers.has("key3"));
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, true));
  EXPECT_EQ(0U, config_->stats().omitted_response_headers_.value());
}

// Test that set-cookie headers from successful authorization are properly added to the client
// response using allowed_client_headers_on_success.
TEST_F(HttpFilterTest, SetCookieHeaderOnSuccessfulAuthorization) {
  InSequence s;

  initialize(R"EOF(
  http_service:
    server_uri:
      uri: "ext_authz:9000"
      cluster: "ext_authz_server"
      timeout: 0.5s
    authorization_response:
      allowed_client_headers_on_success:
        patterns:
        - exact: "set-cookie"
          ignore_case: true
        - exact: "x-custom-header"
          ignore_case: true
  )EOF");

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.response_headers_to_add = {{"set-cookie", "session=abc123"},
                                      {"x-custom-header", "custom-value"}};
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// Test that set-cookie headers from denied authorization are properly added to the client response
// using allowed_client_headers.
TEST_F(HttpFilterTest, SetCookieHeaderOnDeniedAuthorization) {
  InSequence s;

  initialize(R"EOF(
  http_service:
    server_uri:
      uri: "ext_authz:9000"
      cluster: "ext_authz_server"
      timeout: 0.5s
    authorization_response:
      allowed_client_headers:
        patterns:
        - exact: "set-cookie"
          ignore_case: true
        - exact: "www-authenticate"
          ignore_case: true
  )EOF");

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::UnauthorizedExternalService));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  // Verify headers are present in the local reply (including extra headers added by sendLocalReply)
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, false))
      .WillOnce(Invoke([&](const Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ(headers.getStatusValue(), "403");
        EXPECT_EQ(headers.get(Http::LowerCaseString("set-cookie"))[0]->value().getStringView(),
                  "error=invalid");
        EXPECT_EQ(
            headers.get(Http::LowerCaseString("www-authenticate"))[0]->value().getStringView(),
            "Bearer realm=\"example\"");
      }));
  EXPECT_CALL(decoder_filter_callbacks_, encodeData(_, true))
      .WillOnce(Invoke(
          [&](Buffer::Instance& data, bool) { EXPECT_EQ(data.toString(), "Unauthorized"); }));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Forbidden;
  response.body = "Unauthorized";
  response.headers_to_set = {{"set-cookie", "error=invalid"},
                             {"www-authenticate", "Bearer realm=\"example\""}};
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  EXPECT_EQ(1U, config_->stats().denied_.value());
}

// Test that multiple set-cookie headers from successful authorization are properly propagated.
TEST_F(HttpFilterTest, MultipleSetCookieHeadersOnSuccess) {
  InSequence s;

  initialize(R"EOF(
  http_service:
    server_uri:
      uri: "ext_authz:9000"
      cluster: "ext_authz_server"
      timeout: 0.5s
    authorization_response:
      allowed_client_headers_on_success:
        patterns:
        - exact: "set-cookie"
          ignore_case: true
  )EOF");

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.response_headers_to_add = {{"set-cookie", "session=abc123"},
                                      {"set-cookie", "user=john"}};
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  EXPECT_EQ(1U, config_->stats().ok_.value());
}

} // namespace
} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
