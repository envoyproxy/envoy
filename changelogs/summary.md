**Summary of changes**:

* Added new access\_log command operators to retrieve upstream connection information.
* Enhanced ext\_authz to be configured to to ignore dynamic metadata in ext\_authz responses.
* Ext\_authz: added a block list for headers that should never be send to the external auth service.
* Ext\_authz: added the ability to configure what decoder header mutations are allowed from the ext\_authz with the option to fail if disallowed mutations are requested.
* Ext\_proc support for observability mode which is “Send and Go” mode that can be used by external processor to observe Envoy data and status.
* Added support for flow control in Envoy gRPC side stream. This behavior can be disabled by setting the runtime flag envoy.reloadable\_features.grpc\_side\_stream\_flow\_control to false.
* TCP Healthchecks can now leverage ProxyProtocol.
* Hot\_restart: Added new command-line flag to skip hot restart stats transfer.
* Http: Added the ability when request\_mirroring to disable appending of the -shadow suffix to the shadowed host/authority header.
* Http: Added the ability to set the downstream request :scheme to match the upstream transport protocol.
* Http: Envoy now supports proxying 104 headers from upstream.
* Added the ability to bypass the overload manager for a listener.
* Added support for local cluster rate limit shared across all Envoy instances in the local cluster.
* Added Filter State Input for matching http input based on filter state objects.
* Oauth: Added an option to disable setting the ID Token cookie.
* Open Telemtry enhancements to support extension formatter and stats\_prefix configuration for the OpenTelemetry logger.
* QUIC: QUIC stream reset errors are now captured in transport failure reason. Added support for QUIC server preferred address when there is a DNAT between the client and Envoy.
* Added support for Redis inline commands, Bloom 1.0.0 commands, among other commands.
* Added a new retry policy: reset-before-request.
* Added support for dynamic direct response for files.
* Tls: added support to match against OtherName SAN Type under match\_typed\_subject\_alt\_names.
* Upstream: Added a new field to `LocalityLbEndpoints`, `LocalityLbEndpoints.Metadata`, that may be used for transport socket matching groups of endpoints.
* Update wasm filter to support use as an upstream filter.
* Disabled OpenCensus by default as it is no longer maintained upstream.
* Ext\_proc support for route\_cache\_action which specifies the route action to be taken when an external processor response is received in response to request headers.
* Golang: Move `Continue`, `SendLocalReply and `RecoverPanic` to `DecoderFilterCallbacks` and `EncoderFilterCallbacks`, to support full-duplex processing.
* Http2: Use oghttp2 by default.
* Http3: Added a “happy eyeballs” feature to HTTP/3 upstream, where it assuming happy eyeballs sorting results in alternating address families will attempt the first v4 and v6 address before giving up on HTTP/3.
* Populate typed metadata by default in proxy protocol listener.
* Datadog: Disabled remote configuration by default.
* Runtime: Reject invalid yaml instead of supporting corner cases of bad yaml.

