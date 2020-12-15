#include "extensions/filters/http/ext_proc/mutation_utils.h"

#include "common/http/headers.h"
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
  for (const auto& rh : mutation.remove_headers()) {
    // The "router" component removes headers first when processing this protobuf
    // Like that component and "ext_auth", don't allow removing any system headers
    // (with ":") and don't allow removal of "host".
    if (rh.empty() || rh[0] == ':') {
      continue;
    }
    const LowerCaseString header(rh);
    if (header != Http::Headers::get().HostLegacy) {
      headers.remove(header);
    }
  }

  for (const auto& sh : mutation.set_headers()) {
    if (!sh.has_header()) {
      continue;
    }
    // The "router" and "ext_authz" both use "false" as the default here,
    // which matches ext_authz, but does not match the way that the same
    // proto is used in the router
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