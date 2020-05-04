#pragma once

#include <vector>

#include "envoy/http/header_map.h"

namespace Envoy {
namespace Http {

class HeaderListView {
public:
  using HeaderKeyValuePairWrapper =
      std::vector<std::pair<std::reference_wrapper<const HeaderString>,
                            std::reference_wrapper<const HeaderString>>>;
  HeaderListView(const HeaderMap& header_map);
  std::vector<std::reference_wrapper<const HeaderString>> keys() const;
  std::vector<std::reference_wrapper<const HeaderString>> values() const;

private:
  HeaderKeyValuePairWrapper map_;
};

} // namespace Http
} // namespace Envoy
