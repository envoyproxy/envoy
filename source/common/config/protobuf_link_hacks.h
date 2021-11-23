#pragma once

#include "envoy/service/cluster/v3/cds.pb.h"
#include "envoy/service/discovery/v3/ads.pb.h"
#include "envoy/service/endpoint/v3/eds.pb.h"
#include "envoy/service/endpoint/v3/leds.pb.h"
#include "envoy/service/extension/v3/config_discovery.pb.h"
#include "envoy/service/health/v3/hds.pb.h"
#include "envoy/service/listener/v3/lds.pb.h"
#include "envoy/service/ratelimit/v3/rls.pb.h"
#include "envoy/service/route/v3/rds.pb.h"
#include "envoy/service/route/v3/srds.pb.h"
#include "envoy/service/runtime/v3/rtds.pb.h"
#include "envoy/service/secret/v3/sds.pb.h"

// API_NO_BOOST_FILE

namespace Envoy {

const envoy::service::discovery::v3::AdsDummy _ads_dummy_v3;
const envoy::service::ratelimit::v3::RateLimitRequest _rls_dummy_v3;
const envoy::service::secret::v3::SdsDummy _sds_dummy_v3;
const envoy::service::runtime::v3::RtdsDummy _tds_dummy_v3;
const envoy::service::listener::v3::LdsDummy _lds_dummy_v3;
const envoy::service::route::v3::RdsDummy _rds_dummy_v3;
const envoy::service::cluster::v3::CdsDummy _cds_dummy_v3;
const envoy::service::endpoint::v3::EdsDummy _eds_dummy_v3;
const envoy::service::endpoint::v3::LedsDummy _leds_dummy_v3;
const envoy::service::route::v3::SrdsDummy _srds_dummy_v3;
const envoy::service::extension::v3::EcdsDummy _ecds_dummy_v3;
const envoy::service::runtime::v3::RtdsDummy _rtds_dummy_v3;
const envoy::service::health::v3::HdsDummy _hds_dummy_v3;

} // namespace Envoy
