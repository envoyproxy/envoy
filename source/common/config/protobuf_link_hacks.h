#pragma once

#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/eds.pb.h"
#include "envoy/api/v2/lds.pb.h"
#include "envoy/api/v2/rds.pb.h"
#include "envoy/api/v2/srds.pb.h"
#include "envoy/service/cluster/v3alpha/cds.pb.h"
#include "envoy/service/discovery/v2/ads.pb.h"
#include "envoy/service/discovery/v2/rtds.pb.h"
#include "envoy/service/discovery/v2/sds.pb.h"
#include "envoy/service/discovery/v3alpha/ads.pb.h"
#include "envoy/service/endpoint/v3alpha/eds.pb.h"
#include "envoy/service/listener/v3alpha/lds.pb.h"
#include "envoy/service/ratelimit/v2/rls.pb.h"
#include "envoy/service/ratelimit/v3alpha/rls.pb.h"
#include "envoy/service/route/v3alpha/rds.pb.h"
#include "envoy/service/route/v3alpha/srds.pb.h"
#include "envoy/service/runtime/v3alpha/rtds.pb.h"
#include "envoy/service/secret/v3alpha/sds.pb.h"

// API_NO_BOOST_FILE

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

const envoy::service::discovery::v3alpha::AdsDummy _ads_dummy_v3;
const envoy::service::ratelimit::v3alpha::RateLimitRequest _rls_dummy_v3;
const envoy::service::secret::v3alpha::SdsDummy _sds_dummy_v3;
const envoy::service::runtime::v3alpha::RtdsDummy _tds_dummy_v3;
const envoy::service::listener::v3alpha::LdsDummy _lds_dummy_v3;
const envoy::service::route::v3alpha::RdsDummy _rds_dummy_v3;
const envoy::service::cluster::v3alpha::CdsDummy _cds_dummy_v3;
const envoy::service::endpoint::v3alpha::EdsDummy _eds_dummy_v3;
const envoy::service::route::v3alpha::SrdsDummy _srds_dummy_v4;
} // namespace Envoy
