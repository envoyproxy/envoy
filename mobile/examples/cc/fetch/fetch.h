#include <string>
#include <vector>

#include "absl/synchronization/notification.h"

#include "envoy/http/header_map.h"
#include "source/common/http/header_map_impl.h"

#include "library/cc/engine_builder.h"
#include "library/cc/stream.h"
#include "library/cc/stream_prototype.h"
#include "library/common/http/client.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Platform {

class Fetch {
public:
  Fetch(int argc, char** argv);

  void Run();

 private:
  void threadRoutine(absl::Notification& engine_running);
  void sendRequest(const absl::string_view url);

  // URLs to fetch
  std::vector<absl::string_view> urls_;

  Api::ApiPtr api_;
  std::unique_ptr<Http::RequestHeaderMapImpl> default_request_headers_;
  //envoy_http_callbacks bridge_callbacks_;
  Event::DispatcherPtr full_dispatcher_;
  Platform::StreamPrototypeSharedPtr stream_prototype_;
  Platform::StreamSharedPtr stream_;
  Platform::EngineSharedPtr engine_;
  Thread::ThreadPtr envoy_thread_;
  Platform::EngineBuilder builder_;
};

}  // namespace Envoy
}  // namespace Platform
