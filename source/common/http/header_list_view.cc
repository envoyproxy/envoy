#include "common/http/header_list_view.h"

namespace Envoy {
namespace Http {

HeaderListView::HeaderListView(const HeaderMap& header_map) {
  header_map.iterate(
      [](const Http::HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        auto* context_ptr = static_cast<HeaderListView*>(context);
        context_ptr->keys_.emplace_back(std::reference_wrapper<const HeaderString>(header.key()));
        context_ptr->values_.emplace_back(
            std::reference_wrapper<const HeaderString>(header.value()));
        return HeaderMap::Iterate::Continue;
      },
      this);
}

} // namespace Http
} // namespace Envoy