#include "common/http/alternate_protocols_cache_impl.h"

namespace Envoy {
namespace Http {

AlternateProtocolsCacheImpl::AlternateProtocolsCacheImpl(ThreadLocal::Instance& tls,
                                                         TimeSource& time_source)
    : time_source_(time_source), slot_(tls) {
  slot_.set([](Event::Dispatcher& /*dispatcher*/) { return std::make_shared<State>(); });
}

AlternateProtocolsCacheImpl::~AlternateProtocolsCacheImpl() {}

void AlternateProtocolsCacheImpl::setAlternatives(const Origin& origin,
                                                  const std::vector<AlternateProtocol>& protocols,
                                                  const MonotonicTime& expiration) {
  // TODO(RyanTheOptimist): Propagate state changes across threads.
  State& state = *(slot_.get());
  Entry& entry = state.protocols_[origin];
  if (entry.protocols_ != protocols) {
    entry.protocols_ = protocols;
  }
  if (entry.expiration_ != expiration) {
    entry.expiration_ = expiration;
  }
}

OptRef<const std::vector<AlternateProtocolsCache::AlternateProtocol>>
AlternateProtocolsCacheImpl::findAlternatives(const Origin& origin) {
  State& state = *(slot_.get());
  auto entry_it = state.protocols_.find(origin);
  if (entry_it == state.protocols_.end()) {
    return makeOptRefFromPtr<const std::vector<AlternateProtocol>>(nullptr);
  }

  const Entry& entry = entry_it->second;
  if (time_source_.monotonicTime() > entry.expiration_) {
    // Expire the entry.
    state.protocols_.erase(entry_it);
    return makeOptRefFromPtr<const std::vector<AlternateProtocol>>(nullptr);
  }
  return makeOptRef(entry.protocols_);
}

size_t AlternateProtocolsCacheImpl::size() const {
  State& state = *(slot_.get());
  return state.protocols_.size();
}

} // namespace Http
} // namespace Envoy
