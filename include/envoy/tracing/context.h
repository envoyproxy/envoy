#pragma once

namespace Tracing {

struct TransportContext {
  std::string request_id_;
  std::string span_context_;
};

} // Tracing