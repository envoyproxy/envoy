#include "extensions/filters/http/ext_proc/headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using Http::LowerCaseString;

void buildHttpHeaders(const Http::HeaderMap& headers_in,
                      envoy::config::core::v3::HeaderMap* headers_out) {
  headers_in.iterate([headers_out](const Http::HeaderEntry& e) -> Http::HeaderMap::Iterate {
    auto new_header = headers_out->add_headers();
    new_header->set_key(std::string(e.key().getStringView()));
    new_header->set_value(std::string(e.value().getStringView()));
    return Http::HeaderMap::Iterate::Continue;
  });
}

void applyHeaderMutations(const envoy::service::ext_proc::v3alpha::HeaderMutation& mutation,
                          Http::HeaderMap* headers) {
  for (const auto& sh : mutation.set_headers()) {
    if (!sh.has_header()) {
      continue;
    }
    bool append = false;
    if (sh.has_append()) {
      append = sh.append().value();
    }
    if (append) {
      headers->addCopy(LowerCaseString(sh.header().key()), sh.header().value());
    } else {
      headers->setCopy(LowerCaseString(sh.header().key()), sh.header().value());
    }
  }

  for (const auto& rh : mutation.remove_headers()) {
    headers->remove(LowerCaseString(rh));
  }
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy