#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/grpc_field_extraction/v3/config.pb.h"
#include "envoy/extensions/filters/http/grpc_field_extraction/v3/config.pb.validate.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/status/status.h"
#include "grpc_transcoding/type_helper.h"
#include "proto_field_extraction/message_data/message_data.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {

using TypeFinder = std::function<const Protobuf::Type*(const std::string&)>;
struct RequestField {
  // The request field path.
  absl::string_view path;

  // The request field value.
  ProtobufWkt::Value value;
};

using ExtractionResult = std::vector<RequestField>;

class Extractor {
public:
  virtual ~Extractor() = default;

  // Process a request message to extract targeted fields.
  virtual absl::StatusOr<ExtractionResult>
  processRequest(Protobuf::field_extraction::MessageData& message) const = 0;
};

using ExtractorPtr = std::unique_ptr<Extractor>;

class ExtractorFactory {
public:
  virtual ~ExtractorFactory() = default;

  virtual absl::StatusOr<ExtractorPtr> createExtractor(
      const TypeFinder& type_finder, absl::string_view request_type_url,
      const envoy::extensions::filters::http::grpc_field_extraction::v3::FieldExtractions&
          field_extractions) const = 0;
};

} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
