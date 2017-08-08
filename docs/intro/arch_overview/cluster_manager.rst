.. _arch_overview_cluster_manager:

Cluster manager
===============

Envoy’s cluster manager manages all configured upstream clusters. Just as the Envoy configuration
can contain any number of listeners, the configuration can also contain any number of independently
configured upstream clusters.

Upstream clusters and hosts are abstracted from the network/HTTP filter stack given that upstream
clusters and hosts may be used for any number of different proxy tasks. The cluster manager exposes
APIs to the filter stack that allow filters to obtain a L3/L4 connection to an upstream cluster, or
a handle to an abstract HTTP connection pool to an upstream cluster (whether the upstream host
supports HTTP/1.1 or HTTP/2 is hidden). A filter stage determines whether it needs an L3/L4
connection or a new HTTP stream and the cluster manager handles all of the complexity of knowing
which hosts are available and healthy, load balancing, thread local storage of upstream connection
data (since most Envoy code is written to be single threaded), upstream connection type (TCP/IP,
UDS), upstream protocol where applicable (HTTP/1.1, HTTP/2), etc.

Clusters known to the cluster manager can be configured either statically, or fetched dynamically
via the cluster discovery service (CDS) API. Dynamic cluster fetches allow more configuration to
be stored in a central configuration server and thus requires fewer Envoy restarts and configuration
distribution.

* Cluster manager :ref:`configuration <config_cluster_manager>`.
* CDS :ref:`configuration <config_cluster_manager_cds>`.
