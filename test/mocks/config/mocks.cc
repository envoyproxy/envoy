#include "test/mocks/config/mocks.h"

namespace Envoy {
namespace Config {

MockAdsWatch::MockAdsWatch() {}
MockAdsWatch::~MockAdsWatch() { cancel(); }

MockAdsApi::MockAdsApi() {}
MockAdsApi::~MockAdsApi() {}

AdsWatchPtr MockAdsApi::subscribe(const std::string& type_url,
                                  const std::vector<std::string>& resources,
                                  AdsCallbacks& callbacks) {
  return AdsWatchPtr(subscribe_(type_url, resources, callbacks));
}

} // namespace Config
} // namespace Envoy
