#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

// A filter that only allows decodeData() to be called once with fixed data length.
class DecodeDynamicMetadataFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "decode-dynamic-metadata-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& header_map, bool) override {
    for (auto const& [filter, kvs] :
         decoder_callbacks_->connection()->streamInfo().dynamicMetadata().filter_metadata()) {
      for (auto const& [k, v] : kvs.fields()) {
        std::string value;
        switch (v.kind_case()) {
        case ProtobufWkt::Value::kNumberValue:
          value = fmt::format("{:g}", v.number_value());
          break;
        case ProtobufWkt::Value::kStringValue:
          value = v.string_value();
          break;
        case ProtobufWkt::Value::kBoolValue:
          value = v.bool_value() ? "true" : "false";
          break;
        default:
          // Unsupported type or null value.
          break;
        }
        std::string key = filter + "." + k;
        header_map.addCopy(Http::LowerCaseString(key), value);
      }
    }
    return Http::FilterHeadersStatus::Continue;
  }
};

constexpr char DecodeDynamicMetadataFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<DecodeDynamicMetadataFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
