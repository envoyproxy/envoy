#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

// A test filter registration that adds an access logger.
class AccessLogFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  SimpleFilterConfig() : EmptyHttpFilterConfig("access-log-filter") {}

  Http::FilterFactoryCb createFilter(const std::string&,
                                     Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addAccessLog(std::make_shared < NiceMock<AccessLog::MockInstance>());
    };
  }
};
