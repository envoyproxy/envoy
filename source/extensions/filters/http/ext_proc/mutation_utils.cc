#include "extensions/filters/http/ext_proc/mutation_utils.h"

#include "common/http/header_utility.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using Http::LowerCaseString;

void MutationUtils::buildHttpHeaders(const Http::HeaderMap& headers_in,
                                     envoy::config::core::v3::HeaderMap& headers_out) {
  headers_in.iterate([&headers_out](const Http::HeaderEntry& e) -> Http::HeaderMap::Iterate {
    auto* new_header = headers_out.add_headers();
    new_header->set_key(std::string(e.key().getStringView()));
    new_header->set_value(std::string(e.value().getStringView()));
    return Http::HeaderMap::Iterate::Continue;
  });
}

void MutationUtils::applyHeaderMutations(
    const envoy::service::ext_proc::v3alpha::HeaderMutation& mutation, Http::HeaderMap& headers) {
  for (const auto& remove_header : mutation.remove_headers()) {
    if (Http::HeaderUtility::isRemovableHeader(remove_header)) {
      headers.remove(LowerCaseString(remove_header));
    }
  }

  for (const auto& sh : mutation.set_headers()) {
    if (!sh.has_header()) {
      continue;
    }
    // Make "false" the default. This is logical and matches the ext_authz
    // filter. However, the router handles this same protobuf and uses "true"
    // as the default instead.
    const bool append = PROTOBUF_GET_WRAPPED_OR_DEFAULT(sh, append, false);
    if (append) {
      headers.addCopy(LowerCaseString(sh.header().key()), sh.header().value());
    } else {
      headers.setCopy(LowerCaseString(sh.header().key()), sh.header().value());
    }
  }
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy