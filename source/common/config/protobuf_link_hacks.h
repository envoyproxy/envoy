#pragma once

#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/eds.pb.h"
#include "envoy/api/v2/lds.pb.h"
#include "envoy/api/v2/rds.pb.h"
#include "envoy/api/v2/srds.pb.h"
#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/service/cluster/v3/cds.pb.h"
#include "envoy/service/discovery/v2/ads.pb.h"
#include "envoy/service/discovery/v2/hds.pb.h"
#include "envoy/service/discovery/v2/rtds.pb.h"
#include "envoy/service/discovery/v2/sds.pb.h"
#include "envoy/service/discovery/v3/ads.pb.h"
#include "envoy/service/endpoint/v3/eds.pb.h"
#include "envoy/service/listener/v3/lds.pb.h"
#include "envoy/service/ratelimit/v2/rls.pb.h"
#include "envoy/service/ratelimit/v3/rls.pb.h"
#include "envoy/service/route/v3/rds.pb.h"
#include "envoy/service/route/v3/srds.pb.h"
#include "envoy/service/runtime/v3/rtds.pb.h"
#include "envoy/service/secret/v3/sds.pb.h"

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

const envoy::service::discovery::v3::AdsDummy _ads_dummy_v3;
const envoy::service::ratelimit::v3::RateLimitRequest _rls_dummy_v3;
const envoy::service::secret::v3::SdsDummy _sds_dummy_v3;
const envoy::service::runtime::v3::RtdsDummy _tds_dummy_v3;
const envoy::service::listener::v3::LdsDummy _lds_dummy_v3;
const envoy::service::route::v3::RdsDummy _rds_dummy_v3;
const envoy::service::cluster::v3::CdsDummy _cds_dummy_v3;
const envoy::service::endpoint::v3::EdsDummy _eds_dummy_v3;
const envoy::service::route::v3::SrdsDummy _srds_dummy_v3;

// With the v2 -> v3 migration there is another, related linking issue.
// Symbols for v2 protos which headers are not included in any file in the codebase are being
// dropped by the linker in some circumstances. For example, in the Envoy Mobile iOS build system.
// Even though all v2 packages are included as a dependency in their corresponding v3 package, and
// `always_link` is set for all proto bazel targets.
// Further proof of this can be seen by way of counter example with the envoy.api.v2.Cluster type,
// which is checked for by proto_descriptors.cc. This type **is** getting linked because its headers
// is still included in cds_api_impl.cc. On the other side because the v2 hds header is not included
// anywhere the v2 service type is getting dropped, and thus the descriptor is not present in the
// descriptor pool.
// https://github.com/envoyproxy/envoy/issues/9639
const envoy::config::bootstrap::v2::Bootstrap _bootstrap_dummy_v2;
const envoy::service::discovery::v2::Capability _hds_dummy_v2;

} // namespace Envoy
