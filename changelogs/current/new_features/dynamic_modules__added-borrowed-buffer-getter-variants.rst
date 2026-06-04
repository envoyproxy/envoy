Added borrowed ``EnvoyBuffer`` variants of the owned-``String`` getters in the load balancer,
network filter, and listener filter Rust SDKs. The new ``*_bytes`` methods (for example
``EnvoyLoadBalancer::get_cluster_name_bytes`` and
``EnvoyNetworkFilter::get_upstream_host_address_bytes``) return the Envoy-owned buffer directly
instead of copying into a ``String``, avoiding a per-call allocation on the host-selection and
metadata read paths.
