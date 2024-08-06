#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/filters/http/proto_message_logging/v3/config.pb.h"
#include "envoy/extensions/filters/http/proto_message_logging/v3/config.pb.validate.h"

#include "source/extensions/filters/http/proto_message_logging/extractor.h"
#include "source/extensions/filters/http/proto_message_logging/logging_util/audit_proto_scrubber.h"
#include "source/extensions/filters/http/proto_message_logging/logging_util/proto_scrubber_interface.h"

#include "absl/status/statusor.h"
#include "grpc_transcoding/type_helper.h"
#include "proto_field_extraction/message_data/message_data.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageLogging {

class ExtractorImpl : public Extractor {
public:
  ExtractorImpl(const google::grpc::transcoding::TypeHelper& type_helper,
                const ::Envoy::ProtobufWkt::Type* request_type,
                const ::Envoy::ProtobufWkt::Type* response_type,
                const envoy::extensions::filters::http::proto_message_logging::v3::MethodLogging&
                    method_logging);

  void processRequest(Protobuf::field_extraction::MessageData& message) override;

  void processResponse(Protobuf::field_extraction::MessageData& message) override;

  const AuditResult& GetResult() const override { return result_; }

private:
  const envoy::extensions::filters::http::proto_message_logging::v3::MethodLogging& method_logging_;

  FieldPathToScrubType request_field_path_to_scrub_type_;

  FieldPathToScrubType response_field_path_to_scrub_type_;

  AuditResult result_;
  std::unique_ptr<ProtoScrubberInterface> request_scrubber_;
  std::unique_ptr<ProtoScrubberInterface> response_scrubber_;
};

class ExtractorFactoryImpl : public ExtractorFactory {
public:
  absl::StatusOr<ExtractorPtr>
  createExtractor(const google::grpc::transcoding::TypeHelper& type_helper,
                  const TypeFinder& type_finder, std::string request_type_url,
                  std::string response_type_url,
                  const envoy::extensions::filters::http::proto_message_logging::v3::MethodLogging&
                      method_logging) const override {
    auto extractor = std::make_unique<ExtractorImpl>(
        type_helper, type_finder(request_type_url), type_finder(response_type_url), method_logging);

    return extractor;
  }
};

} // namespace ProtoMessageLogging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
