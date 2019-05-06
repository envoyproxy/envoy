Comparison to similar systems
=============================

The Envoy Mobile proposed roadmap has similarities to several existing systems. They are compared
below.

.. attention::

  The projects below are under active development. Thus some of the information may become out of
  date. If that is the case please let us know and we will fix it.

`Cronet <https://chromium.googlesource.com/chromium/src/+/master/components/cronet/>`_
--------------------------------------------------------------------------------------

Cronet is Google's cross-platform client networking library that is used by all of its mobile
applications and several other companies including Snap. In terms of goals, Cronet is a subset of
Envoy Mobile targeted at advanced client networking protocols, with the caveat that it already
supports QUIC. Envoy Mobile's goals are larger and include extensibility, observability, IDL
integration, etc. With that said, there is a lot to learn from Cronet in terms of how it handles
QUIC in a robust and production ready way.

`proxygen <https://github.com/facebook/proxygen>`_ and `mvfst <https://github.com/facebookincubator/mvfst>`_
------------------------------------------------------------------------------------------------------------

proxygen is Facebook's high performance HTTP proxy library, written on top of a Finagle like
C++ library called wangle. mvst is Facebook's recently open sourced QUIC implementation. Both
libraries are used by Facebook in their mobile applications, but none of the mobile integration
code has been open sourced. Like Cronet, Envoy Mobile's goals are a superset of what proxygen and
mvfst provide.

`gRPC <https://www.grpc.io/>`_
------------------------------

gRPC is a multi-platform message passing system out of Google. It uses an IDL to describe an RPC
library and then implements application specific runtimes for a variety of different languages. The
underlying transport is HTTP/2. gRPC does have mobile client libraries, though the production
readiness is not equivalent to the server-side libraries. In terms of goals and feature set, gRPC is
closer to Envoy Mobile than Cronet or proxygen/mvfst, and in fact gRPC is currently in the process
of migrating its look-aside load balancing APIs to `xDS
<https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/dynamic_configuration>`_. It is
possible that in the future gRPC's mobile clients will continue to converge with Envoy Mobile, but
in the near term we feel that Envoy Mobile can deliver a production ready iOS/Android feature set
that includes xDS, QUIC, etc. in a shorter time frame. Note that Envoy Mobile will likely continue
to use gRPC framing for calling IDL based unary and streaming APIs, making it wire compatible with
gRPC on the server.
