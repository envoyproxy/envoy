#pragma once

#include "envoy/service/discovery/v2/ads.pb.h"
#include "envoy/service/ratelimit/v2/rls.pb.h"

// Hack to force linking of the service: https://github.com/google/protobuf/issues/4221.
// This file should be included ONLY if this hack is required.
envoy::service::discovery::v2::AdsDummy dummy;
envoy::service::ratelimit::v2::RateLimitRequest rls_dummy;