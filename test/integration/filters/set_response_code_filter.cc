#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/set_response_code_filter_config.pb.h"
#include "test/integration/filters/set_response_code_filter_config.pb.validate.h"

#include "absl/strings/match.h"

namespace Envoy {

// A test filter that responds directly with a code on a prefix match.
class SetResponseCodeFilterConfig {
public:
  SetResponseCodeFilterConfig(const std::string& prefix, uint32_t code, const std::string& body,
                              Server::Configuration::FactoryContext& context)
      : prefix_(prefix), code_(code), body_(body), tls_slot_(context.threadLocal().allocateSlot()) {
  }

  const std::string prefix_;
  const uint32_t code_;
  const std::string body_;
  // Allocate a slot to validate that it is destroyed on a main thread only.
  ThreadLocal::SlotPtr tls_slot_;
};

class SetResponseCodeFilter : public Http::PassThroughFilter {
public:
  SetResponseCodeFilter(std::shared_ptr<SetResponseCodeFilterConfig> config) : config_(config) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    if (absl::StartsWith(headers.Path()->value().getStringView(), config_->prefix_)) {
      decoder_callbacks_->sendLocalReply(static_cast<Http::Code>(config_->code_), config_->body_,
                                         nullptr, absl::nullopt, "");
      return Http::FilterHeadersStatus::StopIteration;
    }
    return Http::FilterHeadersStatus::Continue;
  }

private:
  const std::shared_ptr<SetResponseCodeFilterConfig> config_;
};

class SetResponseCodeFilterFactory : public Extensions::HttpFilters::Common::FactoryBase<
                                         test::integration::filters::SetResponseCodeFilterConfig> {
public:
  SetResponseCodeFilterFactory() : FactoryBase("set-response-code-filter") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::integration::filters::SetResponseCodeFilterConfig& proto_config,
      const std::string&, Server::Configuration::FactoryContext& context) override {
    auto filter_config = std::make_shared<SetResponseCodeFilterConfig>(
        proto_config.prefix(), proto_config.code(), proto_config.body(), context);
    return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<SetResponseCodeFilter>(filter_config));
    };
  }
};

REGISTER_FACTORY(SetResponseCodeFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);
} // namespace Envoy
