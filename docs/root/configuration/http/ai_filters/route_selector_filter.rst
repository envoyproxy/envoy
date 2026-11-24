.. _config_http_ai_filters_route_selector:

Route selector
==============

The route selector AI filter applies, on each upstream attempt, the candidate
the :ref:`model router <config_http_ai_filters_model_router>` AI filter resolved
for it: the first candidate on the first attempt, the second on the first retry,
and so on. It removes the client's ``authorization``, ``x-api-key`` and
``x-goog-api-key`` headers, sets the request's authority to the candidate's
provider, sends the candidate's credential in ``authorization``
(``x-goog-api-key`` for ``GEMINI_GENERATE_CONTENT``), and leaves the
candidate's wire API, model and binding as the ``envoy.ai.llm_protocol.response``,
``envoy.ai.model.resolved`` and ``envoy.ai.binding.response`` :ref:`transcoding
instructions <config_http_ai_filters_transcoder_instructions>`.

* This AI filter should be configured with the type URL
  ``type.googleapis.com/envoy.extensions.http.ai_filters.route_selector.v3.RouteSelector``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.http.ai_filters.route_selector.v3.RouteSelector>`

Configuration
-------------

The filter reads the attempt count, so it runs in an AI Protocol Manager placed
in the router's upstream HTTP filter chain, ahead of the
:ref:`transcoder <config_http_ai_filters_transcoder>` instances that apply its
instructions:

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
      upstream_http_filters:
      - name: envoy.filters.http.ai_protocol_manager
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
          request_handling: {}
          filters:
          - name: envoy.http.ai_filters.route_selector
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.http.ai_filters.route_selector.v3.RouteSelector
          - name: envoy.http.ai_filters.transcoder
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.http.ai_filters.transcoder.v3.Transcoder
              request_handling: TO_IR
              response_handling: FROM_IR
          - name: envoy.http.ai_filters.transcoder
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.http.ai_filters.transcoder.v3.Transcoder
              request_handling: FROM_IR
              response_handling: TO_IR
      - name: envoy.filters.http.upstream_codec
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.upstream_codec.v3.UpstreamCodec

An attempt with no candidate, because the model router did not run or the
retries outnumber the candidates, fails with a 500.

Statistics
----------

The filter outputs statistics in the ``ai_protocol_manager.route_selector.`` namespace:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  success, Counter, The attempt's candidate was applied to the request.
  invalid_route_data, Counter, "No candidate was resolved for the attempt, or it named no wire API; the request failed with a 500."
