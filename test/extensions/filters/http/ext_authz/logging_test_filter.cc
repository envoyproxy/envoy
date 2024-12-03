#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/ext_authz/ext_authz.h"

#include "test/extensions/filters/http/ext_authz/logging_test_filter.pb.h"
#include "test/extensions/filters/http/ext_authz/logging_test_filter.pb.validate.h"
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
                    bool expect_stats, bool expect_envoy_grpc_specific_stats,
                    bool expect_response_bytes, const ProtobufWkt::Struct& filter_metadata)
      : logging_id_(logging_id), expected_cluster_name_(cluster_name), expect_stats_(expect_stats),
        expect_envoy_grpc_specific_stats_(expect_envoy_grpc_specific_stats),
        expect_response_bytes_(expect_response_bytes), filter_metadata_(filter_metadata) {}
  void encodeComplete() override {
    ASSERT(decoder_callbacks_ != nullptr);
    const Envoy::StreamInfo::FilterStateSharedPtr& filter_state =
        decoder_callbacks_->streamInfo().filterState();

    ASSERT_EQ(filter_state->hasData<ExtAuthz::ExtAuthzLoggingInfo>(logging_id_), expect_stats_);
    if (!expect_stats_) {
      return;
    }

    const ExtAuthz::ExtAuthzLoggingInfo* ext_authz_logging_info =
        filter_state->getDataReadOnly<ExtAuthz::ExtAuthzLoggingInfo>(logging_id_);

    ASSERT_EQ(ext_authz_logging_info->filterMetadata().has_value(), filter_metadata_.has_value());
    if (filter_metadata_.has_value()) {
      // TODO: Is there a better way to do deep comparison of protos?
      EXPECT_EQ(ext_authz_logging_info->filterMetadata()->DebugString(),
                filter_metadata_->DebugString());
    }

    ASSERT_TRUE(ext_authz_logging_info->latency().has_value());
    EXPECT_GT(ext_authz_logging_info->latency()->count(), 0);
    if (expect_envoy_grpc_specific_stats_) {
      // If the stats exist a request should always have been sent.
      EXPECT_GT(ext_authz_logging_info->bytesSent(), 0);

      // A response may or may not have been received depending on the test.
      if (expect_response_bytes_) {
        EXPECT_GT(ext_authz_logging_info->bytesReceived(), 0);
      } else {
        EXPECT_EQ(ext_authz_logging_info->bytesReceived(), 0);
      }
      ASSERT_NE(ext_authz_logging_info->upstreamHost(), nullptr);
      EXPECT_EQ(ext_authz_logging_info->upstreamHost()->cluster().name(), expected_cluster_name_);
    }
  }

private:
  const std::string logging_id_;
  const std::string expected_cluster_name_;
  const bool expect_stats_;
  const bool expect_envoy_grpc_specific_stats_;
  const bool expect_response_bytes_;
  const absl::optional<ProtobufWkt::Struct> filter_metadata_;
};

class LoggingTestFilterFactory : public Extensions::HttpFilters::Common::FactoryBase<
                                     test::integration::filters::LoggingTestFilterConfig> {
public:
  LoggingTestFilterFactory() : FactoryBase("logging-test-filter"){};

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::integration::filters::LoggingTestFilterConfig& proto_config, const std::string&,
      Server::Configuration::FactoryContext&) override {
    return [=](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<LoggingTestFilter>(
          proto_config.logging_id(), proto_config.upstream_cluster_name(),
          proto_config.expect_stats(), proto_config.expect_envoy_grpc_specific_stats(),
          proto_config.expect_response_bytes(), proto_config.filter_metadata()));
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
