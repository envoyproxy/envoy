// Changing the default behavior of ext_proc is generally not allowed. While you may add tests, you
// generally should not change or remove existing tests.

#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "test/extensions/filters/http/ext_proc/logging_test_filter.pb.h"
#include "test/extensions/filters/http/ext_proc/logging_test_filter.pb.validate.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

// A test filter that retrieve the logging info on encodeComplete.
class LoggingTestFilter : public Http::PassThroughFilter {
public:
  LoggingTestFilter(const std::string& logging_id, const std::string& cluster_name,
                    bool check_received_bytes,
                    const std::string& response_code_details = "via_upstream",
                    uint32_t expected_response_body_call_count = 0)
      : logging_id_(logging_id), expected_cluster_name_(cluster_name),
        check_received_bytes_(check_received_bytes), expected_rcd_(response_code_details),
        expected_response_body_call_count_(expected_response_body_call_count) {}
  void encodeComplete() override {
    ASSERT(decoder_callbacks_ != nullptr);
    const Envoy::StreamInfo::FilterStateSharedPtr& filter_state =
        decoder_callbacks_->streamInfo().filterState();
    const ExtProcLoggingInfo* ext_proc_logging_info =
        filter_state->getDataReadOnly<ExtProcLoggingInfo>(logging_id_);
    if (ext_proc_logging_info != nullptr) {
      EXPECT_NE(ext_proc_logging_info->bytesSent(), 0);
      if (check_received_bytes_) {
        EXPECT_NE(ext_proc_logging_info->bytesReceived(), 0);
      }
      ASSERT_TRUE(ext_proc_logging_info->upstreamHost() != nullptr);
      EXPECT_EQ(ext_proc_logging_info->upstreamHost()->cluster().name(), expected_cluster_name_);
      EXPECT_EQ(ext_proc_logging_info->clusterInfo()->name(), expected_cluster_name_);
      EXPECT_EQ(ext_proc_logging_info->httpResponseCodeDetails(), expected_rcd_);
      if (expected_response_body_call_count_ > 0) {
        const auto& outbound_calls =
            ext_proc_logging_info->grpcCalls(envoy::config::core::v3::TrafficDirection::OUTBOUND);
        ASSERT_NE(outbound_calls.body_stats_, nullptr);
        EXPECT_EQ(outbound_calls.body_stats_->call_count_, expected_response_body_call_count_);
      }
    }
  }

private:
  std::string logging_id_;
  std::string expected_cluster_name_;
  const bool check_received_bytes_;
  std::string expected_rcd_;
  const uint32_t expected_response_body_call_count_;
};

class LoggingTestFilterFactory : public Extensions::HttpFilters::Common::FactoryBase<
                                     test::integration::filters::LoggingTestFilterConfig> {
public:
  LoggingTestFilterFactory() : FactoryBase("logging-test-filter") {};

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::integration::filters::LoggingTestFilterConfig& proto_config, const std::string&,
      Server::Configuration::FactoryContext&) override {
    return [=](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<LoggingTestFilter>(
          proto_config.logging_id(), proto_config.upstream_cluster_name(),
          proto_config.check_received_bytes(), proto_config.http_rcd(),
          proto_config.expected_response_body_call_count()));
    };
  }
};

// Perform static registration
static Registry::RegisterFactory<LoggingTestFilterFactory,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
