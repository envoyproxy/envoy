#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/set_is_terminal_filter_config.pb.h"
#include "test/integration/filters/set_is_terminal_filter_config.pb.validate.h"

#include "absl/strings/match.h"

namespace Envoy {

// A test filter that control whether it's a terminal filter by protobuf.
class SetIsTerminalFilter : public Http::PassThroughFilter {};

class SetIsTerminalFilterFactory : public Extensions::HttpFilters::Common::FactoryBase<
                                       test::integration::filters::SetIsTerminalFilterConfig> {
public:
  SetIsTerminalFilterFactory() : FactoryBase("set-is-terminal-filter") {}

private:
  Http::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const test::integration::filters::SetIsTerminalFilterConfig&,
                                    const std::string&,
                                    Server::Configuration::FactoryContext&) override {

    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<SetIsTerminalFilter>());
    };
  }
  bool isTerminalFilterByProtoTyped(
      const test::integration::filters::SetIsTerminalFilterConfig& proto_config,
      Server::Configuration::ServerFactoryContext&) override {
    return proto_config.is_terminal_filter();
  }
};

REGISTER_FACTORY(SetIsTerminalFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);
} // namespace Envoy
