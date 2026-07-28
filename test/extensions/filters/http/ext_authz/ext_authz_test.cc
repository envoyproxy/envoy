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
#include "test/extensions/filters/http/ext_authz/http_filter_test_base.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/printers.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {
namespace {

using StatusHelpers::HasStatus;

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

// Matcher to verify RequestOptions has no timeout set.
MATCHER(HasNoTimeout, "") {
  if (arg.timeout.has_value()) {
    *result_listener << "expected no timeout, but timeout is " << arg.timeout->count() << "ms";
    return false;
  }
  return true;
}

using CreateFilterConfigFunc = envoy::extensions::filters::http::ext_authz::v3::ExtAuthz();

// Builds a per-route config, asserting that construction succeeds. Used by tests that only exercise
// valid per-route configurations.
std::unique_ptr<FilterConfigPerRoute> makePerRouteConfig(
    const envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute& config) {
  absl::Status creation_status = absl::OkStatus();
  auto per_route = std::make_unique<FilterConfigPerRoute>(config, creation_status);
  EXPECT_OK(creation_status);
  return per_route;
}

class HttpFilterTestParam
    : public HttpFilterTestBase<
          testing::TestWithParam<std::tuple<bool /*failure_mode_allow*/, bool /*http_client*/>>> {
public:
  void SetUp() override {
    initialize(getFilterConfig(std::get<0>(GetParam()), std::get<1>(GetParam())));
  }

  static std::string paramsToString(const testing::TestParamInfo<std::tuple<bool, bool>>& info) {
    return absl::StrCat(std::get<0>(info.param) ? "FailOpen" : "FailClosed", "_",
                        std::get<1>(info.param) ? "HttpClient" : "GrpcClient");
  }
};

INSTANTIATE_TEST_SUITE_P(ParameterizedFilterConfig, HttpFilterTestParam,
                         testing::Combine(testing::Bool(), testing::Bool()),
                         HttpFilterTestParam::paramsToString);

class ExtAuthzLoggingInfoTest
    : public testing::TestWithParam<
          std::tuple<std::string /* field_name */, std::optional<uint64_t> /* value */>> {
public:
  ExtAuthzLoggingInfoTest() : logging_info_({}) {}

  void SetUp() override {
    std::string fieldName = std::get<0>(GetParam());
    std::optional<uint64_t> optional = std::get<1>(GetParam());
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
    std::optional<uint64_t> optional = std::get<1>(GetParam());
    if (optional.has_value()) {
      EXPECT_THAT(logging_info_.getField(std::get<0>(GetParam())),
                  testing::VariantWith<int64_t>(optional.value()));
    } else {
      EXPECT_THAT(logging_info_.getField(std::get<0>(GetParam())),
                  testing::VariantWith<absl::monostate>(absl::monostate{}));
    }
  }

  static std::string paramsToString(
      const testing::TestParamInfo<std::tuple<std::string, std::optional<uint64_t>>>& info) {
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
                     testing::Values(std::optional<uint64_t>{}, std::optional<uint64_t>{0},
                                     std::optional<uint64_t>{1})),
    ExtAuthzLoggingInfoTest::paramsToString);

INSTANTIATE_TEST_SUITE_P(ExtAuthzLoggingInfoTestInvalid, ExtAuthzLoggingInfoTest,
                         testing::Values(std::make_tuple("wrong_property_name",
                                                         std::optional<uint64_t>{})),
                         ExtAuthzLoggingInfoTest::paramsToString);

class EmitFilterStateTest
    : public HttpFilterTestBase<testing::TestWithParam<
          std::tuple<bool /*http_client*/, bool /*emit_stats*/, bool /*emit_filter_metadata*/>>> {
public:
  EmitFilterStateTest() : expected_output_(filterMetadata()) {}

  std::optional<Envoy::Protobuf::Struct> filterMetadata() const {
    if (!std::get<2>(GetParam())) {
      return std::nullopt;
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
  paramsToString(const testing::TestParamInfo<std::tuple<bool, bool, bool>>& info) {
    return absl::StrCat(std::get<0>(info.param) ? "HttpClient" : "GrpcClient", "_",
                        std::get<1>(info.param) ? "EmitStats" : "DoNotEmitStats", "_",
                        std::get<2>(info.param) ? "EmitFilterMetadata" : "DoNotEmitFilterMetadata");
  }

  // Convenience function to save rewriting the same boilerplate & checks for all these tests.
  void test(const Filters::Common::ExtAuthz::Response& response) {
    InSequence s;

    prepareCheck();

    auto& filter_state = decoder_filter_callbacks_.streamInfo().filterState();
    std::optional<ExtAuthzLoggingInfo> preexisting_data_copy =
        filter_state->hasData<ExtAuthzLoggingInfo>(FilterConfigName)
            ? std::make_optional(
                  *filter_state->getDataReadOnly<ExtAuthzLoggingInfo>(FilterConfigName))
            : std::nullopt;

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
                         EmitFilterStateTest::paramsToString);

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
        emit_filter_state_stats: true
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
    // NOLINTNEXTLINE(modernize-return-braced-init-list)
    return std::string(reinterpret_cast<const char*>(invalid_value_bytes_));
  }
};

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

TEST_F(InvalidMutationTest, InvalidRequestHeadersSet) {
  Filters::Common::ExtAuthz::Response response;
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_set = {{InvalidMutationTest::invalid_key_, "bar"}};
  testResponse(response);
  auto& filter_state = decoder_filter_callbacks_.streamInfo().filterState();
  ASSERT_TRUE(filter_state->hasData<ExtAuthzLoggingInfo>(FilterConfigName));
  auto actual = filter_state->getDataReadOnly<ExtAuthzLoggingInfo>(FilterConfigName);
  EXPECT_EQ(actual->requestProcessingEffect(),
            Filters::Common::ProcessingEffect::Effect::InvalidMutationRejected);
}

TEST_F(InvalidMutationTest, InvalidRequestHeadersAppend) {
  Filters::Common::ExtAuthz::Response response;
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_append = {{InvalidMutationTest::invalid_key_, "bar"}};
  testResponse(response);
  auto& filter_state = decoder_filter_callbacks_.streamInfo().filterState();
  ASSERT_TRUE(filter_state->hasData<ExtAuthzLoggingInfo>(FilterConfigName));
  auto actual = filter_state->getDataReadOnly<ExtAuthzLoggingInfo>(FilterConfigName);
  EXPECT_EQ(actual->requestProcessingEffect(),
            Filters::Common::ProcessingEffect::Effect::InvalidMutationRejected);
}

TEST_F(InvalidMutationTest, InvalidRequestHeadersAdd) {
  Filters::Common::ExtAuthz::Response response;
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_add = {{"foo", InvalidMutationTest::getInvalidValue()}};
  testResponse(response);
  auto& filter_state = decoder_filter_callbacks_.streamInfo().filterState();
  ASSERT_TRUE(filter_state->hasData<ExtAuthzLoggingInfo>(FilterConfigName));
  auto actual = filter_state->getDataReadOnly<ExtAuthzLoggingInfo>(FilterConfigName);
  EXPECT_EQ(actual->requestProcessingEffect(),
            Filters::Common::ProcessingEffect::Effect::InvalidMutationRejected);
}

TEST_F(InvalidMutationTest, InvalidRequestQueryParams) {
  Filters::Common::ExtAuthz::Response response;
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.query_parameters_to_set = {{"f o o", "bar"}};
  testResponse(response);
  auto& filter_state = decoder_filter_callbacks_.streamInfo().filterState();
  ASSERT_TRUE(filter_state->hasData<ExtAuthzLoggingInfo>(FilterConfigName));
  auto actual = filter_state->getDataReadOnly<ExtAuthzLoggingInfo>(FilterConfigName);
  EXPECT_EQ(actual->requestProcessingEffect(),
            Filters::Common::ProcessingEffect::Effect::InvalidMutationRejected);
}

struct DecoderHeaderMutationRulesTestOpts {
  std::optional<envoy::config::common::mutation_rules::v3::HeaderMutationRules> rules;
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

// Test for ExtAuthzLoggingInfo clear methods.
TEST(ExtAuthzLoggingInfoTest, ClearMethods) {
  ExtAuthzLoggingInfo logging_info(std::nullopt);
  logging_info.setLatency(std::chrono::microseconds(100));
  logging_info.setBytesSent(10);
  logging_info.setBytesReceived(20);
  logging_info.setClusterInfo(std::make_shared<NiceMock<Upstream::MockClusterInfo>>());
  logging_info.setUpstreamHost(std::make_shared<NiceMock<Upstream::MockHostDescription>>());

  EXPECT_TRUE(logging_info.latency().has_value());
  EXPECT_TRUE(logging_info.bytesSent().has_value());
  EXPECT_TRUE(logging_info.bytesReceived().has_value());
  EXPECT_NE(nullptr, logging_info.clusterInfo());
  EXPECT_NE(nullptr, logging_info.upstreamHost());

  logging_info.clearLatency();
  logging_info.clearBytesSent();
  logging_info.clearBytesReceived();
  logging_info.clearClusterInfo();
  logging_info.clearUpstreamHost();

  EXPECT_FALSE(logging_info.latency().has_value());
  EXPECT_FALSE(logging_info.bytesSent().has_value());
  EXPECT_FALSE(logging_info.bytesReceived().has_value());
  EXPECT_EQ(nullptr, logging_info.clusterInfo());
  EXPECT_EQ(nullptr, logging_info.upstreamHost());
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
  FilterConfigPerRoute auth_per_vhost = makePerRoute(settingsvhost);

  // Place something in the context extensions on the route.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settingsroute;
  (*settingsroute.mutable_check_settings()->mutable_context_extensions())["key_route"] =
      "value_route";
  // Initialize the route's per filter config.
  FilterConfigPerRoute auth_per_route = makePerRoute(settingsroute);

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
  std::unique_ptr<FilterConfigPerRoute> auth_per_route = makePerRouteConfig(settings);

  prepareCheck();

  auto test_disable = [&](bool disabled) {
    initialize("");
    // Set disabled
    settings.set_disabled(disabled);
    // Initialize the route's per filter config.
    auth_per_route = makePerRouteConfig(settings);
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
  std::unique_ptr<FilterConfigPerRoute> auth_per_route = makePerRouteConfig(settings);

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
    auth_per_route = makePerRouteConfig(settings);
    // Update the mock to return the new pointer.
    ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
        .WillByDefault(Return(auth_per_route.get()));
  };

  test_disable(false);
  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  // When filter is not disabled, setBufferLimit is called.
  EXPECT_CALL(decoder_filter_callbacks_, setBufferLimit(_));
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data_, false));

  // To test that disabling the filter works.
  test_disable(true);
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  // Make sure that setBufferLimit is skipped.
  EXPECT_CALL(decoder_filter_callbacks_, setBufferLimit(_)).Times(0);
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
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config;
  TestUtility::loadFromYaml(R"EOF(
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
  )EOF",
                            proto_config);
  absl::Status creation_status = absl::OkStatus();
  FilterConfig filter_config(proto_config, *stats_store_.rootScope(), "ext_authz_prefix",
                             factory_context_, creation_status);
  EXPECT_THAT(creation_status, HasStatus(absl::StatusCode::kInvalidArgument,
                                         "Invalid duplicate configuration for allowed_headers."));
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

// Test that onDestroy cancels the correct client (per-route vs default).
TEST_P(HttpFilterTestParam, OnDestroyCancelsCorrectClient) {
  if (std::get<1>(GetParam())) {
    // Skip HTTP client test as per-route gRPC service only applies to gRPC clients.
    // For HTTP clients, we test the default client cancellation path.
    return;
  }

  InSequence s;

  prepareCheck();

  // Create per-route configuration with gRPC service override.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute per_route_config;
  per_route_config.mutable_check_settings()
      ->mutable_grpc_service()
      ->mutable_envoy_grpc()
      ->set_cluster_name("per_route_ext_authz_cluster");

  std::unique_ptr<FilterConfigPerRoute> per_route_filter_config =
      makePerRouteConfig(per_route_config);

  // Set up route to return per-route config.
  ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(per_route_filter_config.get()));

  // Mock perFilterConfigs to return the per-route config vector.
  Router::RouteSpecificFilterConfigs per_route_configs;
  per_route_configs.push_back(per_route_filter_config.get());
  ON_CALL(decoder_filter_callbacks_, perFilterConfigs()).WillByDefault(Return(per_route_configs));

  // Create a new filter with server context for per-route gRPC client creation.
  auto default_client = std::make_unique<Filters::Common::ExtAuthz::MockClient>();
  auto* default_client_ptr = default_client.get();
  auto test_filter = std::make_unique<Filter>(config_, std::move(default_client), factory_context_);
  test_filter->setDecoderFilterCallbacks(decoder_filter_callbacks_);
  test_filter->setEncoderFilterCallbacks(encoder_filter_callbacks_);

  // Mock successful gRPC async client manager access.
  auto mock_grpc_client_manager = std::make_shared<Grpc::MockAsyncClientManager>();
  ON_CALL(factory_context_, clusterManager()).WillByDefault(ReturnRef(cm_));
  ON_CALL(cm_, grpcAsyncClientManager()).WillByDefault(ReturnRef(*mock_grpc_client_manager));

  // Mock successful raw gRPC client creation.
  auto mock_raw_grpc_client = std::make_shared<Grpc::MockAsyncClient>();
  auto mock_async_request = std::make_unique<Grpc::MockAsyncRequest>();
  auto* mock_async_request_ptr = mock_async_request.get();

  EXPECT_CALL(*mock_grpc_client_manager, getOrCreateRawAsyncClientWithHashKey(_, _, true))
      .WillOnce(Return(absl::StatusOr<Grpc::RawAsyncClientSharedPtr>(mock_raw_grpc_client)));

  // Set up expectations for the sendRaw call that will be made by the GrpcClientImpl.
  EXPECT_CALL(*mock_raw_grpc_client, sendRaw(_, _, _, _, _, _))
      .WillOnce(Return(mock_async_request_ptr));

  // Set expectations on default client BEFORE decodeHeaders() because the default client
  // is destroyed when replaced by the per-route client during decodeHeaders().
  // gMock will verify these expectations when the mock object is destroyed.
  EXPECT_CALL(*default_client_ptr, check(_, _, _, _)).Times(0);
  EXPECT_CALL(*default_client_ptr, cancel()).Times(0);

  // Start the authorization check - this will create the per-route client and replace
  // the default client. The default client is destroyed here.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            test_filter->decodeHeaders(request_headers_, false));

  // Verify that the per-route client's async request is cancelled.
  EXPECT_CALL(*mock_async_request_ptr, cancel());
  test_filter->onDestroy();
}

// Test that onDestroy cancels the default client when no per-route client is used.
TEST_P(HttpFilterTestParam, OnDestroyCancelsDefaultClient) {
  InSequence s;

  prepareCheck();

  // No per-route configuration - default client will be used.
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  // Verify that when onDestroy is called, the default client's cancel IS called.
  EXPECT_CALL(*client_, cancel());
  filter_->onDestroy();
}

// Verify that the default client is NOT destroyed when a per-route client override
// is active.
TEST_P(HttpFilterTestParam, OnDestroyPreservesDefaultClientWithPerRouteOverride) {
  if (std::get<1>(GetParam())) {
    return;
  }

  prepareCheck();

  // Create per-route configuration with gRPC service override.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute per_route_config;
  per_route_config.mutable_check_settings()
      ->mutable_grpc_service()
      ->mutable_envoy_grpc()
      ->set_cluster_name("per_route_ext_authz_cluster");

  std::unique_ptr<FilterConfigPerRoute> per_route_filter_config =
      makePerRouteConfig(per_route_config);

  ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(per_route_filter_config.get()));

  Router::RouteSpecificFilterConfigs per_route_configs;
  per_route_configs.push_back(per_route_filter_config.get());
  ON_CALL(decoder_filter_callbacks_, perFilterConfigs()).WillByDefault(Return(per_route_configs));

  // Create a new filter with an explicitly tracked default client.
  auto default_client = std::make_unique<Filters::Common::ExtAuthz::MockClient>();
  auto* default_client_ptr = default_client.get();

  // we'll verify after decodeHeaders() to prove the client is still alive.
  EXPECT_CALL(*default_client_ptr, streamInfo()).WillRepeatedly(Return(nullptr));

  auto test_filter = std::make_unique<Filter>(config_, std::move(default_client), factory_context_);
  test_filter->setDecoderFilterCallbacks(decoder_filter_callbacks_);
  test_filter->setEncoderFilterCallbacks(encoder_filter_callbacks_);

  // Mock successful gRPC async client manager access.
  auto mock_grpc_client_manager = std::make_shared<Grpc::MockAsyncClientManager>();
  ON_CALL(factory_context_, clusterManager()).WillByDefault(ReturnRef(cm_));
  ON_CALL(cm_, grpcAsyncClientManager()).WillByDefault(ReturnRef(*mock_grpc_client_manager));

  auto mock_raw_grpc_client = std::make_shared<Grpc::MockAsyncClient>();
  auto mock_async_request = std::make_unique<Grpc::MockAsyncRequest>();
  auto* mock_async_request_ptr = mock_async_request.get();

  EXPECT_CALL(*mock_grpc_client_manager, getOrCreateRawAsyncClientWithHashKey(_, _, true))
      .WillOnce(Return(absl::StatusOr<Grpc::RawAsyncClientSharedPtr>(mock_raw_grpc_client)));

  EXPECT_CALL(*mock_raw_grpc_client, sendRaw(_, _, _, _, _, _))
      .WillOnce(Return(mock_async_request_ptr));

  // The default client's check should NOT be called.
  EXPECT_CALL(*default_client_ptr, check(_, _, _, _)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            test_filter->decodeHeaders(request_headers_, false));

  EXPECT_EQ(nullptr, default_client_ptr->streamInfo());

  EXPECT_CALL(*default_client_ptr, cancel()).Times(0);
  EXPECT_CALL(*mock_async_request_ptr, cancel());
  test_filter->onDestroy();
}

// Regression test for https://github.com/envoyproxy/envoy/pull/8436.
// Test that ext_authz filter is not in noop mode when cluster is not specified per route
// (this could be the case when route is configured with redirect or direct response action).
TEST_P(HttpFilterTestParam, NoCluster) {
  decoder_filter_callbacks_.cluster_info_ = nullptr;

  // Place something in the context extensions on the route.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settingsroute;
  (*settingsroute.mutable_check_settings()->mutable_context_extensions())["key_route"] =
      "value_route";
  // Initialize the route's per filter config.
  FilterConfigPerRoute auth_per_route = makePerRoute(settingsroute);
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

// Verify that request body buffering can be skipped per route.
TEST_P(HttpFilterTestParam, DisableRequestBodyBufferingOnRoute) {
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settings;
  std::unique_ptr<FilterConfigPerRoute> auth_per_route = makePerRouteConfig(settings);

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
    auth_per_route = makePerRouteConfig(settings);
    // Update the mock to return the new pointer.
    ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
        .WillByDefault(Return(auth_per_route.get()));
  };

  test_disable_request_body_buffering(false);
  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  // When request body buffering is not skipped, setBufferLimit is called.
  EXPECT_CALL(decoder_filter_callbacks_, setBufferLimit(_));
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data_, false));

  test_disable_request_body_buffering(true);
  // When request body buffering is skipped, setBufferLimit is not called.
  EXPECT_CALL(decoder_filter_callbacks_, setBufferLimit(_)).Times(0);
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
      std::make_shared<TestObject>(), Envoy::StreamInfo::FilterState::LifeSpan::Request);

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
      std::make_shared<ExtAuthzLoggingInfo>(std::nullopt),
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
      makePerRouteConfig(per_route_config);

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
  FilterConfigPerRoute base_filter_config = makePerRoute(base_config);
  FilterConfigPerRoute specific_filter_config = makePerRoute(specific_config);
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
  FilterConfigPerRoute base_filter_config = makePerRoute(base_config);
  FilterConfigPerRoute specific_filter_config = makePerRoute(specific_config);
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
  FilterConfigPerRoute base_filter_config = makePerRoute(base_config);
  FilterConfigPerRoute specific_filter_config = makePerRoute(specific_config);
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
  FilterConfigPerRoute base_filter_config = makePerRoute(base_config);
  FilterConfigPerRoute specific_filter_config = makePerRoute(specific_config);
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
  FilterConfigPerRoute vh_filter_config = makePerRoute(vh_config);
  FilterConfigPerRoute route_filter_config = makePerRoute(route_config);
  FilterConfigPerRoute wc_filter_config = makePerRoute(wc_config);

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
  FilterConfigPerRoute base_filter_config = makePerRoute(base_config);
  FilterConfigPerRoute specific_filter_config = makePerRoute(specific_config);
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
      makePerRouteConfig(per_route_config);

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
  FilterConfigPerRoute base_filter_config = makePerRoute(base_config);
  FilterConfigPerRoute specific_filter_config = makePerRoute(specific_config);
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
  FilterConfigPerRoute base_filter_config = makePerRoute(base_config);

  // Create per-route configuration with gRPC service.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute per_route_config;
  per_route_config.mutable_check_settings()
      ->mutable_grpc_service()
      ->mutable_envoy_grpc()
      ->set_cluster_name("per_route_cluster");
  (*per_route_config.mutable_check_settings()->mutable_context_extensions())["route"] = "override";

  // Test merging constructor.
  FilterConfigPerRoute merged_config(base_filter_config, makePerRoute(per_route_config));

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
      makePerRouteConfig(per_route_config);

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
  EXPECT_CALL(
      *mock_raw_grpc_client,
      sendRaw(_, _, BufferPtrString(AsCheckRequest(HasContextExtension("test_key", "test_value"))),
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
        std::ignore = check_response.SerializeToString(&serialized_response);
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
      makePerRouteConfig(per_route_config);

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
        std::ignore = check_response.SerializeToString(&serialized_response);
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
      makePerRouteConfig(per_route_config);

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
      makePerRouteConfig(per_route_config);

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
      makePerRouteConfig(per_route_config);

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
// Tests both non-zero timeout (30s -> 30000ms) and zero timeout (0s -> no timeout/infinite).
TEST_P(HttpFilterTestParam, PerRouteGrpcClientTimeoutConfiguration) {
  if (std::get<1>(GetParam())) {
    // Skip HTTP client test as per-route gRPC service only applies to gRPC clients.
    return;
  }

  // Test both non-zero and zero timeout cases.
  // timeout_seconds=30 -> expect 30000ms timeout
  // timeout_seconds=0  -> expect no timeout (infinite)
  for (const auto& [timeout_seconds, expect_timeout_ms] :
       std::vector<std::pair<int64_t, std::optional<int64_t>>>{{30, 30000}, {0, std::nullopt}}) {
    SCOPED_TRACE(absl::StrCat("timeout_seconds=", timeout_seconds));

    // Create per-route configuration with custom timeout.
    envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute per_route_config;
    auto* grpc_service = per_route_config.mutable_check_settings()->mutable_grpc_service();
    grpc_service->mutable_envoy_grpc()->set_cluster_name("per_route_grpc_cluster");
    grpc_service->mutable_timeout()->set_seconds(timeout_seconds);

    std::unique_ptr<FilterConfigPerRoute> per_route_filter_config =
        makePerRouteConfig(per_route_config);

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

    // Mock the sendRaw call with appropriate timeout matcher.
    if (expect_timeout_ms.has_value()) {
      EXPECT_CALL(*mock_raw_grpc_client, sendRaw(_, _, _, _, _, HasTimeout(*expect_timeout_ms)))
          .WillOnce([](absl::string_view, absl::string_view, Buffer::InstancePtr&&,
                       Grpc::RawAsyncRequestCallbacks& callbacks, Tracing::Span& parent_span,
                       const Http::AsyncClient::RequestOptions&) -> Grpc::AsyncRequest* {
            envoy::service::auth::v3::CheckResponse check_response;
            check_response.mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::Ok);
            check_response.mutable_ok_response();

            std::string serialized_response;
            std::ignore = check_response.SerializeToString(&serialized_response);
            auto response = std::make_unique<Buffer::OwnedImpl>(serialized_response);

            callbacks.onSuccessRaw(std::move(response), parent_span);
            return nullptr;
          });
    } else {
      // Zero timeout means no timeout (infinite).
      EXPECT_CALL(*mock_raw_grpc_client, sendRaw(_, _, _, _, _, HasNoTimeout()))
          .WillOnce([](absl::string_view, absl::string_view, Buffer::InstancePtr&&,
                       Grpc::RawAsyncRequestCallbacks& callbacks, Tracing::Span& parent_span,
                       const Http::AsyncClient::RequestOptions&) -> Grpc::AsyncRequest* {
            envoy::service::auth::v3::CheckResponse check_response;
            check_response.mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::Ok);
            check_response.mutable_ok_response();

            std::string serialized_response;
            std::ignore = check_response.SerializeToString(&serialized_response);
            auto response = std::make_unique<Buffer::OwnedImpl>(serialized_response);

            callbacks.onSuccessRaw(std::move(response), parent_span);
            return nullptr;
          });
    }

    EXPECT_CALL(*new_client_ptr, check(_, _, _, _)).Times(0);

    Http::TestRequestHeaderMapImpl request_headers_{
        {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {"host", "example.com"}};

    EXPECT_EQ(Http::FilterHeadersStatus::Continue,
              new_filter->decodeHeaders(request_headers_, false));
  }
}

class ResponseHeaderLimitTest : public HttpFilterTest {
public:
  ResponseHeaderLimitTest() = default;

  void runTest(Http::ResponseHeaderMap& response_headers,
               Filters::Common::ExtAuthz::Response response) {
    InSequence s;

    initialize(R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: "ext_authz_server"
    enforce_response_header_limits: true
    )EOF");

    prepareCheck();

    EXPECT_CALL(*client_, check(_, _, _, _))
        .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                             const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                             const StreamInfo::StreamInfo&) -> void {
          callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
        }));

    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));

    EXPECT_CALL(encoder_filter_callbacks_.stream_info_,
                setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService));
    EXPECT_CALL(encoder_filter_callbacks_,
                sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
    EXPECT_CALL(encoder_filter_callbacks_, continueEncoding()).Times(0);

    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->encodeHeaders(response_headers, false));

    EXPECT_EQ(1U, config_->stats().response_header_limits_reached_.value());
  }
};

// Verifies that the filter stops adding headers from `response_headers_to_add` once the header
// limit is reached.
TEST_F(ResponseHeaderLimitTest, EncodeHeadersToAddExceedsCountLimit) {
  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.response_headers_to_add.push_back({"key1", "value1"});
  response.response_headers_to_add.push_back({"key2", "value2"});
  response.response_headers_to_add.push_back({"key3", "value3"});

  Http::TestResponseHeaderMapImpl response_headers(
      {{":status", "200"}, {"existing-header", "value"}}, /*max_headers_kb=*/99999,
      /*max_headers_count=*/3);

  runTest(response_headers, response);
}

TEST_F(ResponseHeaderLimitTest, EncodeHeadersToAddExceedsSizeLimit) {
  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.response_headers_to_add.push_back({"key1", "value1"});
  response.response_headers_to_add.push_back({"key2", "value2"});
  response.response_headers_to_add.push_back({"key3", std::string(9999, 'a')});

  Http::TestResponseHeaderMapImpl response_headers(
      {{":status", "200"}, {"existing-header", "value"}}, /*max_headers_kb=*/1,
      /*max_headers_count=*/9999);

  runTest(response_headers, response);
}

// Verifies that the filter stops adding new headers from `response_headers_to_set` once the header
// limit is reached, but still allows overwriting existing ones.
TEST_F(ResponseHeaderLimitTest, EncodeHeadersToSetExceedsCountLimit) {
  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.response_headers_to_set.push_back({"existing-header-to-overwrite", "new-value"});
  response.response_headers_to_set.push_back({"new-header-to-add", "value"});
  response.response_headers_to_set.push_back({"another-new-header", "value"});

  Http::TestResponseHeaderMapImpl response_headers(
      {{":status", "200"}, {"existing-header-to-overwrite", "old-value"}}, /*max_headers_kb=*/99999,
      /*max_headers_count=*/2);

  runTest(response_headers, response);
}

TEST_F(ResponseHeaderLimitTest, EncodeHeadersToSetExceedsSizeLimit) {
  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.response_headers_to_set.push_back(
      {"existing-header-to-overwrite", std::string(9999, 'a')});
  response.response_headers_to_set.push_back({"new-header-to-add", "value"});
  response.response_headers_to_set.push_back({"another-new-header", "value"});

  Http::TestResponseHeaderMapImpl response_headers(
      {{":status", "200"}, {"existing-header-to-overwrite", "old-value"}}, /*max_headers_kb=*/1,
      /*max_headers_count=*/9999);

  runTest(response_headers, response);
}

// Verifies that the filter stops adding headers from `response_headers_to_add_if_absent` once the
// header limit is reached.
TEST_F(ResponseHeaderLimitTest, EncodeHeadersToAddIfAbsentExceedsCountLimit) {
  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.response_headers_to_add_if_absent.push_back({"key1", "value1"});
  response.response_headers_to_add_if_absent.push_back({"key2", "value2"});
  response.response_headers_to_add_if_absent.push_back({"existing-header", "value"});

  Http::TestResponseHeaderMapImpl response_headers(
      {{":status", "200"}, {"existing-header", "value"}}, /*max_headers_kb=*/99999,
      /*max_headers_count=*/3);

  runTest(response_headers, response);
}

TEST_F(ResponseHeaderLimitTest, EncodeHeadersToAddIfAbsentExceedsSizeLimit) {
  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.response_headers_to_add_if_absent.push_back({"foo", std::string(9999, 'a')});

  Http::TestResponseHeaderMapImpl response_headers(
      {{":status", "200"}, {"existing-header", "value"}}, /*max_headers_kb=*/1,
      /*max_headers_count=*/9999);

  runTest(response_headers, response);
}

TEST_F(ResponseHeaderLimitTest, EncodeHeadersToOverwriteIfExistsExceedsSizeLimit) {
  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.response_headers_to_overwrite_if_exists.push_back(
      {"existing-header-to-overwrite", std::string(9999, 'a')});
  response.response_headers_to_overwrite_if_exists.push_back({"non-existing-header", "value"});

  Http::TestResponseHeaderMapImpl response_headers({{":status", "200"},
                                                    {"existing-header", "value"},
                                                    {"existing-header-to-overwrite", "old-value"}},
                                                   /*max_headers_kb=*/1,
                                                   /*max_headers_count=*/9999);

  runTest(response_headers, response);
}

} // namespace
} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
