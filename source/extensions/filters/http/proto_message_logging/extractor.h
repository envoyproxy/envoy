#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "envoy/extensions/filters/http/proto_message_logging/v3/config.pb.h"
#include "envoy/extensions/filters/http/proto_message_logging/v3/config.pb.validate.h"
#include "google/protobuf/util/converter/type_info.h"
#include "google/protobuf/util/type_resolver.h"
#include "grpc_transcoding/type_helper.h"
#include "proto_field_extraction/message_data/message_data.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/proto_message_logging/logging_util/proto_scrubber_interface.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageLogging {

using ::Envoy::Protobuf::Type;

using TypeFinder =
    std::function<const google::protobuf::Type*(const std::string&)>;

struct AuditResult {
  const TypeFinder* type_finder;

  std::vector<
      ::Envoy::Extensions::HttpFilters::ProtoMessageLogging::AuditMetadata>
      request_data;
  std::vector<
      ::Envoy::Extensions::HttpFilters::ProtoMessageLogging::AuditMetadata>
      response_data;

  // Extracted struct with a "@type" field.
  ProtobufWkt::Struct request_type_struct;
  ProtobufWkt::Struct response_type_struct;
};

class Extractor {
 public:
  virtual ~Extractor() = default;

  // Extract audit fields on a request message.
  // It only needs to be called for the first request message and the last
  // for a client streaming call.
  // It can be called on every message too if callers don't know which one
  // is the last message.  It only keeps the result from the first and the last.
  virtual void processRequest(
      google::protobuf::field_extraction::MessageData& message) = 0;

  // Extract audit fields on a response message.
  // It only needs to be called for the first response message and the last
  // for a server streaming call.
  // It can be called on every message too if callers don't know which one
  // is the last message.  It only keeps the result from the first one the last.
  virtual void processResponse(
      google::protobuf::field_extraction::MessageData& message) = 0;

  virtual const AuditResult& GetResult() const = 0;
};

using ExtractorPtr = std::unique_ptr<Extractor>;

class ExtractorFactory {
 public:
  virtual ~ExtractorFactory() = default;

  virtual absl::StatusOr<ExtractorPtr> createExtractor(
      const google::grpc::transcoding::TypeHelper& type_helper,
      const google::protobuf::util::converter::TypeInfo& type_info,
      const TypeFinder& type_finder, std::string request_type_url,
      std::string response_type_url,
      const envoy::extensions::filters::http::proto_message_logging::v3::
          MethodLogging& method_logging) const = 0;
};

using ExtractorFactoryPtr = std::unique_ptr<ExtractorFactory>;

}  // namespace ProtoMessageLogging
}  // namespace HttpFilters
}  // namespace Extensions
}  // namespace Envoy
