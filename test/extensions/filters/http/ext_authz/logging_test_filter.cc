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

using test::integration::filters::LoggingTestFilterConfig;

Grpc::Status::WellKnownGrpcStatus
GrpcStatusFromProto(LoggingTestFilterConfig::GrpcStatus grpc_status) {
  switch (grpc_status) {
  case LoggingTestFilterConfig::OK:
    return Grpc::Status::WellKnownGrpcStatus::Ok;
  case LoggingTestFilterConfig::CANCELLED:
    return Grpc::Status::WellKnownGrpcStatus::Canceled;
  case LoggingTestFilterConfig::UNKNOWN:
    return Grpc::Status::WellKnownGrpcStatus::Unknown;
  case LoggingTestFilterConfig::INVALID_ARGUMENT:
    return Grpc::Status::WellKnownGrpcStatus::InvalidArgument;
  case LoggingTestFilterConfig::DEADLINE_EXCEEDED:
    return Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded;
  case LoggingTestFilterConfig::NOT_FOUND:
    return Grpc::Status::WellKnownGrpcStatus::NotFound;
  case LoggingTestFilterConfig::ALREADY_EXISTS:
    return Grpc::Status::WellKnownGrpcStatus::AlreadyExists;
  case LoggingTestFilterConfig::PERMISSION_DENIED:
    return Grpc::Status::WellKnownGrpcStatus::PermissionDenied;
  case LoggingTestFilterConfig::RESOURCE_EXHAUSTED:
    return Grpc::Status::WellKnownGrpcStatus::ResourceExhausted;
  case LoggingTestFilterConfig::FAILED_PRECONDITION:
    return Grpc::Status::WellKnownGrpcStatus::FailedPrecondition;
  case LoggingTestFilterConfig::ABORTED:
    return Grpc::Status::WellKnownGrpcStatus::Aborted;
  case LoggingTestFilterConfig::OUT_OF_RANGE:
    return Grpc::Status::WellKnownGrpcStatus::OutOfRange;
  case LoggingTestFilterConfig::UNIMPLEMENTED:
    return Grpc::Status::WellKnownGrpcStatus::Unimplemented;
  case LoggingTestFilterConfig::INTERNAL:
    return Grpc::Status::WellKnownGrpcStatus::Internal;
  case LoggingTestFilterConfig::UNAVAILABLE:
    return Grpc::Status::WellKnownGrpcStatus::Unavailable;
  case LoggingTestFilterConfig::DATA_LOSS:
    return Grpc::Status::WellKnownGrpcStatus::DataLoss;
  case LoggingTestFilterConfig::UNAUTHENTICATED:
    return Grpc::Status::WellKnownGrpcStatus::Unauthenticated;
  default:
    return Grpc::Status::WellKnownGrpcStatus::InvalidCode;
  }
}

// A test filter that retrieve the logging info on encodeComplete.
class LoggingTestFilter : public Http::PassThroughFilter {
public:
  LoggingTestFilter(const LoggingTestFilterConfig& proto_config)
      : logging_id_(proto_config.logging_id()),
        expected_cluster_name_(proto_config.upstream_cluster_name()),
        expect_stats_(proto_config.expect_stats()),
        expect_envoy_grpc_specific_stats_(proto_config.expect_envoy_grpc_specific_stats()),
        expect_response_bytes_(proto_config.expect_response_bytes()),
        filter_metadata_(proto_config.filter_metadata()),
        expect_grpc_status_(proto_config.expect_grpc_status()) {}
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
      if (expect_grpc_status_ != LoggingTestFilterConfig::UNSPECIFIED) {
        ASSERT_TRUE(ext_authz_logging_info->grpcStatus().has_value());
        EXPECT_EQ(ext_authz_logging_info->grpcStatus(), GrpcStatusFromProto(expect_grpc_status_));
      } else {
        EXPECT_FALSE(ext_authz_logging_info->grpcStatus().has_value());
      }
    }
  }

private:
  const std::string logging_id_;
  const std::string expected_cluster_name_;
  const bool expect_stats_;
  const bool expect_envoy_grpc_specific_stats_;
  const bool expect_response_bytes_;
  const absl::optional<Protobuf::Struct> filter_metadata_;
  // The gRPC status returned by the authorization server when it is making a gRPC call.
  const LoggingTestFilterConfig::GrpcStatus expect_grpc_status_;
};

class LoggingTestFilterFactory
    : public Extensions::HttpFilters::Common::FactoryBase<LoggingTestFilterConfig> {
public:
  LoggingTestFilterFactory() : FactoryBase("logging-test-filter") {};

  Http::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const LoggingTestFilterConfig& proto_config, const std::string&,
                                    Server::Configuration::FactoryContext&) override {
    return [=](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<LoggingTestFilter>(proto_config));
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
