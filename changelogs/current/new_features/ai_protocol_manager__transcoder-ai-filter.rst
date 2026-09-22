Added :ref:`envoy.http.ai_filters.transcoder
<envoy_v3_api_msg_extensions.http.ai_filters.transcoder.v3.Transcoder>` (work in progress), an
``envoy.http.ai_filters`` extension that converts a declared AI endpoint's parsed request payload
between a vendor's schema and the canonical OpenAI Chat Completions intermediate representation
(``TO_IR`` and ``FROM_IR``), validating the converted payload against the target's request schema
before it is replayed upstream.
