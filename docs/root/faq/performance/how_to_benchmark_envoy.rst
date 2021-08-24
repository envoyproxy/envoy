What are best practices for benchmarking Envoy?
===============================================

There is :ref:`no single QPS, latency or throughput overhead <faq_how_fast_is_envoy>` that can
characterize a network proxy such as Envoy. Instead, any measurements need to be contextually aware,
ensuring an apples-to-apples comparison with other systems by configuring and load testing Envoy
appropriately. As a result, we can't provide a canonical benchmark configuration, but instead offer
the following guidance:

* A release Envoy binary should be used. If building, please ensure that ``-c opt``
  is used on the Bazel command line. When consuming Envoy point releases, make
  sure you are using the latest point release; given the pace of Envoy development
  it's not reasonable to pick older versions when making a statement about Envoy
  performance. Similarly, if working on a main build, please perform due diligence
  and ensure no regressions or performance improvements have landed proximal to your
  benchmark work and that your are close to HEAD.

* The :option:`--concurrency` Envoy CLI flag should be unset (providing one worker thread per
  logical core on your machine) or set to match the number of cores/threads made available to other
  network proxies in your comparison.

* Disable :ref:`circuit breaking <faq_disable_circuit_breaking>`. A common issue during benchmarking
  is that Envoy's default circuit breaker limits are low, leading to connection and request queuing.

* Disable :ref:`generate_request_id
  <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.generate_request_id>`.

* Disable :ref:`dynamic_stats
  <envoy_v3_api_field_extensions.filters.http.router.v3.Router.dynamic_stats>`. If you are measuring
  the overhead vs. a direct connection, you might want to consider disabling all stats via
  :ref:`reject_all <envoy_v3_api_field_config.metrics.v3.StatsMatcher.reject_all>`.

* Ensure that the networking and HTTP filter chains are reflective of comparable features
  in the systems that Envoy is being compared with.

* Ensure that TLS settings (if any) are realistic and that consistent cyphers are used in
  any comparison. Session reuse may have a significant impact on results and should be tracked via
  :ref:`listener SSL stats <config_listener_stats>`.

* Ensure that :ref:`HTTP/2 settings <envoy_v3_api_msg_config.core.v3.Http2ProtocolOptions>`, in
  particular those that affect flow control and stream concurrency, are consistent in any
  comparison. Ideally taking into account BDP and network link latencies when optimizing any
  HTTP/2 settings.

* Verify in the listener and cluster stats that the number of streams, connections and errors
  matches what is expected in any given experiment.

* Make sure you are aware of how connections created by your load generator are
  distributed across Envoy worker threads. This is especially important for
  benchmarks that use low connection counts and perfect keep-alive. You should be aware that
  Envoy will allocate all streams for a given connection to a single worker thread. This means,
  for example, that if you have 72 logical cores and worker threads, but only a single HTTP/2
  connection from your load generator, then only 1 worker thread will be active.

* Make sure request-release timing expectations line up with what is intended.
  Some load generators produce naturally jittery and/or batchy timings. This
  might end up being an unintended dominant factor in certain tests.

* The specifics of how your load generator reuses connections is an important factor (e.g. MRU,
  random, LRU, etc.) as this impacts work distribution.

* If you're trying to measure small (say < 1ms) latencies, make sure the measurement tool and
  environment have the required sensitivity and the noise floor is sufficiently low.

* Be critical of your bootstrap or xDS configuration. Ideally every line has a motivation and is
  necessary for the benchmark under consideration.

* Consider using `Nighthawk <https://github.com/envoyproxy/nighthawk>`_ as your
  load generator and measurement tool. We are committed to building out
  benchmarking and latency measurement best practices in this tool.

* Examine ``perf`` profiles of Envoy during the benchmark run, e.g. with `flame graphs
  <http://www.brendangregg.com/flamegraphs.html>`_. Verify that Envoy is spending its time
  doing the expected essential work under test, rather than some unrelated or tangential
  work.

* Familiarize yourself with `latency measurement best practices
  <https://www.youtube.com/watch?v=lJ8ydIuPFeU>`_. In particular, never measure latency at
  max load, this is not generally meaningful or reflecting of real system performance; aim
  to measure below the knee of the QPS-latency curve. Prefer open vs. closed loop load
  generators.

* Avoid `benchmarking crimes <https://www.cse.unsw.edu.au/~gernot/benchmarking-crimes.html>`_.
