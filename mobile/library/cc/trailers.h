#pragma once

// NOLINT(namespace-envoy)

#include "headers.h"

class Trailers : public Headers {
public:
  Trailers(const RawHeaders& headers) : Headers(headers) {}
};
