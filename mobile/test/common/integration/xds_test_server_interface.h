#pragma once

#include <string>

#include "envoy/service/discovery/v3/discovery.pb.h"

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" { // functions
#endif

/** Initializes xDS server. */
void initXdsServer();

/** Gets the xDS server host. `initXdsServer` must be called prior to calling this function.
 */
const char* getXdsServerHost();

/**
 * Gets the xDS server port. `initXdsServer` must be called prior to calling this function.
 */
int getXdsServerPort();

/**
 * Starts the xDS server. `initXdsServer` must be called prior to calling this function.
 */
void startXdsServer();

/**
 * Sends the `DiscoveryResponse`. `startXdsServer` must be called prior to calling this function.
 */
void sendDiscoveryResponse(const envoy::service::discovery::v3::DiscoveryResponse& response);

/**
 * Shuts down the xDS server. `startXdsServer` must be called prior to calling this function.
 */
void shutdownXdsServer();

#ifdef __cplusplus
} // functions
#endif
