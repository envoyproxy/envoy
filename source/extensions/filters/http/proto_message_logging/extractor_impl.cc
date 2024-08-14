#include "source/extensions/filters/http/proto_message_logging/extractor_impl.h"

#include <string>
#include <vector>

#include "envoy/extensions/filters/http/proto_message_logging/v3/config.pb.h"
#include "envoy/extensions/filters/http/proto_message_logging/v3/config.pb.validate.h"

#include "source/extensions/filters/http/proto_message_logging/extractor.h"
#include "source/extensions/filters/http/proto_message_logging/logging_util/proto_scrubber_interface.h"

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
namespace ProtoMessageLogging {
namespace {

using ::envoy::extensions::filters::http::proto_message_logging::v3::MethodLogging;
using ::Envoy::Extensions::HttpFilters::ProtoMessageLogging::AuditDirective;
using ::Envoy::Extensions::HttpFilters::ProtoMessageLogging::AuditMetadata;
using ::google::grpc::transcoding::TypeHelper;
using ::proto_processing_lib::proto_scrubber::ScrubberContext;
using Protobuf::field_extraction::FieldValueExtractorFactory;

// The type property value that will be included into the converted Struct.
constexpr char kTypeProperty[] = "@type";

ABSL_CONST_INIT const char* const kTypeServiceBaseUrl = "type.googleapis.com";

void Extract(ProtoScrubberInterface& scrubber, Protobuf::field_extraction::MessageData& message,
             std::vector<AuditMetadata>& vect) {
  AuditMetadata data = scrubber.ScrubMessage(message);
  LOG(INFO) << "Extracted audit fields: " << data.scrubbed_message;

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

AuditDirective TypeMapping(const MethodLogging::LogDirective& type) {
  switch (type) {
  case MethodLogging::LOG:
    return AuditDirective::AUDIT;
  case MethodLogging::LOG_REDACT:
    return AuditDirective::AUDIT_REDACT;
  case MethodLogging::LogDirective_UNSPECIFIED:
    return AuditDirective::AUDIT;
  default:
    return AuditDirective::AUDIT;
  }
}

} // namespace

absl::Status ExtractorImpl::init() {
  FieldValueExtractorFactory extractor_factory(type_finder_);
  for (const auto& it : method_logging_.request_logging_by_field()) {
    auto extractor = extractor_factory.Create(request_type_url_, it.first);
    if (!extractor.ok()) {
      return extractor.status();
    }

    request_field_path_to_scrub_type_[it.first].push_back(TypeMapping(it.second));
  }

  for (const auto& it : method_logging_.response_logging_by_field()) {
    auto extractor = extractor_factory.Create(response_type_url_, it.first);
    if (!extractor.ok()) {
      return extractor.status();
    }

    response_field_path_to_scrub_type_[it.first].push_back(TypeMapping(it.second));
  }

  request_scrubber_ = AuditProtoScrubber::Create(ScrubberContext::kRequestScrubbing, &type_helper_,
                                                 type_finder_(request_type_url_),
                                                 request_field_path_to_scrub_type_);

  response_scrubber_ = AuditProtoScrubber::Create(ScrubberContext::kResponseScrubbing,
                                                  &type_helper_, type_finder_(response_type_url_),
                                                  response_field_path_to_scrub_type_);

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
} // namespace ProtoMessageLogging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
