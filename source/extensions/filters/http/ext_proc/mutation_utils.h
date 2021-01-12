#pragma once

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

  // Modify header map based on a set of mutations from a protobuf
  static void
  applyHeaderMutations(const envoy::service::ext_proc::v3alpha::HeaderMutation& mutation,
                       Http::HeaderMap& headers);

private:
  static bool isSettableHeader(absl::string_view key);
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy