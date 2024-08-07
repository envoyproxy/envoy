#include "source/common/http/http_server_properties_cache_impl.h"

#include <memory>

#include "source/common/common/logger.h"
#include "source/common/http/http3_status_tracker_impl.h"

#include "quiche/http2/core/spdy_alt_svc_wire_format.h"
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
HttpServerPropertiesCacheImpl::originToString(const HttpServerPropertiesCache::Origin& origin) {
  return absl::StrCat(origin.scheme_, "://", origin.hostname_, ":", origin.port_);
}

absl::optional<HttpServerPropertiesCache::Origin>
HttpServerPropertiesCacheImpl::stringToOrigin(const std::string& str) {
  const re2::RE2& origin_regex = ConstRegexHolder::get().origin_regex;
  std::string scheme;
  std::string hostname;
  int port = 0;
  if (re2::RE2::FullMatch(str.c_str(), origin_regex, &scheme, &hostname, &port)) {
    return HttpServerPropertiesCache::Origin(scheme, hostname, port);
  }
  return {};
}

std::string HttpServerPropertiesCacheImpl::originDataToStringForCache(const OriginData& data) {
  std::string value;
  if (!data.protocols.has_value() || data.protocols->empty()) {
    value = "clear";
  } else {
    for (auto& protocol : *data.protocols) {
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
  }
  absl::StrAppend(&value, "|", data.srtt.count(), "|", data.concurrent_streams);
  return value;
}

absl::optional<HttpServerPropertiesCacheImpl::OriginData>
HttpServerPropertiesCacheImpl::originDataFromString(absl::string_view origin_data_string,
                                                    TimeSource& time_source, bool from_cache) {
  const std::vector<absl::string_view> parts = absl::StrSplit(origin_data_string, '|');
  if (parts.size() != 3) {
    return {};
  }

  OriginData data;
  data.protocols = alternateProtocolsFromString(parts[0], time_source, from_cache);

  int64_t srtt;
  if (!absl::SimpleAtoi(parts[1], &srtt)) {
    return {};
  }
  data.srtt = std::chrono::microseconds(srtt);

  int32_t concurrency;
  if (!absl::SimpleAtoi(parts[2], &concurrency)) {
    return {};
  }
  data.concurrent_streams = concurrency;

  return data;
}

std::vector<Http::HttpServerPropertiesCache::AlternateProtocol>
HttpServerPropertiesCacheImpl::alternateProtocolsFromString(absl::string_view altsvc_str,
                                                            TimeSource& time_source,
                                                            bool from_cache) {
  spdy::SpdyAltSvcWireFormat::AlternativeServiceVector altsvc_vector;
  if (!spdy::SpdyAltSvcWireFormat::ParseHeaderFieldValue(altsvc_str, &altsvc_vector)) {
    return {};
  }
  std::vector<Http::HttpServerPropertiesCache::AlternateProtocol> results;
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

HttpServerPropertiesCacheImpl::HttpServerPropertiesCacheImpl(
    Event::Dispatcher& dispatcher, std::vector<std::string>&& canonical_suffixes,
    std::unique_ptr<KeyValueStore>&& key_value_store, size_t max_entries)
    : dispatcher_(dispatcher), canonical_suffixes_(canonical_suffixes),
      max_entries_(max_entries > 0 ? max_entries : 1024) {
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
        if (origin_data->protocols.has_value()) {
          protocols = *origin_data->protocols;
        }
        OriginDataWithOptRef data(protocols, origin_data->srtt, nullptr,
                                  origin_data->concurrent_streams);
        setPropertiesImpl(*origin, data);
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

HttpServerPropertiesCacheImpl::~HttpServerPropertiesCacheImpl() = default;

void HttpServerPropertiesCacheImpl::setAlternatives(const Origin& origin,
                                                    std::vector<AlternateProtocol>& protocols) {
  OriginDataWithOptRef data;
  data.protocols = protocols;
  auto it = setPropertiesImpl(origin, data);
  if (key_value_store_) {
    key_value_store_->addOrUpdate(originToString(origin), originDataToStringForCache(it->second),
                                  absl::nullopt);
  }
}

void HttpServerPropertiesCacheImpl::setSrtt(const Origin& origin, std::chrono::microseconds srtt) {
  OriginDataWithOptRef data;
  data.srtt = srtt;
  auto it = setPropertiesImpl(origin, data);
  if (key_value_store_) {
    key_value_store_->addOrUpdate(originToString(origin), originDataToStringForCache(it->second),
                                  absl::nullopt);
  }
}

std::chrono::microseconds HttpServerPropertiesCacheImpl::getSrtt(const Origin& origin) const {
  auto entry_it = protocols_.find(origin);
  if (entry_it == protocols_.end()) {
    return std::chrono::microseconds(0);
  }
  return entry_it->second.srtt;
}

void HttpServerPropertiesCacheImpl::setConcurrentStreams(const Origin& origin,
                                                         uint32_t concurrent_streams) {
  OriginDataWithOptRef data;
  data.concurrent_streams = concurrent_streams;
  auto it = setPropertiesImpl(origin, data);
  if (key_value_store_) {
    key_value_store_->addOrUpdate(originToString(origin), originDataToStringForCache(it->second),
                                  absl::nullopt);
  }
}

uint32_t HttpServerPropertiesCacheImpl::getConcurrentStreams(const Origin& origin) const {
  auto entry_it = protocols_.find(origin);
  if (entry_it == protocols_.end()) {
    return 0;
  }
  return entry_it->second.concurrent_streams;
}

HttpServerPropertiesCacheImpl::ProtocolsMap::iterator
HttpServerPropertiesCacheImpl::setPropertiesImpl(const Origin& origin,
                                                 OriginDataWithOptRef& origin_data) {
  if (origin_data.protocols.has_value()) {
    maybeSetCanonicalOrigin(origin);
    std::vector<AlternateProtocol>& protocols = *origin_data.protocols;
    static const size_t max_protocols = 10;
    if (protocols.size() > max_protocols) {
      ENVOY_LOG_MISC(trace, "Too many alternate protocols: {}, truncating", protocols.size());
      protocols.erase(protocols.begin() + max_protocols, protocols.end());
    }
  }
  auto entry_it = protocols_.find(origin);
  if (entry_it != protocols_.end()) {
    if (origin_data.protocols.has_value()) {
      entry_it->second.protocols = *origin_data.protocols;
    }
    if (origin_data.srtt.count()) {
      entry_it->second.srtt = origin_data.srtt;
    }
    if (origin_data.h3_status_tracker) {
      entry_it->second.h3_status_tracker = std::move(origin_data.h3_status_tracker);
    }

    return entry_it;
  }
  return addOriginData(origin,
                       {origin_data.protocols, origin_data.srtt,
                        std::move(origin_data.h3_status_tracker), origin_data.concurrent_streams});
}

HttpServerPropertiesCacheImpl::ProtocolsMap::iterator
HttpServerPropertiesCacheImpl::addOriginData(const Origin& origin, OriginData&& origin_data) {
  ASSERT(protocols_.find(origin) == protocols_.end());
  while (protocols_.size() >= max_entries_) {
    auto iter = protocols_.begin();
    if (key_value_store_) {
      key_value_store_->remove(originToString(iter->first));
    }
    protocols_.erase(iter);
  }
  protocols_[origin] = std::move(origin_data);
  return protocols_.find(origin);
}

OptRef<const std::vector<HttpServerPropertiesCache::AlternateProtocol>>
HttpServerPropertiesCacheImpl::findAlternatives(const Origin& origin) {
  auto entry_it = protocols_.find(origin);
  if (entry_it == protocols_.end() || !entry_it->second.protocols.has_value()) {
    absl::optional<Origin> canonical = getCanonicalOrigin(origin.hostname_);
    if (canonical.has_value()) {
      entry_it = protocols_.find(*canonical);
    }
    if (entry_it == protocols_.end() || !entry_it->second.protocols.has_value()) {
      return makeOptRefFromPtr<const std::vector<AlternateProtocol>>(nullptr);
    }
  }
  std::vector<AlternateProtocol>& protocols = *entry_it->second.protocols;

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
                                  originDataToStringForCache(entry_it->second), absl::nullopt);
  }
  return makeOptRef(const_cast<const std::vector<AlternateProtocol>&>(protocols));
}

size_t HttpServerPropertiesCacheImpl::size() const { return protocols_.size(); }

HttpServerPropertiesCache::Http3StatusTracker&
HttpServerPropertiesCacheImpl::getOrCreateHttp3StatusTracker(const Origin& origin) {
  auto entry_it = protocols_.find(origin);
  if (entry_it != protocols_.end()) {
    if (entry_it->second.h3_status_tracker == nullptr) {
      entry_it->second.h3_status_tracker = std::make_unique<Http3StatusTrackerImpl>(dispatcher_);
    }
    return *entry_it->second.h3_status_tracker;
  }

  OriginDataWithOptRef data;
  data.h3_status_tracker = std::make_unique<Http3StatusTrackerImpl>(dispatcher_);
  auto it = setPropertiesImpl(origin, data);
  return *it->second.h3_status_tracker;
}

void HttpServerPropertiesCacheImpl::resetBrokenness() {
  for (auto& protocol : protocols_) {
    if (protocol.second.h3_status_tracker && protocol.second.h3_status_tracker->isHttp3Broken()) {
      protocol.second.h3_status_tracker->markHttp3FailedRecently();
    }
  }
}

absl::string_view HttpServerPropertiesCacheImpl::getCanonicalSuffix(absl::string_view hostname) {
  for (const std::string& suffix : canonical_suffixes_) {
    if (absl::EndsWith(hostname, suffix)) {
      return suffix;
    }
  }
  return "";
}

absl::optional<HttpServerPropertiesCache::Origin>
HttpServerPropertiesCacheImpl::getCanonicalOrigin(absl::string_view hostname) {
  absl::string_view suffix = getCanonicalSuffix(hostname);
  if (suffix.empty()) {
    return {};
  }

  auto it = canonical_alt_svc_map_.find(std::string(suffix));
  if (it == canonical_alt_svc_map_.end()) {
    return {};
  }
  return it->second;
}

void HttpServerPropertiesCacheImpl::maybeSetCanonicalOrigin(const Origin& origin) {
  absl::string_view suffix = getCanonicalSuffix(origin.hostname_);
  if (suffix.empty()) {
    return;
  }

  canonical_alt_svc_map_[std::string(suffix)] = origin;
}

} // namespace Http
} // namespace Envoy
