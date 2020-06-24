.. _faq_how_fast_is_envoy:

How fast is Envoy?
==================

We are frequently asked *how fast is Envoy?* or *how much latency will Envoy add to my requests?*
The answer is: *it depends*. Performance depends a great deal on which Envoy features are being
used and the environment in which Envoy is run. In addition, doing accurate performance testing
is an incredibly difficult task that the project does not currently have resources for.

Although we have done quite a bit of performance tuning of Envoy in the critical path and we
believe it performs extremely well, because of the previous points we do not currently publish
any official benchmarks. We encourage users to benchmark Envoy in their own environments with a
configuration similar to what they plan on using in production.
