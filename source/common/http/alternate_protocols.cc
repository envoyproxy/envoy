#include "common/http/alternate_protocols.h"

namespace Envoy {
namespace Http {

AlternateProtocols::AlternateProtocol::AlternateProtocol(absl::string_view alpn,
                                                         absl::string_view hostname, int port)
    : alpn_(alpn), hostname_(hostname), port_(port) {}

AlternateProtocols::Origin::Origin(absl::string_view scheme, absl::string_view hostname, int port)
    : scheme_(scheme), hostname_(hostname), port_(port) {}

AlternateProtocols::AlternateProtocols(TimeSource& time_source) : time_source_(time_source) {}

void AlternateProtocols::setAlternatives(const Origin& origin,
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

OptRef<const std::vector<AlternateProtocols::AlternateProtocol>>
AlternateProtocols::findAlternatives(const Origin& origin) {
  auto entry_it = protocols_.find(origin);
  if (entry_it == protocols_.end()) {
    return makeOptRefFromPtr<const std::vector<AlternateProtocols::AlternateProtocol>>(nullptr);
  }

  const Entry& entry = entry_it->second;
  if (time_source_.monotonicTime() > entry.expiration_) {
    // Expire the entry.
    protocols_.erase(entry_it);
    return makeOptRefFromPtr<const std::vector<AlternateProtocols::AlternateProtocol>>(nullptr);
  }
  return makeOptRef(entry.protocols_);
}

size_t AlternateProtocols::size() const { return protocols_.size(); }

} // namespace Http
} // namespace Envoy
