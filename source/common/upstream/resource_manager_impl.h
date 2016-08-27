#pragma once

#include "envoy/upstream/resource_manager.h"

#include "common/common/assert.h"

namespace Upstream {

/**
 * Implementation of ResourceManager.
 * NOTE: This implementation makes some assumptions which favor simplicity over correctness.
 * 1) Primarily, it assumes that traffic will be mostly balanced over all the worker threads since
 *    no attempt is made to balance resources between them. It is possible that starvation can
 *    occur during high contention.
 * 2) Though atomics are used, it is possible for resources to temporarily go above the supplied
 *    maximums. This should not effect overall behavior.
 */
class ResourceManagerImpl : public ResourceManager {
public:
  ResourceManagerImpl(uint64_t max_connections, uint64_t max_pending_requests,
                      uint64_t max_requests, uint64_t max_retries)
      : connections_(max_connections), pending_requests_(max_pending_requests),
        requests_(max_requests), retries_(max_retries) {}

  // Upstream::ResourceManager
  Resource& connections() override { return connections_; }
  Resource& pendingRequests() override { return pending_requests_; }
  Resource& requests() override { return requests_; }
  Resource& retries() override { return retries_; }

private:
  struct ResourceImpl : public Resource {
    ResourceImpl(uint64_t max) : max_(max) {}
    ~ResourceImpl() { ASSERT(current_ == 0); }

    // Upstream::Resource
    bool canCreate() override { return current_ < max_; }
    void inc() override { current_++; }
    void dec() override {
      ASSERT(current_ > 0);
      current_--;
    }
    uint64_t max() override { return max_; }

    const uint64_t max_;
    std::atomic<uint64_t> current_{};
  };

  ResourceImpl connections_;
  ResourceImpl pending_requests_;
  ResourceImpl requests_;
  ResourceImpl retries_;
};

typedef std::unique_ptr<ResourceManagerImpl> ResourceManagerImplPtr;

} // Upstream
