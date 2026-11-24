.. _config_http_ai_filters_model_router:

Model router
============

The model router AI filter maps the model a client asks for to an ordered list
of upstream models, and stores them in the ``envoy.ai.routes`` filter state
object for the :ref:`route selector <config_http_ai_filters_route_selector>` AI
filter to apply on each upstream attempt. It reads the requested model from the
``envoy.ai.model.request`` filter state object, which the :ref:`request info
<config_http_ai_filters_request_info>` AI filter sets, so configure it after that
filter: without request info ahead of it, every request is rejected with a
``404``.

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

The filter does not retry by itself. Falling back to the next candidate takes a
:ref:`retry policy <envoy_v3_api_field_config.route.v3.RouteAction.retry_policy>`
on the route, whose ``retry_on`` conditions decide which failures move on to the
next candidate. The filter caps the retries with ``x-envoy-max-retries``, one
less than the number of candidates, and, until the router reads the resolved
candidates itself, sets the request's authority to the first candidate's provider.

The model router is one half of a configuration: it runs in the downstream
filter chain, and the route selector, which applies each candidate, in the
router's upstream filter chain. In this configuration, a request for the model
``fast`` is sent to ``gpt-4o``, and on a retryable failure to ``gpt-4o-mini``:

.. literalinclude:: _include/model-routing.yaml
    :language: yaml
    :lines: 20-69
    :lineno-start: 20
    :emphasize-lines: 6-10, 28-50
    :linenos:
    :caption: :download:`model-routing.yaml <_include/model-routing.yaml>`

The route selector sends the credential as the whole ``authorization`` header
value, such as ``Bearer sk-...``, or as ``x-goog-api-key`` for a
``GEMINI_GENERATE_CONTENT`` provider. A provider that only takes its key in
another header, such as Anthropic's ``x-api-key``, cannot be authenticated this
way.

A request that names no model, or one no route lists, is rejected with a
``404``.

Statistics
----------

The filter outputs statistics in the ``ai_protocol_manager.model_router.`` namespace:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  success, Counter, The requested model matched a route and its candidates were stored in filter state.
  model_name_missing, Counter, "The request named no model, or no ``envoy.ai.model.request`` object was set, and it was rejected with a ``404``."
  route_not_found, Counter, No route matched the requested model and the request was rejected with a ``404``.
