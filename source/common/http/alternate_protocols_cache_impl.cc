#include "common/http/alternate_protocols_cache_impl.h"

namespace Envoy {
namespace Http {

AlternateProtocolsCacheImpl::AlternateProtocolsCacheImpl(TimeSource& time_source)
    : time_source_(time_source) {}

AlternateProtocolsCacheImpl::~AlternateProtocolsCacheImpl() = default;

void AlternateProtocolsCacheImpl::setAlternatives(const Origin& origin,
                                                  const std::vector<AlternateProtocol>& protocols,
                                                  const MonotonicTime& expiration) {
  Entry& entry = protocols_[origin];
  if (entry.protocols_ != protocols) {
    entry.protocols_ = protocols;
  }
  if (entry.expiration_ != expiration) {
    entry.expiration_ = expiration;
  }
}

OptRef<const std::vector<AlternateProtocolsCache::AlternateProtocol>>
AlternateProtocolsCacheImpl::findAlternatives(const Origin& origin) {
  auto entry_it = protocols_.find(origin);
  if (entry_it == protocols_.end()) {
    return makeOptRefFromPtr<const std::vector<AlternateProtocolsCache::AlternateProtocol>>(
        nullptr);
  }

  const Entry& entry = entry_it->second;
  if (time_source_.monotonicTime() > entry.expiration_) {
    // Expire the entry.
    // TODO(RyanTheOptimist): expire entries based on a timer.
    protocols_.erase(entry_it);
    return makeOptRefFromPtr<const std::vector<AlternateProtocolsCache::AlternateProtocol>>(
        nullptr);
  }
  return makeOptRef(entry.protocols_);
}

size_t AlternateProtocolsCacheImpl::size() const { return protocols_.size(); }

} // namespace Http
} // namespace Envoy
