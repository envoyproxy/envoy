#include "test/mocks/network/mocks.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {

class UberFilterFuzzer {
public:
  UberFilterFuzzer();

  // Create the filter config and ingest the fuzzed data.
  // TODO: Not sure about what type data should be yet.
  void fuzz(const envoy::config::listener::v3::Filter& proto_config, const std::string data);
  // Setup filter-specific mock expectations - maybe not necessary with FakeConnectionSocket?
  void perFilterSetup(const std::string filter_name);
protected:
  // Setup mock expectations in constructor relevant to all listener filters.
  void fuzzerSetup();
  // Setup mock expectations each time fuzz() is called.
  void filterSetup(const envoy::config::listener::v3::Filter& proto_config);

  // fuzz() will call filterSetup(), then perFilterSetup(), and then onAccept()

private:
  NiceMock<MockConnectionSocket> socket_;
  NiceMock<MockListenerFilterCallbacks> cb_;
  NiceMock<MockDispatcher> dispatcher_;
}

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
