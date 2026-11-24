.. _config_http_ai_filters_route_selector:

Route selector
==============

The route selector AI filter applies, on each upstream attempt, the candidate
the :ref:`model router <config_http_ai_filters_model_router>` AI filter resolved
for it: the first candidate on the first attempt, the second on the first retry,
and so on. It sets the request's authority to the candidate's provider and
leaves the candidate's wire API, model and binding as the
``envoy.ai.llm_protocol.response``, ``envoy.ai.model.resolved`` and
``envoy.ai.binding.response`` :ref:`transcoding instructions
<config_http_ai_filters_transcoder_instructions>`.

The filter always removes the client's ``authorization``, ``x-api-key`` and
``x-goog-api-key`` headers, whether or not the candidate has a credential. It
then sends the candidate's credential, if it has one, in ``authorization``
(``x-goog-api-key`` for ``GEMINI_GENERATE_CONTENT``). A candidate without an
:ref:`api_key <envoy_v3_api_field_extensions.http.ai_filters.model_router.v3.Model.api_key>`
is sent upstream unauthenticated.

* This AI filter should be configured with the type URL
  ``type.googleapis.com/envoy.extensions.http.ai_filters.route_selector.v3.RouteSelector``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.http.ai_filters.route_selector.v3.RouteSelector>`

Configuration
-------------

The filter reads the attempt count, so it runs in an AI Protocol Manager placed
in the router's upstream HTTP filter chain, ahead of the
:ref:`transcoder <config_http_ai_filters_transcoder>` instances that apply its
instructions. This is the upstream half of the configuration the
:ref:`model router <config_http_ai_filters_model_router>` page shows:

.. literalinclude:: _include/model-routing.yaml
    :language: yaml
    :lines: 70-94
    :lineno-start: 70
    :emphasize-lines: 10-12
    :linenos:
    :caption: :download:`model-routing.yaml <_include/model-routing.yaml>`

An attempt with no candidate, because the model router did not run or the
retries outnumber the candidates, fails with a ``500``.

Statistics
----------

The filter outputs statistics in the ``ai_protocol_manager.route_selector.`` namespace:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  success, Counter, The attempt's candidate was applied to the request.
  invalid_route_data, Counter, "No candidate was resolved for the attempt, or it named no wire API; the request failed with a ``500``."
