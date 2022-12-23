Health checks: https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/upstream/load_balancing/panic_threshold.html
Panic treshold: https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/upstream/load_balancing/panic_threshold.html


Notes:

- configuration of panic threshold

Experiment idea:

- Bring up 2 instances of each grpc service
- render one unhealthy (may be using docker-compose exec)
- requests should all pass
- could query metrics for health checks

