The load balancer, network filter, and listener filter Rust SDK getters return the Envoy-owned
buffer directly as a borrowed ``EnvoyBuffer`` instead of copying into an owned ``String``,
avoiding a per-call allocation on the host-selection and metadata read paths.
