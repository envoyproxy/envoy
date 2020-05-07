#pragma once

#include <vector>

#include "envoy/http/header_map.h"

namespace Envoy {
namespace Http {

class HeaderListView {
public:
  HeaderListView(const HeaderMap& header_map);
  std::vector<std::reference_wrapper<const HeaderString>> keys() const { return keys_; }
  std::vector<std::reference_wrapper<const HeaderString>> values() const { return values_; }

private:
  std::vector<std::reference_wrapper<const HeaderString>> keys_;
  std::vector<std::reference_wrapper<const HeaderString>> values_;
};

} // namespace Http
} // namespace Envoy
