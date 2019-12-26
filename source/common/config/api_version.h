#pragma once

// Use this to force a specific version of a given config proto, preventing API
// boosting from modifying it. E.g. API_NO_BOOST(envoy::api::v2::Cluster).
#define API_NO_BOOST(x) x

namespace Envoy {}
