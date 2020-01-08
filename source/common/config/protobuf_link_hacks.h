#pragma once

#include "envoy/service/discovery/v3alpha/ads.pb.h"
#include "envoy/service/ratelimit/v3alpha/rls.pb.h"
#include "envoy/service/runtime/v3alpha/rtds.pb.h"
#include "envoy/service/secret/v3alpha/sds.pb.h"

namespace Envoy {

// Hack to force linking of the service: https://github.com/google/protobuf/issues/4221.
// This file should be included ONLY if this hack is required.
const envoy::service::discovery::v3alpha::AdsDummy _ads_dummy;
const envoy::service::ratelimit::v3alpha::RateLimitRequest _rls_dummy;
const envoy::service::secret::v3alpha::SdsDummy _sds_dummy;
const envoy::service::runtime::v3alpha::RtdsDummy _tds_dummy;
} // namespace Envoy
