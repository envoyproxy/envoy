//! Load balancer integration test module. Mirror of
//! test_data/go/load_balancer_integration_test/load_balancer_integration_test.go.
//!
//! Registers a "first_host_lb" load balancer that always picks priority 0, index 0.

use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::Arc;

declare_load_balancer_init_functions!(init, new_lb_config);

fn init() -> bool {
  true
}

fn new_lb_config(
  _name: &str,
  _config: &[u8],
  _envoy_lb_config: Arc<dyn EnvoyLbConfig>,
) -> Option<Box<dyn LoadBalancerConfig>> {
  Some(Box::new(FirstHostConfig {}))
}

struct FirstHostConfig {}

impl LoadBalancerConfig for FirstHostConfig {
  fn new_load_balancer(&self, _envoy_lb: &dyn EnvoyLoadBalancer) -> Box<dyn LoadBalancer> {
    Box::new(FirstHostLB {})
  }
}

struct FirstHostLB {}

impl LoadBalancer for FirstHostLB {
  fn choose_host(&mut self, envoy_lb: &dyn EnvoyLoadBalancer) -> Option<HostSelection> {
    if envoy_lb.get_hosts_count(0) == 0 {
      return None;
    }
    Some(HostSelection {
      priority: 0,
      index: 0,
    })
  }
}
