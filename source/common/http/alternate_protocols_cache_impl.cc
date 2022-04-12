#include "source/common/http/alternate_protocols_cache_impl.h"

#include <memory>

#include "source/common/common/logger.h"

#include "http3_status_tracker_impl.h"
#include "quiche/spdy/core/spdy_alt_svc_wire_format.h"
#include "re2/re2.h"

namespace Envoy {
namespace Http {
namespace {

struct RegexHolder {
  RegexHolder() : origin_regex("(.*)://(.*):(\\d+)") {}

  const re2::RE2 origin_regex;
};

using ConstRegexHolder = ConstSingleton<RegexHolder>;

} // namespace

std::string
AlternateProtocolsCacheImpl::originToString(const AlternateProtocolsCache::Origin& origin) {
  return absl::StrCat(origin.scheme_, "://", origin.hostname_, ":", origin.port_);
}

absl::optional<AlternateProtocolsCache::Origin>
AlternateProtocolsCacheImpl::stringToOrigin(const std::string& str) {
  const re2::RE2& origin_regex = ConstRegexHolder::get().origin_regex;
  std::string scheme;
  std::string hostname;
  int port = 0;
  if (re2::RE2::FullMatch(str.c_str(), origin_regex, &scheme, &hostname, &port)) {
    return AlternateProtocolsCache::Origin(scheme, hostname, port);
  }
  return {};
}

std::string AlternateProtocolsCacheImpl::originDataToStringForCache(const OriginData& data) {
  if (!data.protocols.has_value() || data.protocols.value().empty()) {
    return absl::StrCat("clear|", data.srtt.count());
  }
  std::string value;
  for (auto& protocol : data.protocols.value()) {
    if (!value.empty()) {
      value.push_back(',');
    }
    absl::StrAppend(&value, protocol.alpn_, "=\"", protocol.hostname_, ":", protocol.port_, "\"");
    // Note this is _not_ actually the max age, but the absolute time at which
    // this entry will expire. protocolsFromString will convert back to ma.
    absl::StrAppend(
        &value, "; ma=",
        std::chrono::duration_cast<std::chrono::seconds>(protocol.expiration_.time_since_epoch())
            .count());
  }
  absl::StrAppend(&value, "|", data.srtt.count());
  return value;
}

absl::optional<AlternateProtocolsCacheImpl::OriginData>
AlternateProtocolsCacheImpl::originDataFromString(absl::string_view origin_data_string,
                                                  TimeSource& time_source, bool from_cache) {
  OriginData data;
  const std::vector<absl::string_view> parts = absl::StrSplit(origin_data_string, '|');
  if (parts.size() == 2) {
    int64_t srtt;
    if (!absl::SimpleAtoi(parts[1], &srtt)) {
      return {};
    }
    data.srtt = std::chrono::microseconds(srtt);
  } else if (parts.size() != 1) {
    return {};
  } else {
    // Handling raw alt-svc with no endpoint info
    data.srtt = std::chrono::microseconds(0);
  }
  data.protocols = alternateProtocolsFromString(parts[0], time_source, from_cache);
  return data;
}

std::vector<Http::AlternateProtocolsCache::AlternateProtocol>
AlternateProtocolsCacheImpl::alternateProtocolsFromString(absl::string_view altsvc_str,
                                                          TimeSource& time_source,
                                                          bool from_cache) {
  spdy::SpdyAltSvcWireFormat::AlternativeServiceVector altsvc_vector;
  if (!spdy::SpdyAltSvcWireFormat::ParseHeaderFieldValue(altsvc_str, &altsvc_vector)) {
    return {};
  }
  std::vector<Http::AlternateProtocolsCache::AlternateProtocol> results;
  for (const auto& alt_svc : altsvc_vector) {
    MonotonicTime expiration;
    if (from_cache) {
      auto expire_time_from_epoch = std::chrono::seconds(alt_svc.max_age_seconds);
      auto time_since_epoch = std::chrono::duration_cast<std::chrono::seconds>(
          time_source.monotonicTime().time_since_epoch());
      if (expire_time_from_epoch < time_since_epoch) {
        expiration = time_source.monotonicTime();
      } else {
        expiration = time_source.monotonicTime() + (expire_time_from_epoch - time_since_epoch);
      }
    } else {
      expiration = time_source.monotonicTime() + std::chrono::seconds(alt_svc.max_age_seconds);
    }
    results.emplace_back(alt_svc.protocol_id, alt_svc.host, alt_svc.port, expiration);
  }
  return results;
}

AlternateProtocolsCacheImpl::AlternateProtocolsCacheImpl(
    Event::Dispatcher& dispatcher, std::unique_ptr<KeyValueStore>&& key_value_store,
    size_t max_entries)
    : dispatcher_(dispatcher), max_entries_(max_entries > 0 ? max_entries : 1024) {
  if (key_value_store) {
    KeyValueStore::ConstIterateCb load_protocols = [this](const std::string& key,
                                                          const std::string& value) {
      absl::optional<OriginData> origin_data =
          originDataFromString(value, dispatcher_.timeSource(), true);
      absl::optional<Origin> origin = stringToOrigin(key);
      if (origin_data.has_value() && origin.has_value()) {
        // We deferred transfering ownership into key_value_store_ prior, so
        // that we won't end up doing redundant updates to the store while
        // iterating.
        OptRef<std::vector<AlternateProtocol>> protocols;
        if (origin_data.value().protocols.has_value()) {
          protocols = origin_data.value().protocols.value();
        }
        OriginDataWithOptRef data{protocols, origin_data.value().srtt, nullptr};
        setAlternativesImpl(origin.value(), data);
      } else {
        ENVOY_LOG(warn,
                  fmt::format("Unable to parse cache entry with key: {} value: {}", key, value));
      }
      return KeyValueStore::Iterate::Continue;
    };
    key_value_store->iterate(load_protocols);
    key_value_store_ = std::move(key_value_store);
  }
}

AlternateProtocolsCacheImpl::~AlternateProtocolsCacheImpl() = default;

void AlternateProtocolsCacheImpl::setAlternatives(const Origin& origin,
                                                  std::vector<AlternateProtocol>& protocols) {
  OriginDataWithOptRef data;
  data.protocols = protocols;
  auto it = setAlternativesImpl(origin, data);
  if (key_value_store_) {
    key_value_store_->addOrUpdate(originToString(origin), originDataToStringForCache(it->second));
  }
}

void AlternateProtocolsCacheImpl::setSrtt(const Origin& origin, std::chrono::microseconds srtt) {
  setSrttImpl(origin, srtt);
}

void AlternateProtocolsCacheImpl::setSrttImpl(const Origin& origin,
                                              std::chrono::microseconds srtt) {
  OriginDataWithOptRef data;
  data.srtt = srtt;
  auto it = setAlternativesImpl(origin, data);
  if (key_value_store_) {
    key_value_store_->addOrUpdate(originToString(origin), originDataToStringForCache(it->second));
  }
}

std::chrono::microseconds AlternateProtocolsCacheImpl::getSrtt(const Origin& origin) const {
  auto entry_it = protocols_.find(origin);
  if (entry_it == protocols_.end()) {
    return std::chrono::microseconds(0);
  }
  return entry_it->second.srtt;
}

AlternateProtocolsCacheImpl::ProtocolsMap::iterator
AlternateProtocolsCacheImpl::setAlternativesImpl(const Origin& origin,
                                                 OptRef<std::vector<AlternateProtocol>> protocols,
                                                 std::chrono::microseconds srtt,
                                                 Http3StatusTrackerPtr&& tracker) {
  OriginDataWithOptRef data{protocols, srtt, std::move(tracker)};
  return setAlternativesImpl(origin, data);
}

AlternateProtocolsCacheImpl::ProtocolsMap::iterator
AlternateProtocolsCacheImpl::setAlternativesImpl(const Origin& origin,
                                                 OriginDataWithOptRef& origin_data) {
  if (origin_data.protocols.has_value()) {
    std::vector<AlternateProtocol>& p = origin_data.protocols.value().get();
    static const size_t max_protocols = 10;
    if (p.size() > max_protocols) {
      ENVOY_LOG_MISC(trace, "Too many alternate protocols: {}, truncating", p.size());
      p.erase(p.begin() + max_protocols, p.end());
    }
  }
  auto entry_it = protocols_.find(origin);
  if (entry_it != protocols_.end()) {
    if (origin_data.protocols.has_value()) {
      entry_it->second.protocols = origin_data.protocols.value().get();
    }
    if (origin_data.srtt.count()) {
      entry_it->second.srtt = origin_data.srtt;
    }
    if (origin_data.h3_status_tracker) {
      entry_it->second.h3_status_tracker = std::move(origin_data.h3_status_tracker);
    }

    return entry_it;
  }
  return addOriginData(origin, OriginData{origin_data.protocols, origin_data.srtt,
                                          std::move(origin_data.h3_status_tracker)});
}

AlternateProtocolsCacheImpl::ProtocolsMap::iterator
AlternateProtocolsCacheImpl::addOriginData(const Origin& origin, OriginData&& origin_data) {
  ASSERT(protocols_.find(origin) == protocols_.end());
  while (protocols_.size() >= max_entries_) {
    auto iter = protocols_.begin();
    key_value_store_->remove(originToString(iter->first));
    protocols_.erase(iter);
  }
  protocols_[origin] = std::move(origin_data);
  return protocols_.find(origin);
}

OptRef<const std::vector<AlternateProtocolsCache::AlternateProtocol>>
AlternateProtocolsCacheImpl::findAlternatives(const Origin& origin) {
  auto entry_it = protocols_.find(origin);
  if (entry_it == protocols_.end() || !entry_it->second.protocols.has_value()) {
    return makeOptRefFromPtr<const std::vector<AlternateProtocol>>(nullptr);
  }
  std::vector<AlternateProtocol>& protocols = entry_it->second.protocols.value();

  auto original_size = protocols.size();
  const MonotonicTime now = dispatcher_.timeSource().monotonicTime();
  protocols.erase(std::remove_if(protocols.begin(), protocols.end(),
                                 [now](const AlternateProtocol& protocol) {
                                   return (now > protocol.expiration_);
                                 }),
                  protocols.end());

  if (protocols.empty()) {
    if (key_value_store_) {
      key_value_store_->remove(originToString(origin));
    }
    return makeOptRefFromPtr<const std::vector<AlternateProtocol>>(nullptr);
  }
  if (key_value_store_ && original_size != protocols.size()) {
    key_value_store_->addOrUpdate(originToString(origin),
                                  originDataToStringForCache(entry_it->second));
  }
  return makeOptRef(const_cast<const std::vector<AlternateProtocol>&>(protocols));
}

size_t AlternateProtocolsCacheImpl::size() const { return protocols_.size(); }

AlternateProtocolsCache::Http3StatusTracker&
AlternateProtocolsCacheImpl::getOrCreateHttp3StatusTracker(const Origin& origin) {
  auto entry_it = protocols_.find(origin);
  if (entry_it != protocols_.end()) {
    if (entry_it->second.h3_status_tracker == nullptr) {
      entry_it->second.h3_status_tracker = std::make_unique<Http3StatusTrackerImpl>(dispatcher_);
    }
    return *entry_it->second.h3_status_tracker;
  }
  auto it =
      setAlternativesImpl(origin, {}, {}, std::make_unique<Http3StatusTrackerImpl>(dispatcher_));
  return *it->second.h3_status_tracker;
}

} // namespace Http
} // namespace Envoy
