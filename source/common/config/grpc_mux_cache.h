#pragma once

#include <memory>
#include <string>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription_factory.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "absl/container/flat_hash_map.h"
#include "absl/hash/hash.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Config {

class GrpcMuxKey {
public:
  GrpcMuxKey() = default;
  GrpcMuxKey(const envoy::config::core::v3::ConfigSource& config, absl::string_view type_url)
      : config_(config), type_url_(type_url),
        pre_computed_hash_(absl::HashOf(Envoy::MessageUtil::hash(config), type_url)) {}

  template <typename H> friend H AbslHashValue(H h, const GrpcMuxKey& key) {
    return H::combine(std::move(h), key.pre_computed_hash_);
  }

  friend bool operator==(const GrpcMuxKey& lhs, const GrpcMuxKey& rhs) {
    if (lhs.pre_computed_hash_ == rhs.pre_computed_hash_ && lhs.type_url_ == rhs.type_url_) {
      return Protobuf::util::MessageDifferencer::Equivalent(lhs.config_, rhs.config_);
    }
    return false;
  }

  envoy::config::core::v3::ConfigSource config_;
  std::string type_url_;
  std::size_t pre_computed_hash_{0};
};

class GrpcMuxCache {
public:
  GrpcMuxCache() = default;

  std::shared_ptr<GrpcMux> getOrCreateMux(
      const envoy::config::core::v3::ConfigSource& config, absl::string_view type_url,
      std::function<std::shared_ptr<GrpcMux>()> mux_creator);

  void clear();

private:
  absl::flat_hash_map<GrpcMuxKey, std::weak_ptr<GrpcMux>> cache_;
};

} // namespace Config
} // namespace Envoy
