What is Envoy Mobile
====================

.. attention::

  Envoy Mobile is currently in a very early stage of development. This page should be read as a set
  of aspirational project goals.

Envoy Mobile is an iOS and Android client networking library built using
`Envoy <https://www.envoyproxy.io/>`_ as its core.

Envoy's `project goals <https://www.envoyproxy.io/docs/envoy/latest/intro/what_is_envoy>`_ are
simply stated as:

  *The network should be transparent to applications. When network and application problems do occur
  it should be easy to determine the source of the problem.*

As fundamental as Envoy has become in scaling many organizations’ distributed architectures, the
reality is that three 9s at the server-side edge is meaningless if the user of a mobile application
is only able to complete the desired product flows a fraction of the time. This may be due to a
combination of network and application errors. Thus, in order to fully achieve the Envoy project’s
goal of making the network transparent to applications, the service mesh and its inherent benefits
(observability, consistency, etc.) must expand beyond the edge all the way to the mobile
applications that are so critical to the end user’s experience. Envoy Mobile in conjunction with
Envoy in the data center will provide the ability to reason about the entire distributed system
network, not just the server-side portion.

Whereas server-side Envoy proxy is a self-contained process that is meant to be deployed alongside a
polyglot architecture, Envoy Mobile is distributed as a library meant to be compiled directly into
client mobile applications. The library approach is required due to the practicalities of how
applications are written and distributed on both the iOS and Android platforms. The high level goals
of the library are discussed in the following subsections.

Ubiquitous API and abstraction for networking
---------------------------------------------

With the industry progressively moving towards specifying APIs via a strongly typed IDL such as
`protocol buffers <https://developers.google.com/protocol-buffers/>`_, Envoy Mobile will standardize
and abstract how mobile developers interact with IDL exposed endpoints. Via `intelligent protobuf
code generation <https://github.com/lyft/protoc-gen-star>`_ and an abstract transport, both iOS and
Android can provide similar interfaces and ergonomics for consuming APIs. Initially we are planning
on focusing our efforts on Swift APIs for iOS and Kotlin APIs for Android, but depending on
community interest we will consider adding support for additional languages in the future. Our
ultimate goal is to make the low-level Envoy common C++ code an implementation detail that the
average mobile developer does not need to be aware of. Instead, mobile developers will interact with
high-level language specific APIs that encapsulate common concerns such as making API calls,
analytics, tracing, etc.

Simple and explicit system for supporting advanced networking features
----------------------------------------------------------------------

With protocol buffer’s powerful annotation/extension system, Envoy Mobile can add sophisticated
cross-platform functionality in a simple and explicit way when using strongly typed IDL APIs.
Examples of annotations that are planned on our roadmap include marking an API as offline/deferred
capable, caching, priority, streaming, marking fields for exclusion both on the request and response
in poor network conditions, as well as general Envoy policies such as retry and timeout
specifications.

Much like Envoy’s use in a server-side service mesh, the goal is to push as much functionality as
possible into the common core so as to avoid reimplementing it in every mobile application language.

Our long-term plans include evolving the `gRPC Server Reflection Protocol
<https://github.com/grpc/grpc/blob/master/doc/server-reflection.md>`_ into a streaming reflection
service API. This API will allow both Envoy and Envoy Mobile to fetch generic protobuf definitions
from a central IDL service, which can then be used to implement annotation driven networking via
reflection. This model means that Envoy Mobile will not necessarily need to have prior knowledge of
an organization’s APIs in order to provide enhanced cross-platform networking functionality.

xDS driven mobile client policy
-------------------------------

One of the reasons that Envoy has become so popular as a platform is its rich configuration
discovery APIs which are collectively known as `xDS
<https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/dynamic_configuration>`_. These
APIs allow a distributed set of Envoys to be managed by an eventually consistent control plane. One
of the long term goals of Envoy Mobile is to bring xDS configuration all the way to mobile clients,
in the form of routing, authentication, failover, load balancing, and other policies driven by a
global load balancing system. This will be an extremely powerful mechanism for bringing layer 7 /
application networking concepts all the way to the mobile client.
