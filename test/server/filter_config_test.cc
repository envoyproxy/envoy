#include <memory>

#include "envoy/server/filter_config.h"
#include "testing/base/public/gmock.h"
#include "testing/base/public/gunit.h"
#include "third_party/absl/memory/memory.h"

namespace Envoy {
namespace {

class FilterConfigTest : public ::testing::Test {
 public:
  FilterConfigTest() {}

 protected:
  std::unique_ptr<FilterConfig> filter_config_;
};

}  // namespace
} // namespace Envoy
