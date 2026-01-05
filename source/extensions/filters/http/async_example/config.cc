#include "source/extensions/filters/http/async_example/v3/async_example.pb.h"
#include "source/extensions/filters/http/async_example/v3/async_example.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/async_example/async_example.h"
#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AsyncExample {

class AsyncExampleFilterFactory
    : public Envoy::Extensions::HttpFilters::Common::FactoryBase<
          envoy::extensions::filters::http::async_example::v3::AsyncExample> {
public:
  AsyncExampleFilterFactory() : FactoryBase("envoy.filters.http.async_example") {}

  std::string name() const override { return "envoy.filters.http.async_example"; }
  std::string category() const override { return "envoy.filters.http"; }

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::async_example::v3::AsyncExample& proto_config,
      const std::string&, Server::Configuration::FactoryContext& /*context*/) override {
    auto config = std::make_shared<AsyncExampleConfig>(proto_config);

    return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamDecoderFilter(std::make_shared<AsyncExampleFilter>(
          config, callbacks.dispatcher()));
    };
  }
};

REGISTER_FACTORY(AsyncExampleFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace AsyncExample
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
