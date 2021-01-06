#pragma once

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Platform {

using RawHeaderMap = absl::flat_hash_map<std::string, std::vector<std::string>>;

class Headers {
public:
  virtual ~Headers() {}

  const std::vector<std::string>& operator[](const std::string& key) const;
  const RawHeaderMap& all_headers() const;
  bool contains(const std::string& key) const;

protected:
  Headers(const RawHeaderMap& headers);

private:
  RawHeaderMap headers_;
};

} // namespace Platform
} // namespace Envoy
