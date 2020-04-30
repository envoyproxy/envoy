#pragma once

#include <atomic>
#include <string>

#include "envoy/runtime/runtime.h"
#include "envoy/upstream/resource_manager.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

class DnsResource : public Envoy::Upstream::Resource {
public:
  DnsResource(uint64_t max, Runtime::Loader& runtime, const std::string& runtime_key)
      : max_(max), runtime_(runtime), runtime_key_(runtime_key) {}
  ~DnsResource() override { ASSERT(current_ == 0); }

  // Envoy::Upstream::Resource
  bool canCreate() override { return current_ < max(); }
  void inc() override { current_++; }
  void dec() override { decBy(1); }
  void decBy(uint64_t amount) override {
    ASSERT(current_ >= amount);
    current_ -= amount;
  }
  uint64_t max() override { return runtime_.snapshot().getInteger(runtime_key_, max_); }
  uint64_t count() const override { return current_.load(); }

private:
  uint64_t max_;
  std::atomic<uint64_t> current_{};
  Runtime::Loader& runtime_;
  const std::string runtime_key_;
};

class DnsCacheResourceManager {
public:
  DnsCacheResourceManager(Runtime::Loader& runtime, const std::string& runtime_key,
                          uint64_t max_pending_requests)
      : pending_requests_(max_pending_requests, runtime, runtime_key) {}

  DnsResource& pendingRequests() { return pending_requests_; }

private:
  DnsResource pending_requests_;
};

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
