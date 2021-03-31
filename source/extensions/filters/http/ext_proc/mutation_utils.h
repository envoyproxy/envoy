#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"
#include "envoy/service/ext_proc/v3alpha/external_processor.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class MutationUtils {
public:
  // Convert a header map until a protobuf
  static void buildHttpHeaders(const Http::HeaderMap& headers_in,
                               envoy::config::core::v3::HeaderMap& headers_out);

  // Apply mutations that are common to header responses.
  static void
  applyCommonHeaderResponse(const envoy::service::ext_proc::v3alpha::HeadersResponse& response,
                            Http::HeaderMap& headers);

  // Modify header map based on a set of mutations from a protobuf
  static void
  applyHeaderMutations(const envoy::service::ext_proc::v3alpha::HeaderMutation& mutation,
                       Http::HeaderMap& headers);

  // Apply mutations that are common to body responses.
  static void applyCommonBodyResponse(const envoy::service::ext_proc::v3alpha::BodyResponse& body,
                                      Buffer::Instance& buffer);

  // Modify a buffer based on a set of mutations from a protobuf
  static void applyBodyMutations(const envoy::service::ext_proc::v3alpha::BodyMutation& mutation,
                                 Buffer::Instance& buffer);

private:
  static bool isSettableHeader(absl::string_view key);
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy