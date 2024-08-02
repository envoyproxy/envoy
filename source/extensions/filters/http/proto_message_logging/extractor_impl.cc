#include "source/extensions/filters/http/proto_message_logging/extractor_impl.h"

#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "envoy/extensions/filters/http/proto_message_logging/v3/config.pb.h"
#include "envoy/extensions/filters/http/proto_message_logging/v3/config.pb.validate.h"
#include "google/protobuf/util/converter/type_info.h"
#include "google/protobuf/util/type_resolver.h"
#include "grpc_transcoding/type_helper.h"
#include "proto_field_extraction/message_data/message_data.h"
#include "proto_processing_lib/proto_scrubber/proto_scrubber_enums.h"
#include "source/extensions/filters/http/proto_message_logging/extractor.h"
#include "source/extensions/filters/http/proto_message_logging/logging_util/proto_scrubber_interface.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageLogging {
namespace {

using ::envoy::extensions::filters::http::proto_message_logging::v3::
    MethodLogging;
using ::Envoy::Extensions::HttpFilters::ProtoMessageLogging::AuditDirective;
using ::Envoy::Extensions::HttpFilters::ProtoMessageLogging::AuditMetadata;
using ::google::grpc::transcoding::TypeHelper;
using ::google::protobuf::util::converter::TypeInfo;
using ::proto_processing_lib::proto_scrubber::ScrubberContext;

// The type property value that will be included into the converted Struct.
constexpr char kTypeProperty[] = "@type";

ABSL_CONST_INIT const char* const kTypeServiceBaseUrl = "type.googleapis.com";

void Extract(ProtoScrubberInterface& scrubber,
             google::protobuf::field_extraction::MessageData& message,
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

void FillStructWithType(const google::protobuf::Type& type,
                        ProtobufWkt::Struct& out) {
  (*out.mutable_fields())[kTypeProperty].set_string_value(
      GetFullTypeWithUrl(type.name()));
}

// std::vector<AuditDirective> GetAuditDirectives(absl::string_view audittings)
// {
//   std::vector<AuditDirective> audit_directives;
//   for (absl::string_view auditting :
//        absl::StrSplit(audittings, ',', absl::SkipWhitespace())) {
//     if (auditting == "AUDIT") {
//       audit_directives.push_back(AuditDirective::AUDIT);
//     } else if (auditting == "AUDIT_REDACT") {
//       audit_directives.push_back(AuditDirective::AUDIT_REDACT);
//     }
//   }
//   return audit_directives;
// }

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

}  // namespace

ExtractorImpl::ExtractorImpl(const TypeHelper& type_helper,
                             const TypeInfo& type_info,
                             const google::protobuf::Type* request_type,
                             const google::protobuf::Type* response_type,
                             const MethodLogging& method_logging)
    : type_info_(type_info), method_logging_(method_logging) {
  for (const auto& rl : method_logging.request_logging_by_field()) {
    request_field_path_to_scrub_type_[rl.first].push_back(
        TypeMapping(rl.second));
  }

  for (const auto& rl : method_logging.response_logging_by_field()) {
    response_field_path_to_scrub_type_[rl.first].push_back(
        TypeMapping(rl.second));
  }

  request_scrubber_ = AuditProtoScrubber::Create(
      ScrubberContext::kRequestScrubbing, &type_helper, &type_info_,
      request_type, request_field_path_to_scrub_type_);

  response_scrubber_ = AuditProtoScrubber::Create(
      ScrubberContext::kResponseScrubbing, &type_helper, &type_info_,
      response_type, response_field_path_to_scrub_type_);

  FillStructWithType(*request_type, result_.request_type_struct);
  FillStructWithType(*response_type, result_.response_type_struct);
}

void ExtractorImpl::processRequest(
    google::protobuf::field_extraction::MessageData& message) {
  Extract(*request_scrubber_, message, result_.request_data);
}

void ExtractorImpl::processResponse(
    google::protobuf::field_extraction::MessageData& message) {
  Extract(*response_scrubber_, message, result_.response_data);
}
}  // namespace ProtoMessageLogging
}  // namespace HttpFilters
}  // namespace Extensions
}  // namespace Envoy
