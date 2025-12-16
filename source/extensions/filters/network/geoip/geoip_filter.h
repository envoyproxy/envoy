#pragma once

#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/network/geoip/v3/geoip.pb.h"
#include "envoy/geoip/geoip_provider_driver.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/common/logger.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Geoip {

// Well-known filter state key for storing geolocation data.
// See docs/root/configuration/advanced/well_known_filter_state.rst for documentation.
constexpr absl::string_view GeoipFilterStateKey = "envoy.geoip";

/**
 * FilterState object that stores geolocation lookup results.
 */
class GeoipInfo : public StreamInfo::FilterState::Object {
public:
  GeoipInfo() = default;

  void setField(const std::string& key, const std::string& value) { fields_[key] = value; }

  absl::optional<std::string> getGeoField(absl::string_view key) const {
    auto it = fields_.find(key);
    if (it != fields_.end()) {
      return it->second;
    }
    return absl::nullopt;
  }

  bool empty() const { return fields_.empty(); }
  size_t size() const { return fields_.size(); }

  // FilterState::Object
  ProtobufTypes::MessagePtr serializeAsProto() const override;
  absl::optional<std::string> serializeAsString() const override;
  bool hasFieldSupport() const override { return true; }
  FieldType getField(absl::string_view field_name) const override;

private:
  absl::flat_hash_map<std::string, std::string> fields_;
};

/**
 * Configuration for the network GeoIP filter.
 */
class GeoipFilterConfig {
public:
  GeoipFilterConfig(const envoy::extensions::filters::network::geoip::v3::Geoip& config,
                    const std::string& stat_prefix, Stats::Scope& scope);

  void incTotal() { incCounter(stat_name_set_->getBuiltin("total", unknown_hit_)); }

private:
  void incCounter(Stats::StatName name);

  Stats::Scope& scope_;
  Stats::StatNameSetPtr stat_name_set_;
  const Stats::StatName stats_prefix_;
  const Stats::StatName unknown_hit_;
};

using GeoipFilterConfigSharedPtr = std::shared_ptr<GeoipFilterConfig>;

// Forward declaration for weak_ptr/shared_ptr usage.
class GeoipFilter;
using GeoipFilterWeakPtr = std::weak_ptr<GeoipFilter>;
using GeoipFilterSharedPtr = std::shared_ptr<GeoipFilter>;

/**
 * Network filter that performs geolocation lookups and stores results in filter state.
 */
class GeoipFilter : public Network::ReadFilter,
                    public Logger::Loggable<Logger::Id::filter>,
                    public std::enable_shared_from_this<GeoipFilter> {
public:
  GeoipFilter(GeoipFilterConfigSharedPtr config, Geolocation::DriverSharedPtr driver);

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance&, bool) override {
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

  // Callback for geolocation lookup completion.
  void onLookupComplete(Geolocation::LookupResult&& result);

private:
  GeoipFilterConfigSharedPtr config_;
  Geolocation::DriverSharedPtr driver_;
  Network::ReadFilterCallbacks* read_callbacks_{};
};

} // namespace Geoip
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
