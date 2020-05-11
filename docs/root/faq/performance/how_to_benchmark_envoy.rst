What are best practices for benchmarking Envoy?
===============================================

There is :ref:`no single QPS, latency or throughput overhead <faq_how_fast_is_envoy>` that can
characterize a network proxy such as Envoy. Instead, any measurements need to be contextually aware,
ensuring an apples-to-apples comparison with other systems by configuring and load testing Envoy
appropriately. As a result, we can't provide a canonical benchmark configuration, but instead offer
the following guidance:

* A release Envoy binary should be used. If building, please ensure that `-c opt`
  is used on the Bazel command line. When consuming Envoy point releases, make
  sure you are using the latest point release; given the pace of Envoy development
  it's not reasonable to pick older versions when making a statement about Envoy
  performance. Similarly, if working on a master build, please perform due diligence
  and ensure no regressions or performance improvements have landed proximal to your
  benchmark work and that your are close to HEAD.

* The :option:`--concurrency` Envoy CLI flag should be unset (providing one worker thread per
  logical core on your machine) or set to match the number of cores/threads made available to other
  network proxies in your comparison.

* Disable :ref:`generate_request_id
  <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.generate_request_id>`.

* Disable :ref:`dynamic_stats
  <envoy_v3_api_field_extensions.filters.http.router.v3.Router.dynamic_stats>`.

* Ensure that the networking and HTTP filter chains are reflective of comparable features
  in the systems that Envoy is being compared with.

* Ensure that TLS settings (if any) are realistic and that consistent cyphers are used in
  any comparison.

* Ensure that :ref:`HTTP/2 settings <envoy_v3_api_msg_config.core.v3.Http2ProtocolOptions>`, in
  particular those that affect flow control and stream concurrency, are consistent in any
  comparison.

* Verify in the listener and cluster stats that the number of streams, connections and errors
  matches what is expected in any given experiment.

* Be critical of your bootstrap or xDS configuration. Ideally every line has a motivation and is
  necessary for the benchmark under consideration.

* Examine `perf` profiles of Envoy during the benchmark run, e.g. with `flame graphs
  <http://www.brendangregg.com/flamegraphs.html>`_. Verify that Envoy is spending its time
  doing the expected essential work under test, rather than some unrelated or tangential
  work.

* Familiarize yourself with `latency measurement best practices
  <https://www.youtube.com/watch?v=lJ8ydIuPFeU>`_. In particular, never measure latency at
  max load, this is not generally meaningful or reflecting of real system performance; aim
  to measure below the knee of the QPS-latency curve. Prefer open vs. closed loop load
  generators.

* Avoid `benchmarking crimes <https://www.cse.unsw.edu.au/~gernot/benchmarking-crimes.html>`_.
