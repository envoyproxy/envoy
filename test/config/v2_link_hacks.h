#pragma once

#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/discovery.pb.h"
#include "envoy/api/v2/eds.pb.h"
#include "envoy/api/v2/lds.pb.h"
#include "envoy/api/v2/rds.pb.h"
#include "envoy/api/v2/srds.pb.h"
#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/service/discovery/v2/ads.pb.h"
#include "envoy/service/discovery/v2/hds.pb.h"
#include "envoy/service/discovery/v2/rtds.pb.h"
#include "envoy/service/discovery/v2/sds.pb.h"
#include "envoy/service/ratelimit/v2/rls.pb.h"

namespace Envoy {

// Hack to force linking of the service: https://github.com/google/protobuf/issues/4221.
// This file should be included ONLY if this hack is required.
const envoy::service::discovery::v2::AdsDummy _ads_dummy_v2;
const envoy::service::ratelimit::v2::RateLimitRequest _rls_dummy_v2;
const envoy::service::discovery::v2::SdsDummy _sds_dummy_v2;
const envoy::service::discovery::v2::RtdsDummy _tds_dummy_v2;
const envoy::api::v2::LdsDummy _lds_dummy_v2;
const envoy::api::v2::RdsDummy _rds_dummy_v2;
const envoy::api::v2::CdsDummy _cds_dummy_v2;
const envoy::api::v2::EdsDummy _eds_dummy_v2;
const envoy::api::v2::SrdsDummy _srds_dummy_v2;
const envoy::config::bootstrap::v2::Bootstrap _bootstrap_dummy_v2;
const envoy::service::discovery::v2::Capability _hds_dummy_v2;

} // namespace Envoy
