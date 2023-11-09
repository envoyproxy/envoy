#include "test/integration/filters/process_context_filter.h"

#include <memory>
#include <string>

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {

// A test filter that rejects all requests if the ProcessObject held by the
// ProcessContext is unhealthy, and responds OK to all requests otherwise.
class ProcessContextFilter : public Http::PassThroughFilter {
public:
  ProcessContextFilter(ProcessContext& process_context)
      : process_object_(dynamic_cast<ProcessObjectForFilter&>(process_context.get())) {}
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    if (!process_object_.isHealthy()) {
      decoder_callbacks_->sendLocalReply(Envoy::Http::Code::InternalServerError,
                                         "ProcessObjectForFilter is unhealthy", nullptr,
                                         absl::nullopt, "");
      return Http::FilterHeadersStatus::StopIteration;
    }
    decoder_callbacks_->sendLocalReply(Envoy::Http::Code::OK, "ProcessObjectForFilter is healthy",
                                       nullptr, absl::nullopt, "");
    return Http::FilterHeadersStatus::StopIteration;
  }

private:
  ProcessObjectForFilter& process_object_;
};

class ProcessContextFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  ProcessContextFilterConfig() : EmptyHttpFilterConfig("process-context-filter") {}

  absl::StatusOr<Http::FilterFactoryCb>
  createFilter(const std::string&,
               Server::Configuration::FactoryContext& factory_context) override {
    return [&factory_context](Http::FilterChainFactoryCallbacks& callbacks) {
      callbacks.addStreamFilter(
          std::make_shared<ProcessContextFilter>(*factory_context.processContext()));
    };
  }
};

static Registry::RegisterFactory<ProcessContextFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
