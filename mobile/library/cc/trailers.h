#pragma once

#include "headers.h"

namespace Envoy {
namespace Platform {

class Trailers : public Headers {
public:
  Trailers(const RawHeaderMap& headers) : Headers(headers) {}
};

} // namespace Platform
} // namespace Envoy
