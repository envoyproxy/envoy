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
  ExtractorImpl(const TypeFinder& type_finder,
                const google::grpc::transcoding::TypeHelper& type_helper,
                absl::string_view request_type_url, absl::string_view response_type_url,
                // const Envoy::ProtobufWkt::Type* request_type,
                // const Envoy::ProtobufWkt::Type* response_type,
                const envoy::extensions::filters::http::proto_message_logging::v3::MethodLogging&
                    method_logging)
      : method_logging_(method_logging), request_type_url_(request_type_url),
        response_type_url_(response_type_url), type_finder_(type_finder),
        type_helper_(type_helper) {}

  //  The init method should be invoked right after the constructor has been
  //  called.
  absl::Status init();

  void processRequest(Protobuf::field_extraction::MessageData& message) override;

  void processResponse(Protobuf::field_extraction::MessageData& message) override;

  const AuditResult& GetResult() const override { return result_; }

private:
  const envoy::extensions::filters::http::proto_message_logging::v3::MethodLogging& method_logging_;

  std::string request_type_url_;
  std::string response_type_url_;

  const TypeFinder& type_finder_;
  const google::grpc::transcoding::TypeHelper& type_helper_;

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
    auto extractor = std::make_unique<ExtractorImpl>(type_finder, type_helper, request_type_url,
                                                     response_type_url, method_logging);
    auto status = extractor->init();
    if (!status.ok()) {
      return status;
    }

    return extractor;
  }
};

} // namespace ProtoMessageLogging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
