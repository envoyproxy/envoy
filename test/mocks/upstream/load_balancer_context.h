#pragma once

namespace Upstream {

class TestLoadBalancerContext : public LoadBalancerContext {
public:
  TestLoadBalancerContext(uint64_t hash_key, bool prefer_canary = false)
      : hash_key_(hash_key), prefer_canary_(prefer_canary) {}

  TestLoadBalancerContext(Optional<uint64_t>& hash_key, bool prefer_canary = false)
      : hash_key_(hash_key), prefer_canary_(prefer_canary) {}

  // Upstream::LoadBalancerContext
  const Optional<uint64_t>& hashKey() const override { return hash_key_; }
  bool preferCanary() const override { return prefer_canary_; }

  Optional<uint64_t> hash_key_;
  bool prefer_canary_;
};

} // Upstream
