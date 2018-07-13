#pragma once

#include "envoy/service/discovery/v2/ads.pb.h"
#include "envoy/service/discovery/v2/sds.pb.h"
#include "envoy/service/ratelimit/v2/rls.pb.h"

namespace Envoy {

// Hack to force linking of the service: https://github.com/google/protobuf/issues/4221.
// This file should be included ONLY if this hack is required.
const envoy::service::discovery::v2::AdsDummy _ads_dummy;
const envoy::service::discovery::v2::SdsDummy _sds_dummy;
const envoy::service::ratelimit::v2::RateLimitRequest _rls_dummy;
} // namespace Envoy
