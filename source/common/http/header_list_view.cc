#include "source/common/http/header_list_view.h"

namespace Envoy {
namespace Http {

HeaderListView::HeaderListView(const HeaderMap& header_map) {
  header_map.iterate([this](const Http::HeaderEntry& header) -> HeaderMap::Iterate {
    keys_.emplace_back(std::reference_wrapper<const HeaderString>(header.key()));
    values_.emplace_back(std::reference_wrapper<const HeaderString>(header.value()));
    return HeaderMap::Iterate::Continue;
  });
}

} // namespace Http
} // namespace Envoy
