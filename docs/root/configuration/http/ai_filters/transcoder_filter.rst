.. _config_http_ai_filters_transcoder:

Transcoder
==========

The transcoder AI filter rewrites the parsed payload into the request schema of
the AI backend the route targets, so a client may speak one vendor's API while
the upstream speaks another.

Translation runs through a single canonical schema, OpenAI Chat Completions, rather than
pairwise between vendors. Supporting a new dialect therefore costs one mapping rather than
one per pair, and AI filters placed between the two legs only ever see the canonical shape.

* This AI filter should be configured with the type URL
  ``type.googleapis.com/envoy.extensions.http.ai_filters.transcoder.v3.Transcoder``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.http.ai_filters.transcoder.v3.Transcoder>`

Configuration
-------------

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.ai_protocol_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
      request_handling: {}
      filters:
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

Two instances of the filter bracket the intermediate AI filters across both the request and
response (unary and SSE streaming) pipelines: the client-boundary instance sets
``request_handling: TO_IR`` and ``response_handling: FROM_IR`` to convert between the client's
declared protocol and the canonical OpenAI Chat Completions IR, and the backend-boundary instance
sets ``request_handling: FROM_IR`` and ``response_handling: TO_IR`` to convert between the
canonical IR and the target backend's schema. Filters configured between the two therefore only
ever see the canonical form. Leaving either ``request_handling`` or ``response_handling`` unset
(``DIRECTION_UNSPECIFIED``) disables transcoding on that leg so payloads pass through untouched.

The client-boundary instance converts from and to the API the route declares for the
:ref:`request <envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManagerPerRoute.request>`,
and the backend-boundary instance to and from the one it declares for the
:ref:`response <envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManagerPerRoute.response>`.
A leg whose client API nobody declares cannot be transcoded, which is counted by
``unresolved``. A backend API nobody declares -- neither the route nor
``envoy.ai.llm_protocol.response`` -- is taken to be the client's, so a route with no backend,
such as one answered locally, leaves the request as it came; this is counted by
``backend_defaulted``, and a route that forwards to a backend of another API shows up
there when its ``response`` declaration is missing.

The rewritten request is validated against the target's schema before it is
replayed, so a document the upstream would reject fails locally rather than over the
network. Such a rejection is counted by ``failed``.

Responses are converted one unary body or one SSE event at a time. An event may become none
(Anthropic's ``ping``, for example) or several, and a stream that ends without the terminator
the client's API expects is given one: an OpenAI client of a Gemini backend, whose streams just
end, still receives ``data: [DONE]``. A response or event that cannot be converted is forwarded
untranslated, since part of the response may already be on its way to the client, and is
counted by ``failed``.

.. _config_http_ai_filters_transcoder_instructions:

Per-request transcoding instructions
------------------------------------

The route decides the backend for every request it matches. A filter ahead of the transcoder --
a model router, ext_proc, or :ref:`set_filter_state <config_http_filters_set_filter_state>` --
can override that decision for one request by leaving instructions in filter state. Each key is
independent, and a key that is not set leaves today's behavior in place.

``envoy.ai.llm_protocol.response``
  The backend's wire API, replacing the route's ``response`` declaration. An :ref:`LLMProtocol
  <envoy_v3_api_enum_type.ai.v3.LLMProtocol>` enum-value name, e.g. ``GEMINI_GENERATE_CONTENT``.
  Response token-usage extraction follows it too, so the two never disagree about the backend.

``envoy.ai.binding.request``
  How the client's request carries its API over HTTP. The ``TO_IR`` leg uses this binding to read
  the client's path, body, and headers (without rewriting the path). If unset, Envoy falls back to
  the route's binding, and then to the client API's built-in bindings (e.g., native Gemini or
  Anthropic paths and their Vertex AI equivalents). Set this when the gateway exposes an API under
  a custom path prefix.

``envoy.ai.binding.response``
  How the backend expects its API over HTTP. The ``FROM_IR`` leg uses this binding to render
  ``:path`` and apply body and header edits, overriding the route's binding and the backend API's
  default binding (for example, to target a Gemini backend on Vertex AI). If neither this nor the
  route sets a binding, and the backend speaks the same API as the client, the request keeps the
  binding it arrived with.

``envoy.ai.model.resolved``
  The model to send upstream in place of the one the client asked for. It reaches the body and
  the rendered path alike. The response still reports the model the client asked for; which
  backend served it stays with the gateway.

A route declares its bindings beside each ``llm_protocol``, in :ref:`request.binding
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.RequestPerRoute.binding>` and
:ref:`response.binding
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.ResponsePerRoute.binding>`,
which are checked when the configuration loads. A route's binding is how that side carries the
route's API, so it lapses when filter state names another API for the side: a router that sends a
request to another backend API names that backend's binding too, or the API's own applies. A
route's binding that no AI filter of the chain applies is counted by the :ref:`AI Protocol Manager
<config_http_filters_ai_protocol_manager>` statistic ``route_binding_unused``.

A binding is written as a URI pattern, starting with ``/``, or as an :ref:`LLMBinding
<envoy_v3_api_msg_type.ai.v3.LLMBinding>` in JSON, starting with ``{``. A part the binding leaves
empty is the API's own: a pattern alone keeps the edits of the API's own binding, such as Gemini
removing ``model`` and ``stream`` from the body, or Anthropic adding its ``anthropic-version``
header.

A URI pattern is a path in the :ref:`URI template syntax
<envoy_v3_api_msg_extensions.path.match.uri_template.v3.UriTemplateMatchConfig>` of
``path_match_policy``, with one addition: ``{unary|stream}`` spells out, once, the part of the
path that differs between a unary and a streaming request; either side may be empty, and a side
may end in a fixed query string, which rendering appends and matching ignores. Several layouts
may be listed separated by spaces: a path matches the first that fits it entirely, and rendering
writes the first.

Every named variable, written ``{name}`` or ``{name=*}``, matches one path segment and reads its
value, which must hold only letters, digits and ``-``, ``.``, ``_``, ``@``, and start with a letter
or a digit; a fixed segment is written as text. ``{model}`` is the request's ``model``, and the
choice its ``stream`` where the two sides differ in the path; a choice between queries alone leaves
the streaming mode to the body. Any other variable, such as ``{project}``, is a path variable: it
never enters the body, and the ``envoy.ai.binding.read`` object the transcoder sets for the stream
keeps it for field access, e.g. ``%FILTER_STATE(envoy.ai.binding.read:FIELD:location)%``.
Rendering takes ``{model}`` from the body and every other variable from the client's path; unnamed
wildcards match on the way in but cannot be rendered.

.. code-block:: text

  /v1beta/models/{model}:{generateContent|streamGenerateContent?alt=sse}
  /{version}/projects/{project}/locations/{location}/publishers/google/models/{model}:{generateContent|streamGenerateContent?alt=sse}
  /gateway/{model}/{chat|chat-stream}
  /v1/messages

On the way in, the path's value wins for a field the binding removes from the body, and the
body's wins otherwise; the body fields and headers the binding writes are taken out, so Vertex
AI's ``anthropic_version`` field and Anthropic's ``anthropic-version`` header never reach another
API. On the way out, the body is validated against the backend API's schema first, then the
binding's edits apply in order: to the body, and to the request headers. A request that leaves in
the binding it arrived in gets those fields and headers back as the client sent them.

A binding that does not parse, or one that needs a path variable the client's path did not hold,
is a misconfiguration of the proxy, not of the client: the request fails with a 500, counted by
``unresolved``. A request binding that does not match the path reads nothing and is
logged at debug.

The instructions are read once, when the transcoder's request leg runs, so the filter that
writes them must run before that instance of the transcoder; a model router placed between the
two instances in the example above writes them in time for the backend-boundary one. Writes
during the response have no effect.

The example pins a route to Claude on an illustrative gateway, statically, with
``set_filter_state``; a model router would write the same keys per request.

.. code-block:: yaml

  routes:
  - match: { prefix: /anthropic/ }
    route: { cluster: claude_gateway }
    typed_per_filter_config:
      envoy.filters.http.set_filter_state:
        "@type": type.googleapis.com/envoy.extensions.filters.http.set_filter_state.v3.Config
        on_request_headers:
        - object_key: envoy.ai.llm_protocol.response
          format_string:
            text_format_source:
              inline_string: "ANTHROPIC_MESSAGES"
        - object_key: envoy.ai.binding.response
          format_string:
            text_format_source:
              inline_string: "/gateway/anthropic/{model}/{messages|messages-stream}"
        - object_key: envoy.ai.model.resolved
          format_string:
            text_format_source:
              inline_string: "claude-sonnet-4-5"

A binding in JSON also edits the body and the request headers, with the same
:ref:`KeyValueMutation <envoy_v3_api_msg_config.core.v3.KeyValueMutation>` and
:ref:`HeaderMutation <envoy_v3_api_msg_config.common.mutation_rules.v3.HeaderMutation>`
messages other filters use. Claude on Vertex AI takes the model in the path alone and the
Vertex AI API version in the body, not in the API's header:

.. code-block:: json

  {
    "uri_pattern": "/v1/projects/my-project/locations/us-east5/publishers/anthropic/models/{model}:{rawPredict|streamRawPredict}",
    "body_mutations": [
      {"remove": "model"},
      {"append": {"record": {"key": "anthropic_version", "value": "vertex-2023-10-16"}, "action": "ADD_IF_ABSENT"}}
    ],
    "header_mutations": [
      {"remove": "anthropic-version"}
    ]
  }

Envoy's built-in ``ANTHROPIC_MESSAGES`` binding sets the ``anthropic-version`` header
automatically, so requests to Anthropic need no extra path or header configuration:

.. code-block:: json

  {
    "uri_pattern": "/v1/messages",
    "header_mutations": [
      {"append": {"header": {"key": "anthropic-version", "value": "2023-06-01"}, "append_action": "ADD_IF_ABSENT"}}
    ]
  }

A binding's mutations apply on top of the API's own, header edits first, so a binding that only
adds a header keeps the API's body edits. A mutation naming a field or header the API's also
name replaces that one edit, which is how the Vertex binding above removes the API's version
header. The action is named explicitly: ``OVERWRITE_IF_EXISTS_OR_ADD`` sets, ``ADD_IF_ABSENT``
sets where absent, ``remove`` removes. The enum default ``APPEND_IF_EXISTS_OR_ADD``,
``OVERWRITE_IF_EXISTS``, ``remove_on_match``, ``raw_value`` and the deprecated ``append`` and
``entry`` fields are rejected for now. A value is not a format string: only ``{model}`` and the
client's path variables are replaced.

Use header mutations only for non-secret API headers; pass credentials via
``request_headers_to_add`` or :ref:`credential_injector <config_http_filters_credential_injector>`.

Statistics
----------

The filter outputs statistics in the ``ai_protocol_manager.transcoder.`` namespace:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  transcoded, Counter, "The transcoder AI filter converted a payload (a request, a unary response or an SSE event) into or out of the canonical IR."
  backend_defaulted, Counter, "Neither the route nor filter state named the backend's wire API, so the transcoder took it to be the client's and left the request as it came."
  unresolved, Counter, "A payload could not be transcoded because nothing declared the client's wire API, or an instruction in filter state could not be followed. A request is rejected; a response is forwarded untranslated and counted once, however many SSE events it has."
  failed, Counter, "A payload could not be converted: its rules failed, or a converted request failed the target schema. A request is rejected; a response or SSE event is forwarded untranslated."
