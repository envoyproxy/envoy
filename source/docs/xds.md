# Envoy xDS subsystem

## Overview

Envoy implements the [xDS
protocol](https://www.envoyproxy.io/docs/envoy/latest/api-docs/xds_protocol). This document tracks
some implementation details.

An overview of the architecture is provided in the diagram below:

![xDS implementation architecture overview](xDS_code_diagram.png)

Note that this is currently somewhat idealized, with Envoy having two gRPC mux implementations,
`GrpcMuxImpl` and `NewGrpxMuxImpl`, while we complete migration to a single muxer, see
https://github.com/envoyproxy/envoy/issues/11477.

The key components are:
* `GrpcMuxImpl` which manages the xDS transport and multiplexing one or more subscriptions on a
  single gRPC stream. The muxer is responsible for managing the wire level concerns via
  `GrpcStream`, while also tracking subscription state and watchers for updates. Multiple resource
  type URLs may be managed by a single muxer.
* `SubscriptionState` tracks subscription state for a single resource type URL.
* `WatchMap` tracks all the subscribers for a given resource type.
* Each Envoy subsystem, e.g. LDS, CDS, etc. has a `GrpcSubscription` that points at a `GrpcMuxImpl`.

## xdstp:// naming

In addition, Envoy is currently implementing a new structured naming scheme aimed to support
better scalability, cacheability, federation and reliability, see
https://github.com/envoyproxy/envoy/issues/11264. This introduces new requirements on the xDS
transport; it needs to manage subscriptions to new forms of resource collections and provide
contextual information in resource name subscriptions.

To support this, the server `LocalInfo` owns a `Config::ContextProvider`, providing all node
specific context parameters required for `xdstp://` naming. This is consumed by `GrpcMuxImpl`. In
addition, `GrpcMuxImpl` and `WatchMap` are aware of this naming scheme and implement the expected
match semantics and naming schemes.

Implementation is in progress and we support the following aspects of `xdstp://` today:
* LDS global collections over ADS.
* LDS filesystem list collections featuring inline resources.

It should be noted that `xdstp://` support is experimental at this time while we complete
implementation.
