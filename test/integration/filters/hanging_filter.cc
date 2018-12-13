#include <string>

#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/test_common/test_time_system.h"

namespace Envoy {

// A test filter that hangs unendingly during the decode/encode phase. This filter simulates a
// slow/hanging filter that causes the thread executing the filter callbacks to block.
class HangingFilter : public Http::PassThroughFilter {
public:
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) {
    mutex_.Lock();
    while (true) {
      cond_var_.Wait(&mutex_);
    }

    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) {
    mutex_.Lock();
    while (true) {
      cond_var_.Wait(&mutex_);
    }

    NOT_REACHED_GCOVR_EXCL_LINE;
  }

private:
  absl::Mutex mutex_;
  absl::CondVar cond_var_;
};

class HangingFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  HangingFilterConfig() : EmptyHttpFilterConfig("hanging-filter") {}

  Http::FilterFactoryCb createFilter(const std::string&, Server::Configuration::FactoryContext&) {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<HangingFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<HangingFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
