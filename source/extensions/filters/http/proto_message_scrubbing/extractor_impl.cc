#include "source/extensions/filters/http/proto_message_scrubbing/extractor_impl.h"

#include <string>
#include <vector>

#include "envoy/extensions/filters/http/proto_message_scrubbing/v3/config.pb.h"
#include "envoy/extensions/filters/http/proto_message_scrubbing/v3/config.pb.validate.h"

#include "source/extensions/filters/http/proto_message_scrubbing/extractor.h"
#include "source/extensions/filters/http/proto_message_scrubbing/scrubbing_util/proto_scrubber_interface.h"

#include "absl/base/attributes.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "grpc_transcoding/type_helper.h"
#include "proto_field_extraction/field_value_extractor/field_value_extractor_factory.h"
#include "proto_field_extraction/field_value_extractor/field_value_extractor_interface.h"
#include "proto_field_extraction/message_data/message_data.h"
#include "proto_processing_lib/proto_scrubber/proto_scrubber_enums.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageScrubbing {
namespace {

using ::envoy::extensions::filters::http::proto_message_scrubbing::v3::MethodScrubbing;
using ::Envoy::Extensions::HttpFilters::ProtoMessageScrubbing::ScrubbedMessageDirective;
using ::Envoy::Extensions::HttpFilters::ProtoMessageScrubbing::ScrubbedMessageMetadata;
using ::google::grpc::transcoding::TypeHelper;
using ::proto_processing_lib::proto_scrubber::ScrubberContext;
using Protobuf::field_extraction::FieldValueExtractorFactory;

// The type property value that will be included into the converted Struct.
constexpr char kTypeProperty[] = "@type";

ABSL_CONST_INIT const char* const kTypeServiceBaseUrl = "type.googleapis.com";

void Extract(ProtoScrubberInterface& scrubber, Protobuf::field_extraction::MessageData& message,
             std::vector<ScrubbedMessageMetadata>& vect) {
  ScrubbedMessageMetadata data = scrubber.ScrubMessage(message);
  LOG(INFO) << "Extracted scrubbed fields: " << data.scrubbed_message;

  // Only need to keep the result from the first and the last.
  // Always overwrite the 2nd result as the last one.
  if (vect.size() < 2) {
    vect.push_back(data);
  } else {
    // copy and override the second one as the last one.
    vect[1] = data;
  }
}

std::string GetFullTypeWithUrl(absl::string_view simple_type) {
  return absl::StrCat(kTypeServiceBaseUrl, "/", simple_type);
}

void FillStructWithType(const ::Envoy::ProtobufWkt::Type& type, ::Envoy::ProtobufWkt::Struct& out) {
  (*out.mutable_fields())[kTypeProperty].set_string_value(GetFullTypeWithUrl(type.name()));
}

ScrubbedMessageDirective TypeMapping(const MethodScrubbing::ScrubDirective& type) {
  switch (type) {
  case MethodScrubbing::SCRUB:
    return ScrubbedMessageDirective::SCRUB;
  case MethodScrubbing::SCRUB_REDACT:
    return ScrubbedMessageDirective::SCRUB_REDACT;
  case MethodScrubbing::ScrubDirective_UNSPECIFIED:
    return ScrubbedMessageDirective::SCRUB;
  default:
    return ScrubbedMessageDirective::SCRUB;
  }
}

} // namespace

absl::Status ExtractorImpl::init() {
  FieldValueExtractorFactory extractor_factory(type_finder_);
  for (const auto& it : method_scrubbing_.request_scrubbing_by_field()) {
    auto extractor = extractor_factory.Create(request_type_url_, it.first);
    if (!extractor.ok()) {
      return extractor.status();
    }

    request_field_path_to_scrub_type_[it.first].push_back(TypeMapping(it.second));
  }

  for (const auto& it : method_scrubbing_.response_scrubbing_by_field()) {
    auto extractor = extractor_factory.Create(response_type_url_, it.first);
    if (!extractor.ok()) {
      return extractor.status();
    }

    response_field_path_to_scrub_type_[it.first].push_back(TypeMapping(it.second));
  }

  request_scrubber_ =
      ProtoScrubber::Create(ScrubberContext::kRequestScrubbing, &type_helper_,
                            type_finder_(request_type_url_), request_field_path_to_scrub_type_);

  response_scrubber_ =
      ProtoScrubber::Create(ScrubberContext::kResponseScrubbing, &type_helper_,
                            type_finder_(response_type_url_), response_field_path_to_scrub_type_);

  FillStructWithType(*type_finder_(request_type_url_), result_.request_type_struct);
  FillStructWithType(*type_finder_(response_type_url_), result_.response_type_struct);
  return absl::OkStatus();
}

void ExtractorImpl::processRequest(Protobuf::field_extraction::MessageData& message) {
  Extract(*request_scrubber_, message, result_.request_data);
}

void ExtractorImpl::processResponse(Protobuf::field_extraction::MessageData& message) {
  Extract(*response_scrubber_, message, result_.response_data);
}
} // namespace ProtoMessageScrubbing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
