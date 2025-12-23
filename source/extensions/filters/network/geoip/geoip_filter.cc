#include "source/extensions/filters/network/geoip/geoip_filter.h"

#include "envoy/extensions/filters/network/geoip/v3/geoip.pb.h"

#include "source/common/common/assert.h"
#include "source/common/json/json_loader.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Geoip {

ProtobufTypes::MessagePtr GeoipInfo::serializeAsProto() const {
  auto proto_struct = std::make_unique<Protobuf::Struct>();
  auto& proto_fields = *proto_struct->mutable_fields();
  for (const auto& [key, value] : fields_) {
    proto_fields[key] = ValueUtil::stringValue(value);
  }
  return proto_struct;
}

absl::optional<std::string> GeoipInfo::serializeAsString() const {
  auto proto_struct = serializeAsProto();
  return Json::Factory::loadFromProtobufStruct(dynamic_cast<const Protobuf::Struct&>(*proto_struct))
      ->asJsonString();
}

StreamInfo::FilterState::Object::FieldType GeoipInfo::getField(absl::string_view field_name) const {
  auto it = fields_.find(field_name);
  if (it != fields_.end()) {
    return absl::string_view(it->second);
  }
  return absl::monostate{};
}

GeoipFilterConfig::GeoipFilterConfig(const envoy::extensions::filters::network::geoip::v3::Geoip&,
                                     const std::string& stat_prefix, Stats::Scope& scope,
                                     Formatter::FormatterConstSharedPtr client_ip_formatter)
    : scope_(scope), stat_name_set_(scope.symbolTable().makeSet("Geoip")),
      stats_prefix_(stat_name_set_->add(stat_prefix + "geoip")),
      client_ip_formatter_(std::move(client_ip_formatter)) {
  stat_name_set_->rememberBuiltin("total");
}

void GeoipFilterConfig::incCounter(Stats::StatName name) {
  Stats::SymbolTable::StoragePtr storage = scope_.symbolTable().join({stats_prefix_, name});
  scope_.counterFromStatName(Stats::StatName(storage.get())).inc();
}

GeoipFilter::GeoipFilter(GeoipFilterConfigSharedPtr config, Geolocation::DriverSharedPtr driver)
    : config_(std::move(config)), driver_(std::move(driver)) {}

Network::FilterStatus GeoipFilter::onNewConnection() {
  ASSERT(driver_, "No driver is available to perform geolocation lookup.");

  Network::Address::InstanceConstSharedPtr remote_address;

  // Check if a client IP formatter is configured for dynamic extraction.
  const auto& formatter = config_->clientIpFormatter();
  if (formatter != nullptr) {
    // Format the client IP using the configured formatter.
    const std::string ip_string = formatter->format({}, read_callbacks_->connection().streamInfo());

    if (!ip_string.empty() && ip_string != "-") {
      remote_address = Network::Utility::parseInternetAddressNoThrow(ip_string);
      if (remote_address != nullptr) {
        ENVOY_LOG(debug, "geoip: using client IP '{}' from configured formatter", ip_string);
      } else {
        ENVOY_LOG(debug,
                  "geoip: failed to parse IP address '{}' from configured formatter, "
                  "falling back to connection remote address",
                  ip_string);
      }
    } else {
      ENVOY_LOG(debug, "geoip: formatter returned empty result, falling back to connection remote "
                       "address");
    }
  }

  // Fall back to the downstream connection remote address if no formatter override is available.
  if (remote_address == nullptr) {
    remote_address = read_callbacks_->connection().connectionInfoProvider().remoteAddress();
  }

  // Capture weak_ptr to GeoipFilter so that filter can be safely accessed in the posted callback.
  // This protects against the case when filter gets deleted before the callback is run
  // (e.g., on LDS update).
  GeoipFilterWeakPtr self = weak_from_this();
  driver_->lookup(Geolocation::LookupRequest{std::move(remote_address)},
                  [self, &dispatcher = read_callbacks_->connection().dispatcher()](
                      Geolocation::LookupResult&& result) {
                    dispatcher.post([self, result = std::move(result)]() mutable {
                      if (GeoipFilterSharedPtr filter = self.lock()) {
                        filter->onLookupComplete(std::move(result));
                      }
                    });
                  });

  return Network::FilterStatus::Continue;
}

void GeoipFilter::onLookupComplete(Geolocation::LookupResult&& result) {
  if (result.empty()) {
    ENVOY_LOG(debug, "geoip: no geolocation data found");
    config_->incTotal();
    return;
  }

  auto geoip_info = std::make_shared<GeoipInfo>();
  for (const auto& [key, value] : result) {
    if (!value.empty()) {
      geoip_info->setField(key, value);
    }
  }

  if (!geoip_info->empty()) {
    read_callbacks_->connection().streamInfo().filterState()->setData(
        std::string(GeoipFilterStateKey), std::move(geoip_info),
        StreamInfo::FilterState::StateType::ReadOnly,
        StreamInfo::FilterState::LifeSpan::Connection);
    ENVOY_LOG(debug, "geoip: stored data in filter state key '{}'", GeoipFilterStateKey);
  }

  config_->incTotal();
}

} // namespace Geoip
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
