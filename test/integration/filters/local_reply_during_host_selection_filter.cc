#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

// Upstream HTTP filter that unconditionally rejects during onHostSelected() by calling
// sendLocalReply(403). Used in integration tests to exercise the pre-decodeHeaders abort path.
class LocalReplyDuringHostSelection : public Http::PassThroughFilter,
                                      public Http::UpstreamCallbacks {
public:
  constexpr static char name[] = "local-reply-during-host-selection";

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
    if (auto cb = decoder_callbacks_->upstreamCallbacks(); cb) {
      cb->addUpstreamCallbacks(*this);
    }
  }

  void onHostSelected(Upstream::HostDescriptionConstSharedPtr) override {
    decoder_callbacks_->sendLocalReply(Http::Code::Forbidden, "host rejected", nullptr,
                                       absl::nullopt, "host_rejected_by_filter");
  }

  void onUpstreamConnectionEstablished() override {}
};

constexpr char LocalReplyDuringHostSelection::name[];
static Registry::RegisterFactory<SimpleFilterConfig<LocalReplyDuringHostSelection>,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_;

} // namespace Envoy
