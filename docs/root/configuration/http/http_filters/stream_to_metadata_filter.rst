.. _config_http_filters_stream_to_metadata:

Stream-To-Metadata Filter
=========================
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.stream_to_metadata.v3.StreamToMetadata``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.stream_to_metadata.v3.StreamToMetadata>`

The Stream-To-Metadata filter extracts values from streaming HTTP bodies and writes them to dynamic metadata.
Currently, the filter processes response bodies only. This is particularly useful for observability and rate limiting
based on values that only appear in streaming responses.

The filter is configured with rules that specify:

* The streaming format to parse (currently only Server-Sent Events (SSE) is supported)
* A selector path to extract values from JSON payloads within the stream
* The namespace(s) for extracted values in dynamic metadata to be written to.

When a rule matches, the extracted value is written to the configured metadata namespace and key.
The metadata can then be used for rate limiting decisions, consumed from logs, custom routing, or other purposes.

Use Cases
---------

**Token-Based Rate Limiting for LLM APIs**

Large Language Model (LLM) APIs like OpenAI return token usage information at the end of streaming responses.
This filter can extract the token count and make it available for rate limiting:

.. literalinclude:: _include/stream-to-metadata-filter.yaml
    :language: yaml
    :lines: 33-51
    :lineno-start: 33
    :linenos:
    :emphasize-lines: 7-14

In this example, the filter extracts ``total_tokens`` from the JSON path ``usage.total_tokens`` in the
SSE stream and writes it to metadata namespace ``envoy.lb`` with key ``tokens``. This metadata can then
be used in rate limit descriptors (shown in lines 26-31).

**Cost Tracking and Observability**

Extract multiple values from streaming responses for logging and monitoring:

.. code-block:: yaml

  response_rules:
    rules:
    - selector:
        json_path:
          path: ["usage", "total_tokens"]
      on_present:
      - metadata_namespace: envoy.audit
        key: tokens
        type: NUMBER
    - selector:
        json_path:
          path: ["model"]
      on_present:
      - metadata_namespace: envoy.audit
        key: model_name
        type: STRING
      stop_processing_on_match: false

How It Works
------------

For Server-Sent Events (SSE) format:

1. The filter checks the response ``Content-Type`` header against allowed content types (default: ``text/event-stream``).
   Matching is performed on the media type (type/subtype) only, ignoring parameters like ``charset``.
2. It parses the SSE stream according to the `SSE specification <https://html.spec.whatwg.org/multipage/server-sent-events.html>`_,
   handling CRLF, CR, and LF line endings, and properly managing events split across multiple data chunks
3. For each complete SSE event, it extracts the value from the ``data`` field(s) and parses it as JSON
4. It navigates the JSON object using the configured selector path (e.g., ``["usage", "total_tokens"]``)
5. Based on the result, it writes metadata according to the configured rules:

   * **on_present**: Executes immediately when the selector successfully extracts a value from any event
   * **on_missing**: Deferred until end-of-stream. Executes only if ``on_present`` never executed and the selector path was not found in at least one event
   * **on_error**: Deferred until end-of-stream. Executes only if ``on_present`` never executed and an error occurred (JSON parse failure, no data field, content-type mismatch, etc.). Takes priority over ``on_missing`` if both conditions are met

6. The deferred execution of ``on_missing`` and ``on_error`` ensures that early events without the desired field (common in LLM streams) don't prevent later successful extractions
7. By default (``stop_processing_on_match: false``), it processes the entire stream. Set to ``true`` to stop
   after the first match (only ``on_present`` triggers this), which is more efficient when you know the desired value appears early or only once

Configuration
-------------

Complete Example
~~~~~~~~~~~~~~~~

.. literalinclude:: _include/stream-to-metadata-filter.yaml
    :language: yaml
    :lines: 33-51
    :lineno-start: 33
    :linenos:
    :caption: :download:`stream-to-metadata-filter.yaml <_include/stream-to-metadata-filter.yaml>`

Key Configuration Options
~~~~~~~~~~~~~~~~~~~~~~~~~~

**response_rules**
  Configuration for processing response streams. Contains:

**response_rules.format**
  The streaming format to parse. Currently only :ref:`SERVER_SENT_EVENTS
  <envoy_v3_api_enum_value_extensions.filters.http.stream_to_metadata.v3.StreamToMetadata.Format.SERVER_SENT_EVENTS>`
  is supported.

**response_rules.rules**
  A list of rules to apply. Each rule contains:

  * **selector**: Specifies how to extract a value from the stream payload. Currently supports:

    - **json_path**: A path through the JSON object (e.g., ``["usage", "total_tokens"]`` extracts
      ``json_object["usage"]["total_tokens"]``)

  * **on_present**: Metadata to write when the selector successfully extracts a value.
    Executes immediately when a match is found. Each descriptor specifies:

    - ``metadata_namespace``: The metadata namespace (e.g., ``envoy.lb``). If empty, defaults to ``envoy.filters.http.stream_to_metadata``.
    - ``key``: The metadata key
    - ``value``: Optional hardcoded value. If set, writes this instead of the extracted value.
    - ``type``: The value type (``PROTOBUF_VALUE``, ``STRING``, or ``NUMBER``)
    - ``preserve_existing_metadata_value``: If true, don't overwrite existing metadata. Default false.

  * **on_missing**: Metadata to write when the selector path is not found in the JSON.
    Executes at end-of-stream if ``on_present`` never executed. Useful for rate limiting: write a fallback value (e.g., -1) when token count is missing.
    **Must** have ``value`` set to a fallback. This handles the case where legitimate JSON exists but lacks the expected field.

  * **on_error**: Metadata to write when an error occurs (JSON parse failure, no data field, content-type mismatch, etc.).
    Executes at end-of-stream if ``on_present`` never executed and takes priority over ``on_missing``. Useful for rate limiting: write a safe default (e.g., 0) to avoid blocking on errors.
    **Must** have ``value`` set to a fallback. This handles malformed or missing data.

  .. note::

    At least one of ``on_present``, ``on_missing``, or ``on_error`` must be specified in each rule.
    The ``on_missing`` and ``on_error`` actions are deferred and only execute at the end of the stream if ``on_present`` never executes.
    This prevents early error/missing events from overwriting later successful extractions (common in LLM streams where usage data appears in the final event).

  * **stop_processing_on_match**: If true, stop processing the stream after this rule successfully matches (``on_present`` executes).
    If false (default), continue processing the entire stream. When combined with preserve_existing_metadata_value=false,
    later matches overwrite earlier ones (picks LAST occurrence). Set to true for better performance when you only need the first matching value.
    Note: ``on_missing`` and ``on_error`` do not trigger this.

**response_rules.allowed_content_types**
  A list of content types to process. Defaults to ``["text/event-stream"]`` for SSE format.
  Content-Type matching is performed on the media type (type/subtype) only, ignoring parameters
  such as ``charset``. For example, ``text/event-stream; charset=utf-8`` will match the
  configured type ``text/event-stream``.

**response_rules.max_event_size**
  Maximum size in bytes for a single event before it's considered invalid and discarded.
  This protects against unbounded memory growth from malicious or malformed streams that
  never send event delimiters (blank lines). Default is 8192 bytes (8KB). Set to 0 to
  disable the limit (not recommended for production). Maximum allowed value is 10485760 bytes (10MB).

Advanced Configurations
~~~~~~~~~~~~~~~~~~~~~~~

**Writing to Multiple Namespaces**

Write the same value to multiple metadata namespaces, useful during migrations:

.. code-block:: yaml

  response_rules:
    rules:
    - selector:
        json_path:
          path: ["usage", "total_tokens"]
      on_present:
      - metadata_namespace: old.namespace
        key: tokens
        type: NUMBER
      - metadata_namespace: new.namespace
        key: tokens
        type: NUMBER

**Preserving Existing Metadata**

Avoid overwriting previously set metadata values:

.. code-block:: yaml

  response_rules:
    rules:
    - selector:
        json_path:
          path: ["usage", "total_tokens"]
      on_present:
      - metadata_namespace: envoy.lb
        key: tokens
        type: NUMBER
        preserve_existing_metadata_value: true

**Using on_present, on_missing, and on_error Together**

Write fallback values when extraction fails to ensure metadata is always available for downstream processing:

.. code-block:: yaml

  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
    - selector:
        json_path:
          path: ["usage", "total_tokens"]
      on_present:
      - metadata_namespace: envoy.lb
        key: tokens
        type: NUMBER
      on_missing:
      - metadata_namespace: envoy.lb
        key: tokens
        value:
          number_value: -1
      on_error:
      - metadata_namespace: envoy.lb
        key: tokens
        value:
          number_value: 0

In this configuration:

* When the value is successfully extracted, it's written to metadata
* When the ``usage.total_tokens`` path doesn't exist in any event, ``-1`` is written at end-of-stream as a sentinel value
* When JSON parsing fails, no data field exists, or content-type mismatches, ``0`` is written at end-of-stream as a safe default
* The deferred execution ensures that error/missing states don't overwrite a successful extraction from a later event

**Custom Content Types**

Accept additional content types beyond the default:

.. code-block:: yaml

  response_rules:
    format: SERVER_SENT_EVENTS
    allowed_content_types:
    - "text/event-stream"
    - "application/stream+json"
    rules:
      # ... rules ...

.. note::

  Content-Type matching ignores parameters after the semicolon. The response header
  ``text/event-stream; charset=utf-8`` will match the configured type ``text/event-stream``.
  You only need to configure the base media type (type/subtype).

Statistics
----------

The stream_to_metadata filter outputs statistics in the ``http.<stat_prefix>.stream_to_metadata.*`` namespace.
The :ref:`stat prefix <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
comes from the owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  resp.success, Counter, Total number of values successfully extracted and written to metadata
  resp.mismatched_content_type, Counter, Total number of responses with non-allowed content types
  resp.no_data_field, Counter, Total number of SSE events without a data field
  resp.invalid_json, Counter, Total number of data fields that could not be parsed as JSON
  resp.selector_not_found, Counter, Total number of times the selector path was not found in the JSON
  resp.preserved_existing_metadata, Counter, Total number of times metadata was not written due to preserve_existing_metadata_value being true
  resp.event_too_large, Counter, Total number of events discarded because they exceeded max_event_size

SSE Specification Compliance
-----------------------------

The filter implements full `SSE specification <https://html.spec.whatwg.org/multipage/server-sent-events.html>`_ compliance:

* **Line Endings**: Supports CRLF (``\r\n``), CR (``\r``), and LF (``\n``) line endings, including mixed usage
* **Comments**: Lines starting with ``:`` are properly ignored as comments
* **Field Parsing**: Handles both ``data: value`` and ``data:value`` (with and without space after colon)
* **Multiple Data Fields**: Properly concatenates multiple ``data:`` lines with newlines per the specification
* **Field Ordering**: Correctly processes events regardless of field order
* **Chunked Transfer**: Handles events split across multiple TCP packets/HTTP chunks, properly buffering incomplete events

Performance Considerations
---------------------------

* The filter buffers incomplete SSE events in memory until they are complete
* Once a complete event is found, it is processed immediately and removed from the buffer
* By default, ``stop_processing_on_match: false`` processes the entire stream
* Set ``stop_processing_on_match: true`` to stop after the first match for better performance
  when you only need the first occurrence (e.g., a value that appears early in the stream)
* For extracting multiple different values, use multiple rules with ``stop_processing_on_match: false``

Security Considerations
-----------------------

* The ``max_event_size`` configuration (default: 8KB) protects against unbounded memory growth
  from malicious streams that never send event delimiters
* When an event exceeds ``max_event_size``, the buffered data is discarded and the
  ``event_too_large`` counter is incremented
* For production deployments, it's recommended to keep ``max_event_size`` at a reasonable value
  (the default 8KB is sufficient for most legitimate SSE events)
* Setting ``max_event_size: 0`` disables the limit but is not recommended for untrusted upstream sources
