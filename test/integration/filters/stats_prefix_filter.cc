#include <string>

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/test_filters.pb.h"

#include "absl/strings/str_cat.h"

namespace Envoy {

// An upstream filter that counts its requests the way a filter migrated to the stats prefix scope
// does:
// '<statsPrefixOr()>stats_prefix_filter.rq_total' in statsPrefixScopeOr().
class StatsPrefixFilter : public Http::PassThroughFilter {
public:
  explicit StatsPrefixFilter(Stats::Counter& rq_total) : rq_total_(rq_total) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    rq_total_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

private:
  Stats::Counter& rq_total_;
};

// Unified upstream filter factory: it gets the ExtraFactoryContext that the router builds.
class StatsPrefixFilterFactory : public Server::Configuration::UpstreamHttpFilterConfigFactory {
public:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProto(
      const Protobuf::Message&, Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override {
    Stats::Counter& rq_total = extra_context.statsPrefixScopeOr(context).counterFromString(
        absl::StrCat(extra_context.statsPrefixOr(), "stats_prefix_filter.rq_total"));
    return [&rq_total](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamDecoderFilter(std::make_shared<StatsPrefixFilter>(rq_total));
    };
  }
  bool isUnifiedFilter() override { return true; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<test::integration::filters::StatsPrefixFilterConfig>();
  }
  std::string name() const override { return "stats-prefix-filter"; }
};

static Registry::RegisterFactory<StatsPrefixFilterFactory,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_;

} // namespace Envoy
