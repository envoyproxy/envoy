#pragma once

#include <memory>
#include <string>

#include "common/upstream/od_cds_api_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

class MockOdCdsApi;
using MockOdCdsApiSharedPtr = std::shared_ptr<MockOdCdsApi>;

class MockOdCdsApi : public OdCdsApi {
public:
  static MockOdCdsApiSharedPtr create() { return std::make_shared<MockOdCdsApi>(); }

  MockOdCdsApi();
  ~MockOdCdsApi() override;

  MOCK_METHOD(void, updateOnDemand, (const std::string& cluster_name));
};

} // namespace Upstream
} // namespace Envoy
