#include "envoy/extensions/filters/network/mtls_failure_response/v3/mtls_failure_response.pb.h"
#include "envoy/extensions/filters/network/mtls_failure_response/v3/mtls_failure_response.pb.validate.h"
#include "envoy/type/v3/token_bucket.pb.h"

#include "source/extensions/filters/network/mtls_failure_response/config.h"
#include "source/extensions/filters/network/mtls_failure_response/filter.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MtlsFailureResponse {

namespace {

class MtlsFailureResponseTestBase : public testing::Test, public Event::TestUsingSimulatedTime {
public:
  void initialize(const std::string& filter_yaml) {
    envoy::extensions::filters::network::mtls_failure_response::v3::MtlsFailureResponse
        proto_config;
    TestUtility::loadFromYamlAndValidate(filter_yaml, proto_config);

    Network::FilterFactoryCb cb =
        factory.createFilterFactoryFromProto(proto_config, context_).value();
    Network::MockConnection connection;

    EXPECT_CALL(connection, addReadFilter(_))
        .WillOnce(Invoke([&](Network::ReadFilterSharedPtr filter) {
          filter_ = std::dynamic_pointer_cast<MtlsFailureResponseFilter>(filter);
        }));

    cb(connection);
    proto_config_ = proto_config;
  }

  MtlsFailureResponseConfigFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  envoy::extensions::filters::network::mtls_failure_response::v3::MtlsFailureResponse proto_config_;
  std::shared_ptr<MtlsFailureResponseFilter> filter_;
};

class MtlsFailureResponseFilterTest : public MtlsFailureResponseTestBase {
public:
  struct ActiveFilter {
    ActiveFilter(std::shared_ptr<MtlsFailureResponseFilter> filter) : filter_(std::move(filter)) {
      filter_->initializeReadFilterCallbacks(read_filter_callbacks_);
    }

    NiceMock<Network::MockReadFilterCallbacks> read_filter_callbacks_;
    std::shared_ptr<MtlsFailureResponseFilter> filter_;
  };
};

TEST_F(MtlsFailureResponseFilterTest, ValidateConfigCloseConnectionOnFailure) {
  const std::string filter_yaml = R"EOF(
  validation_mode: PRESENTED
  failure_mode: CLOSE_CONNECTION
  )EOF";

  initialize(filter_yaml);

  EXPECT_EQ(proto_config_.validation_mode(),
            envoy::extensions::filters::network::mtls_failure_response::v3::MtlsFailureResponse::
                PRESENTED);
  EXPECT_EQ(proto_config_.failure_mode(),
            envoy::extensions::filters::network::mtls_failure_response::v3::MtlsFailureResponse::
                CLOSE_CONNECTION);
  EXPECT_FALSE(proto_config_.has_token_bucket());
}

TEST_F(MtlsFailureResponseFilterTest, ValidateConfigKeepConnectionOpenWithTokens) {
  const std::string filter_yaml = R"EOF(
  validation_mode: PRESENTED
  failure_mode: KEEP_CONNECTION_OPEN
  token_bucket:
    max_tokens: 10
    tokens_per_fill: 1
    fill_interval: 5s
  )EOF";

  initialize(filter_yaml);

  EXPECT_EQ(proto_config_.validation_mode(),
            envoy::extensions::filters::network::mtls_failure_response::v3::MtlsFailureResponse::
                PRESENTED);
  EXPECT_EQ(proto_config_.failure_mode(),
            envoy::extensions::filters::network::mtls_failure_response::v3::MtlsFailureResponse::
                KEEP_CONNECTION_OPEN);
  EXPECT_TRUE(proto_config_.has_token_bucket());
  EXPECT_EQ(proto_config_.token_bucket().max_tokens(), 10);
  EXPECT_EQ(static_cast<::uint32_t>(proto_config_.token_bucket().tokens_per_fill().value()),
            static_cast<::uint32_t>(1));
  EXPECT_EQ(proto_config_.token_bucket().fill_interval().seconds(), 5);
}

TEST_F(MtlsFailureResponseFilterTest, ValidateConfigKeepConnectionOpenWithoutTokens) {
  const std::string filter_yaml = R"EOF(
  validation_mode: VALIDATED
  failure_mode: KEEP_CONNECTION_OPEN
  )EOF";

  initialize(filter_yaml);

  EXPECT_EQ(proto_config_.validation_mode(),
            envoy::extensions::filters::network::mtls_failure_response::v3::MtlsFailureResponse::
                VALIDATED);
  EXPECT_EQ(proto_config_.failure_mode(),
            envoy::extensions::filters::network::mtls_failure_response::v3::MtlsFailureResponse::
                KEEP_CONNECTION_OPEN);
  EXPECT_FALSE(proto_config_.has_token_bucket());
}

TEST_F(MtlsFailureResponseFilterTest, InvalidConfig) {
  const std::string filter_yaml = R"EOF(
  validation_mode: INVALID
  failure_mode: CLOSE_CONNECTION
  )EOF";

  EXPECT_THROW_WITH_REGEX(initialize(filter_yaml), EnvoyException, "INVALID_ARGUMENT");
}

} // namespace

} // namespace MtlsFailureResponse
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
