#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/proto_message_scrubbing/v3/config.pb.h"
#include "envoy/extensions/filters/http/proto_message_scrubbing/v3/config.pb.validate.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/proto_message_scrubbing/scrubbing_util/proto_scrubber_interface.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "grpc_transcoding/type_helper.h"
#include "proto_field_extraction/message_data/message_data.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageScrubbing {

using ::Envoy::Protobuf::Type;

using TypeFinder = std::function<const Envoy::ProtobufWkt::Type*(const std::string&)>;

struct ScrubbedMessageResult {
  const TypeFinder* type_finder;

  std::vector<::Envoy::Extensions::HttpFilters::ProtoMessageScrubbing::ScrubbedMessageMetadata>
      request_data;
  std::vector<::Envoy::Extensions::HttpFilters::ProtoMessageScrubbing::ScrubbedMessageMetadata>
      response_data;

  // Extracted struct with a "@type" field.
  ProtobufWkt::Struct request_type_struct;
  ProtobufWkt::Struct response_type_struct;
};

class Extractor {
public:
  virtual ~Extractor() = default;

  // Extract scrub fields on a request message.
  // It only needs to be called for the first request message and the last
  // for a client streaming call.
  // It can be called on every message too if callers don't know which one
  // is the last message. It only keeps the result from the first and the last.
  virtual void processRequest(Protobuf::field_extraction::MessageData& message) = 0;

  // Extract scrub fields on a response message.
  // It only needs to be called for the first response message and the last
  // for a server streaming call.
  // It can be called on every message too if callers don't know which one
  // is the last message. It only keeps the result from the first one the last.
  virtual void processResponse(Protobuf::field_extraction::MessageData& message) = 0;

  virtual const ScrubbedMessageResult& GetResult() const = 0;
};

using ExtractorPtr = std::unique_ptr<Extractor>;

class ExtractorFactory {
public:
  virtual ~ExtractorFactory() = default;

  virtual absl::StatusOr<ExtractorPtr> createExtractor(
      const google::grpc::transcoding::TypeHelper& type_helper, const TypeFinder& type_finder,
      std::string request_type_url, std::string response_type_url,
      const envoy::extensions::filters::http::proto_message_scrubbing::v3::MethodScrubbing&
          method_scrubbing) const = 0;
};

using ExtractorFactoryPtr = std::unique_ptr<ExtractorFactory>;

} // namespace ProtoMessageScrubbing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
