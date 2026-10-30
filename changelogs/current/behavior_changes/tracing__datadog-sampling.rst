The Datadog tracer now applies Envoy's initial sampling decision as a manual keep or drop,
consistently with refreshed decisions. Previously, an initial keep decision allowed additional
sampling by Datadog, while a route cache refresh forced a manual keep. Enable
:ref:`delegate_sampling <envoy_v3_api_field_config.trace.v3.DatadogConfig.delegate_sampling>`
to allow the Datadog sampler to apply its rules and agent-provided rates when Envoy samples a
trace. In delegated mode, route refreshes can revise the decision until context is injected
upstream or the span finishes. Incoming sampling decisions are preserved in either mode.
