#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/add_header_temp_filter_config.pb.h"
#include "test/integration/filters/add_header_temp_filter_config.pb.validate.h"

#include "absl/strings/match.h"

namespace Envoy {

class AddHeaderTempFilter : public Http::PassThroughFilter {
public:
  AddHeaderTempFilter(std::string key, std::string val) : key_(key), val_(val) {}
  AddHeaderTempFilter() = default;

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    headers.addCopy(Http::LowerCaseString(key_), val_);
    return Http::FilterHeadersStatus::Continue;
  }

private:
  std::string key_;
  std::string val_;
};

class AddHeaderTempFilterFactory : public Server::Configuration::UpstreamHttpFilterConfigFactory {
public:
    std::string name() const override { return "envoy.test.add_header_upstream"; }

    ProtobufTypes::MessagePtr createEmptyConfigProto() override {
      return std::make_unique<test::integration::filters::AddHeaderTempFilterConfig>();
    }

    Http::FilterFactoryCb
      createFilterFactoryFromProto(const Protobuf::Message& config, const std::string&,
                               Server::Configuration::UpstreamHttpFactoryContext& context) override {

      const auto& proto_config = MessageUtil::downcastAndValidate<
          const test::integration::filters::AddHeaderTempFilterConfig&>(
          config, context.getServerFactoryContext().messageValidationVisitor());

      return [proto_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamFilter(std::make_shared<AddHeaderTempFilter>(proto_config.header_key(), proto_config.header_val()));
      };
    };
};

static Registry::RegisterFactory<AddHeaderTempFilterFactory,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_;

} // namespace Envoy
