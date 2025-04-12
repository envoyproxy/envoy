#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/ratelimit/ratelimit.h"

#include "test/extensions/filters/http/ratelimit/logging_test_filter.pb.h"
#include "test/extensions/filters/http/ratelimit/logging_test_filter.pb.validate.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {
namespace Test {

// A test filter that verifies the RateLimit logging info on encodeComplete.
class LoggingTestFilter : public Http::PassThroughFilter {
public:
  LoggingTestFilter(const std::string& logging_id, const std::string& cluster_name,
                    bool expect_stats, bool expect_envoy_grpc_specific_stats,
                    bool expect_response_bytes)
      : logging_id_(logging_id), expected_cluster_name_(cluster_name), expect_stats_(expect_stats),
        expect_envoy_grpc_specific_stats_(expect_envoy_grpc_specific_stats),
        expect_response_bytes_(expect_response_bytes) {}

  void encodeComplete() override {
    ASSERT(decoder_callbacks_ != nullptr);
    const Envoy::StreamInfo::FilterStateSharedPtr& filter_state =
        decoder_callbacks_->streamInfo().filterState();

    ASSERT_EQ(filter_state->hasData<RateLimitLoggingInfo>(logging_id_), expect_stats_);
    if (!expect_stats_) {
      return;
    }

    const RateLimitLoggingInfo* ratelimit_logging_info =
        filter_state->getDataReadOnly<RateLimitLoggingInfo>(logging_id_);

    ASSERT_TRUE(ratelimit_logging_info->latency().has_value());
    EXPECT_GT(ratelimit_logging_info->latency().value().count(), 0);

    if (expect_envoy_grpc_specific_stats_) {
      // If the stats exist, a request should always have been sent.
      EXPECT_TRUE(ratelimit_logging_info->bytesSent().has_value());
      EXPECT_GT(ratelimit_logging_info->bytesSent().value(), 0);

      // A response may or may not have been received depending on the test.
      if (expect_response_bytes_) {
        EXPECT_TRUE(ratelimit_logging_info->bytesReceived().has_value());
        EXPECT_GT(ratelimit_logging_info->bytesReceived().value(), 0);
      } else {
        if (ratelimit_logging_info->bytesReceived().has_value()) {
          EXPECT_EQ(ratelimit_logging_info->bytesReceived().value(), 0);
        }
      }

      ASSERT_NE(ratelimit_logging_info->clusterInfo(), nullptr);
      EXPECT_EQ(ratelimit_logging_info->clusterInfo()->name(), expected_cluster_name_);
      ASSERT_NE(ratelimit_logging_info->upstreamHost(), nullptr);
    }
  }

private:
  const std::string logging_id_;
  const std::string expected_cluster_name_;
  const bool expect_stats_;
  const bool expect_envoy_grpc_specific_stats_;
  const bool expect_response_bytes_;
};

class LoggingTestFilterFactory : public Extensions::HttpFilters::Common::FactoryBase<
                                     test::integration::filters::LoggingTestFilterConfig> {
public:
  LoggingTestFilterFactory() : FactoryBase("test.ratelimit.logging_filter") {};

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::integration::filters::LoggingTestFilterConfig& proto_config, const std::string&,
      Server::Configuration::FactoryContext&) override {
    return [=](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<LoggingTestFilter>(
          proto_config.logging_id(), proto_config.expected_cluster_name(),
          proto_config.expect_stats(), proto_config.expect_envoy_grpc_specific_stats(),
          proto_config.expect_response_bytes()));
    };
  }
};

// Perform static registration
static Registry::RegisterFactory<LoggingTestFilterFactory,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Test
} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
