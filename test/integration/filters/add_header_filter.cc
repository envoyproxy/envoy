

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/add_header_filter.pb.h"
#include "test/integration/filters/add_header_filter.pb.validate.h"
#include "test/integration/filters/common.h"

namespace Envoy {

class AddHeaderFilter : public Http::PassThroughFilter {
public:
  static constexpr absl::string_view kHeader = "x-header-to-add";
  AddHeaderFilter() = default;
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    headers.addCopy(Http::LowerCaseString(kHeader), "value");
    return Http::FilterHeadersStatus::Continue;
  }
};

class AddHeaderFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpDualFilterConfig {
public:
  AddHeaderFilterConfig() : EmptyHttpDualFilterConfig("add-header-filter") {}
  absl::StatusOr<Http::FilterFactoryCb>
  createDualFilter(const std::string&, Server::Configuration::ServerFactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::AddHeaderFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<AddHeaderFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
static Registry::RegisterFactory<AddHeaderFilterConfig,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_;

class AddConfigurableHeaderFilter : public Http::PassThroughFilter {
public:
  AddConfigurableHeaderFilter(const std::string& header_key, const std::string& header_value)
      : header_key_(header_key), header_value_(header_value) {}

  AddConfigurableHeaderFilter() = default;

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    headers.appendCopy(Http::LowerCaseString(header_key_), header_value_);
    return Http::FilterHeadersStatus::Continue;
  }

private:
  const std::string header_key_;
  const std::string header_value_;
};

class AddConfigurableHeaderFilterFactory
    : public Server::Configuration::UpstreamHttpFilterConfigFactory {
public:
  std::string name() const override { return "envoy.test.add_header_upstream"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<test::integration::filters::AddHeaderFilterConfig>();
  }

  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& config, const std::string&,
                               Server::Configuration::UpstreamFactoryContext& context) override {

    const auto& proto_config =
        MessageUtil::downcastAndValidate<const test::integration::filters::AddHeaderFilterConfig&>(
            config, context.serverFactoryContext().messageValidationVisitor());

    return [proto_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<AddConfigurableHeaderFilter>(
          proto_config.header_key(), proto_config.header_value()));
    };
  };
};

static Registry::RegisterFactory<AddConfigurableHeaderFilterFactory,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_add_header_filter_;

} // namespace Envoy
