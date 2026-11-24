.. _config_http_ai_filters_model_router:

Model router
============

The model router AI filter maps the model a client asks for to an ordered list
of upstream models, and stores them in the ``envoy.ai.routes`` filter state
object for the :ref:`route selector <config_http_ai_filters_route_selector>` AI
filter to apply on each upstream attempt. It reads the requested model from the
``envoy.ai.model.request`` filter state object, which the :ref:`request info
<config_http_ai_filters_request_info>` AI filter sets, so configure it after that
filter.

* This AI filter should be configured with the type URL
  ``type.googleapis.com/envoy.extensions.http.ai_filters.model_router.v3.ModelRouter``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.http.ai_filters.model_router.v3.ModelRouter>`

Configuration
-------------

A :ref:`provider <envoy_v3_api_msg_extensions.http.ai_filters.model_router.v3.Provider>`
names an upstream's wire API, its authority and, optionally, the :ref:`binding
<config_http_ai_filters_transcoder_instructions>` it carries the API in. A
:ref:`model <envoy_v3_api_msg_extensions.http.ai_filters.model_router.v3.Model>`
names the provider hosting it, the model identifier sent upstream and,
optionally, the credential to send with it. A :ref:`route
<envoy_v3_api_msg_extensions.http.ai_filters.model_router.v3.RoutingEntry>` maps a
requested model to candidate models: the first serves the first attempt, and
each retry the next. All models in a route must have the same provider.

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.ai_protocol_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
      request_handling: {}
      filters:
      - name: envoy.http.ai_filters.request_info
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.http.ai_filters.request_info.v3.RequestInfo
      - name: envoy.http.ai_filters.model_router
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.http.ai_filters.model_router.v3.ModelRouter
          providers:
          - name: anthropic
            llm_protocol: ANTHROPIC_MESSAGES
            host_and_port: api.anthropic.com
          models:
          - name: haiku
            provider: anthropic
            model: claude-haiku-5-5
            api_key:
              environment_variable: ANTHROPIC_AUTHORIZATION
          - name: sonnet
            provider: anthropic
            model: claude-sonnet-5-5
            api_key:
              environment_variable: ANTHROPIC_AUTHORIZATION
          routes:
          - requested_model: fast
            targets:
            - model: haiku
            - model: sonnet

The credential is the whole header value, such as ``Bearer sk-...``.

A request whose model is unknown, or that names none, is rejected with a 404.
Until the router reads the resolved routes itself, the filter also sets the
request's authority to the first candidate's provider and
``x-envoy-max-retries`` to one less than the number of candidates.

Statistics
----------

The filter outputs statistics in the ``ai_protocol_manager.model_router.`` namespace:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  success, Counter, The requested model matched a route and its candidates were stored in filter state.
  model_name_missing, Counter, The request named no model and was rejected with a 404.
  route_not_found, Counter, No route matched the requested model and the request was rejected with a 404.
