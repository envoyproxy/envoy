#pragma once

#include <vector>

#include "envoy/http/header_map.h"

namespace Envoy {
namespace Http {

class HeaderListView {
public:
  using HeaderStringRefs = std::vector<std::reference_wrapper<const HeaderString>>;

  HeaderListView(const HeaderMap& header_map);
  const HeaderStringRefs& keys() const { return keys_; }
  const HeaderStringRefs& values() const { return values_; }

private:
  HeaderStringRefs keys_;
  HeaderStringRefs values_;
};

} // namespace Http
} // namespace Envoy
