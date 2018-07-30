#include "extensions/filters/network/thrift_proxy/metadata.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

HeaderMap::HeaderMap(const std::initializer_list<std::pair<std::string, std::string>>& values) {
  for (auto& value : values) {
    headers_.emplace_back(Header(value.first, value.second));
  }
}

HeaderMap::HeaderMap(const HeaderMap& rhs) {
  for (const auto& header : rhs.headers_) {
    headers_.emplace_back(header);
  }
}

bool HeaderMap::operator==(const HeaderMap& rhs) const {
  if (headers_.size() != rhs.headers_.size()) {
    return false;
  }

  for (auto i = headers_.begin(), j = rhs.headers_.begin(); i != headers_.end(); ++i, ++j) {
    if (i->key() != j->key() || i->value() != j->value()) {
      return false;
    }
  }

  return true;
}

Header* HeaderMap::get(const std::string& key) {
  for (Header& header : headers_) {
    if (header.key() == key) {
      return &header;
    }
  }

  return nullptr;
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
