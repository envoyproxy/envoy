#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class MutationUtils : public Logger::Loggable<Logger::Id::filter> {
public:
  // Convert a header map until a protobuf
  static void headersToProto(const Http::HeaderMap& headers_in,
                             envoy::config::core::v3::HeaderMap& proto_out);

  // Apply mutations that are common to header responses.
  static void
  applyCommonHeaderResponse(const envoy::service::ext_proc::v3::HeadersResponse& response,
                            Http::HeaderMap& headers);

  // Modify header map based on a set of mutations from a protobuf
  static void applyHeaderMutations(const envoy::service::ext_proc::v3::HeaderMutation& mutation,
                                   Http::HeaderMap& headers, bool replacing_message);

  // Apply mutations that are common to body responses.
  // Mutations will be applied to the header map if it is not null.
  static void applyCommonBodyResponse(const envoy::service::ext_proc::v3::BodyResponse& body,
                                      Http::RequestOrResponseHeaderMap* headers,
                                      Buffer::Instance& buffer);

  // Modify a buffer based on a set of mutations from a protobuf
  static void applyBodyMutations(const envoy::service::ext_proc::v3::BodyMutation& mutation,
                                 Buffer::Instance& buffer);

  // Determine if a particular HTTP status code is valid.
  static bool isValidHttpStatus(int code);

private:
  static bool isSettableHeader(const envoy::config::core::v3::HeaderValueOption& header,
                               bool replacing_message);
  static bool isAppendableHeader(absl::string_view key);
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
