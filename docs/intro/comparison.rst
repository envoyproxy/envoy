Comparison to similar systems
=============================

Overall, we believe that Envoy has a unique and compelling feature set for modern service oriented
architectures. Below we compare Envoy to other related systems. Though in any particular area
(edge proxy, software load balancer, service message passing layer) Envoy may not be as feature
rich as some of the solutions below, in aggregate no other solution supplies the same set of
overall features into a single self contained and high performance package.

`nginx <https://nginx.org/en/>`_
--------------------------------

nginx is the canonical modern web server. It supports serving static content, HTTP L7 reverse proxy
load balancing, HTTP/2, and many other features. nginx has far more overall features than Envoy as
an edge reverse proxy, though we think that most modern service oriented architectures don't
typically make use of them. Envoy provides the following main advantages over nginx as an edge
proxy:

* Full HTTP/2 transparent proxy. Envoy supports HTTP/2 for both downstream and upstream
  communication. nginx only supports HTTP/2 for downstream connections.
* Advanced load balancing. Though nginx plus (the paid server) supports some advanced load
  balancing features such as out of band health checking, even that version does not have the
  advanced service discovery and asynchronous DNS resolution features that Envoy does.
* Ability to run the same software at the edge as well as on each service node. Many infrastructures
  run a mix of nginx and haproxy. A single proxy solution at every hop is substantially simpler from
  an operations perspective.

`haproxy <http://www.haproxy.org/>`_
------------------------------------

haproxy is the canonical modern software load balancer. It also supports basic HTTP reverse proxy
features. Envoy provides the following main advantages over haproxy as a load balancer:

* HTTP/2 support.
* Pluggable architecture.
* Multi-threaded architecture. It is substantially easier to operate and configure circuit breaking
  settings when a single process is deployed per machine versus potentially multiple processes.
* Integration with a remote service discovery service.
* Integration with a remote global rate limiting service.
* Ability to hot restart.
* Substantially more detailed statistics.

`AWS ELB <https://aws.amazon.com/elasticloadbalancing/>`_
---------------------------------------------------------

Amazon's ELB is the standard solution for service discovery and load balancing for applications in
EC2. Envoy provides the following main advantages of ELB as a load balancer and service discovery
system:

* Statistics and logging (CloudWatch statistics are delayed and extremely lacking in detail, logs
  must be retrieved from S3 and have a fixed format).
* Stability (it is common to see sporadic instability when using ELBs which ends up being impossible
  to debug).
* Advanced load balancing and direct connection between nodes. An Envoy mesh avoids an additional
  network hop via variably performing elastic hardware. The load balancer can make better decisions
  and gather more interesting statistics based on zone, canary status, etc. The load balancer also
  supports advanced features such as retry.

AWS recently released the *application load balancer* product. This product adds HTTP/2 support as
well as basic HTTP L7 request routing to multiple backend clusters. The feature set is still small
compared to Envoy and performance and stability are unknown, but it's clear that AWS will continue
to invest in this area in the future.

`SmartStack <http://nerds.airbnb.com/smartstack-service-discovery-cloud/>`_
---------------------------------------------------------------------------

SmartStack is an interesting solution which provides additional service discovery and health
checking support on top of haproxy. At a high level, SmartStack has most of the same goals as
Envoy (out of process architecture, application platform agnostic, etc.). Envoy provides the
following main advantages over SmartStack as a load balancer and service discovery package:

* All of the previously mentioned advantages over haproxy.
* Integrated service discovery and active health checking. Envoy provides everything in a single
  high performance package.

`Finagle <https://twitter.github.io/finagle/>`_
-----------------------------------------------

Finagle is Twitter's Scala/JVM service to service communication library. It is used by Twitter and
many other companies that have a primarily JVM based architecture. It has many of the same features
as Envoy such as service discovery, load balancing, filters, etc. Envoy provides the following main
advantages over Finagle as a load balancer and service discovery package:

* Eventually consistent service discovery via distributed active health checking.
* Order of magnitude better performance across all metrics (memory consumption, CPU usage, and P99
  latency properties).
* Out of process and application agnostic architecture. Envoy works with any application stack.

`proxygen <https://github.com/facebook/proxygen>`_ and `wangle <https://github.com/facebook/wangle>`_
-----------------------------------------------------------------------------------------------------

proxygen is Facebook's high performance C++11 HTTP proxy library, written on top of a Finagle like
C++ library called wangle. From a code perspective, Envoy uses most of the same techniques as
proxygen to obtain high performance as an HTTP library/proxy. Beyond that however the two projects
are not really comparable as Envoy is a complete self contained server with a large feature set
versus a library that must be built into something by each project individually. As of this writing
proxygen also does not support HTTP/2.

`gRPC <http://www.grpc.io/>`_
-----------------------------

GRPC is a new multi-platform message passing system out of Google. It uses an IDL to describe an RPC
library and then implements application specific runtimes for a variety of different languages. The
underlying transport is HTTP/2.  Although GRPC likely has the goal of implementing many Envoy like
features in the future (load balancing, etc.), as of this writing the various runtimes are somewhat
immature and are primarily focused on serialization/de-serialization. We consider GRPC to be a
companion to Envoy versus a competitor. How Envoy integrates with GRPC is described :ref:`here
<arch_overview_grpc>`.
