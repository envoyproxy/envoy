#pragma once

#include "envoy/http/header_map.h"
#include "envoy/service/ext_proc/v3alpha/external_processor.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

extern void buildHttpHeaders(const Http::HeaderMap& headers_in,
                             envoy::config::core::v3::HeaderMap* headers_out);

extern void applyHeaderMutations(const envoy::service::ext_proc::v3alpha::HeaderMutation& mutation,
                                 Http::HeaderMap* headers);

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy