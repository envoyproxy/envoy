#pragma once

// NOLINT(namespace-envoy)

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"

using RawHeaders = absl::flat_hash_map<std::string, std::vector<std::string>>;

class Headers {
public:
  virtual ~Headers() {}

  const std::vector<std::string>& operator[](const std::string& key) const;
  const RawHeaders& all_headers() const;

protected:
  Headers(const RawHeaders& headers);

private:
  RawHeaders headers_;
};
