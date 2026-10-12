**Summary of changes**:

* Upstream security fixes:
  - CVE-2026-54786: wasm: bumped `wasmtime` to `24.0.10` to resolve CVE-2026-54786.

* Bug fixes:
  - aws: fixed a race where the route-level configuration of the AWS request signing and AWS Lambda filters could be destroyed on a worker thread when an RDS update replaced the route configuration.
  - dynamic_modules: fixed a bug where the per-route config destroy hook could run on a worker thread, and the module could be unloaded there, when an RDS update replaced the route configuration.
  - local_ratelimit: fixed a race where the share provider manager owned by a local rate limit route-level configuration could be destroyed on a worker thread.
  - sds: fixed a race where a generic secret provider shared with worker threads could be destroyed on a worker thread.
  - stats: fixed a bug introduced in 1.38.0 that exported Prometheus native histogram bucket indices one bucket too low, causing consumers to decode observations into lower value ranges and underreport quantiles.
