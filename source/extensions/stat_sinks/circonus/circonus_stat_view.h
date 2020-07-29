#pragma once

#include <memory>
#include <vector>

#include "envoy/server/admin.h"
#include "envoy/server/instance.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/sink.h"

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Circonus {

class CirconusStatView final {
public:
  explicit CirconusStatView(Server::Configuration::ServerFactoryContext& server);

private:
  Http::Code handlerAdminStats(absl::string_view path_and_query,
                               Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response, Server::AdminStream& admin_stream);
  std::string makeJsonBody(const char* histogram_type);

private:
  Server::Configuration::ServerFactoryContext& server_;
  std::vector<std::uint8_t> bb_;
};

using CirconusStatViewPtr = std::unique_ptr<CirconusStatView>;

} // namespace Circonus
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
