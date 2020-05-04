#include "common/http/header_list_view.h"

namespace Envoy {
namespace Http {

HeaderListView::HeaderListView(const HeaderMap& header_map) {
  header_map.iterate(
      [](const Http::HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        auto* context_ptr = static_cast<HeaderListView::HeaderKeyValuePairWrapper*>(context);
        auto key = std::reference_wrapper<const HeaderString>(header.key());
        auto value = std::reference_wrapper<const HeaderString>(header.value());
        context_ptr->emplace_back(std::make_pair(key, value));
        return HeaderMap::Iterate::Continue;
      },
      &map_);
}

std::vector<std::reference_wrapper<const HeaderString>> HeaderListView::keys() const {
  std::vector<std::reference_wrapper<const HeaderString>> header_keys;
  for (const auto& pair : map_) {
    header_keys.emplace_back(pair.first);
  }
  return header_keys;
}

std::vector<std::reference_wrapper<const HeaderString>> HeaderListView::values() const {
  std::vector<std::reference_wrapper<const HeaderString>> header_values;
  for (const auto& pair : map_) {
    header_values.emplace_back(pair.second);
  }
  return header_values;
}

} // namespace Http
} // namespace Envoy