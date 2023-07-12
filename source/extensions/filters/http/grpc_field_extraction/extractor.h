#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/grpc_field_extraction/v3/config.pb.h"
#include "envoy/extensions/filters/http/grpc_field_extraction/v3/config.pb.validate.h"
#include "absl/status/status.h"
#include "grpc_transcoding/type_helper.h"
#include "src/message_data/message_data.h"

namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction {

using TypeFinder = std::function<const google::protobuf::Type*(const std::string&)>;
struct RequestField {
  absl::string_view field_path;

  // The policy information for the proto field under extraction.
  // The lifetime of this field is tied to the `method_info` argument of the
  // `GrpcExtractor` constructor.

  // A list of resource name (with container) and resource location.
  // Repeated proto fields may have more than 1 extracted pair.
  // Resource location may be empty, but resource name is always present.
  std::vector<std::string> values;
};

struct ExtractionResult {
  // A list of extracted resources, one per field.
  std::vector<RequestField> req_fields;
};

class Extractor {
public:
  virtual ~Extractor() = default;

  // Process a request message to extract resource info.
  // This should be called once, and its extracted result can be fetched
  // by GetResult() if this call is successful.
  virtual absl::Status ProcessRequest(google::protobuf::field_extraction::MessageData& message) = 0;

  // Return the extracted result if ProcessRequest is successful.
  virtual const ExtractionResult& GetResult() const = 0;
};

using ExtractorPtr = std::unique_ptr<Extractor>;

class ExtractorFactory {
public:
  virtual ~ExtractorFactory() = default;

  virtual ExtractorPtr CreateExtractor(
      TypeFinder type_finder_, absl::string_view request_type_url,
      const envoy::extensions::filters::http::grpc_field_extraction::v3::FieldExtractions&
          field_extractions) const = 0;
};

} // namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction
