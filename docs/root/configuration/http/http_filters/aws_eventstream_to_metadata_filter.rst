.. _config_http_filters_aws_eventstream_to_metadata:

AWS-EventStream-To-Metadata Filter
==================================
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.aws_eventstream_to_metadata.v3.AwsEventstreamToMetadata``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.aws_eventstream_to_metadata.v3.AwsEventstreamToMetadata>`

The AWS-EventStream-To-Metadata filter extracts values from AWS EventStream HTTP response bodies and writes them to dynamic metadata.
Currently, the filter processes response bodies only. This is particularly useful for observability, logging, and
custom filters that need to access values from AWS streaming responses (e.g., AWS Bedrock streaming responses).

The filter uses a **typed extension architecture** for content parsing, allowing pluggable parser implementations
for different data formats. The filter handles the AWS EventStream binary protocol parsing, while content parsers handle the payload
format (e.g., JSON, XML, protobuf).

The filter is configured with:

* A **content parser** that specifies how to parse and extract values from message payloads (e.g., JSON parser)
* **Rules** within the content parser that define selector paths and metadata actions
* Configuration for the EventStream protocol (max buffer size)

When a rule matches, the extracted value is written to the configured metadata namespace and key.
The metadata can then be consumed from access logs, used by custom filters, exported to metrics systems, or
attached to trace spans.

Use Cases
---------

**Observability and Cost Tracking for AWS Bedrock**

AWS Bedrock streaming APIs return token usage information at the end of streaming responses using the EventStream protocol.
This filter can extract the token count and other metadata, making it available for logging, metrics, and observability:

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.aws_eventstream_to_metadata
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.aws_eventstream_to_metadata.v3.AwsEventstreamToMetadata
      response_rules:
        content_parser:
          name: envoy.content_parsers.json
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
            rules:
            - rule:
                selectors:
                - key: "amazon-bedrock-invocationMetrics"
                - key: "inputTokenCount"
                on_present:
                  metadata_namespace: envoy.lb
                  key: input_tokens
                  type: NUMBER
            - rule:
                selectors:
                - key: "amazon-bedrock-invocationMetrics"
                - key: "outputTokenCount"
                on_present:
                  metadata_namespace: envoy.lb
                  key: output_tokens
                  type: NUMBER

In this example, the filter extracts ``inputTokenCount`` and ``outputTokenCount`` from the EventStream messages and writes them to
the ``envoy.lb`` metadata namespace. This metadata can then be:

* **Logged**: Access logs can reference dynamic metadata using ``%DYNAMIC_METADATA(envoy.lb:input_tokens)%``
* **Exported to metrics**: Custom stats sinks can consume the metadata
* **Used by custom filters**: Downstream filters can read and act on this metadata
* **Sent to tracing systems**: Metadata can be attached to trace spans

How It Works
------------

For AWS EventStream format with JSON content parser:

1. The filter checks the response ``Content-Type`` header against the expected type (``application/vnd.amazon.eventstream``).
   Matching is performed on the media type only, ignoring parameters.
2. It parses the EventStream binary protocol according to the `AWS EventStream specification <https://smithy.io/2.0/aws/amazon-eventstream.html>`_,
   validating CRC checksums and properly handling messages split across multiple data chunks
3. For each complete EventStream message, it extracts the payload bytes and delegates to the configured **content parser**
4. The JSON content parser parses the payload as JSON and navigates the object using the configured selectors
5. Based on the result, it writes metadata according to the configured rules defined in the content parser:

   * **on_present**: Executes immediately when the selector successfully extracts a value from any message
   * **on_missing**: Deferred until end-of-stream. Executes only if ``on_present`` never executed and the selector path was not found
   * **on_error**: Deferred until end-of-stream. Executes only if ``on_present`` never executed and a parse error occurred

6. The deferred execution of ``on_missing`` and ``on_error`` ensures that early messages without the desired field don't prevent later successful extractions

Configuration
-------------

Complete Example
~~~~~~~~~~~~~~~~

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.aws_eventstream_to_metadata
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.aws_eventstream_to_metadata.v3.AwsEventstreamToMetadata
      response_rules:
        content_parser:
          name: envoy.content_parsers.json
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
            rules:
            - rule:
                selectors:
                - key: "usage"
                - key: "total_tokens"
                on_present:
                  metadata_namespace: envoy.lb
                  key: tokens
                  type: NUMBER
        max_buffer_size: 1048576  # 1MB (default)

Key Configuration Options
~~~~~~~~~~~~~~~~~~~~~~~~~

**response_rules**
  Configuration for processing EventStream response streams. Contains:

**response_rules.content_parser**
  A :ref:`typed extension <envoy_v3_api_msg_config.core.v3.TypedExtensionConfig>` that specifies how to parse
  and extract values from message payloads. Available parsers:

  * **envoy.content_parsers.json**: Parses JSON content and extracts values using JSONPath-like selectors.
    See :ref:`v3 API reference <envoy_v3_api_msg_extensions.content_parsers.json.v3.JsonContentParser>`
    for configuration options.

**JSON Content Parser Configuration**

When using ``envoy.content_parsers.json``, configure rules within the typed_config:

**rules**
  A list of rules to apply. Each rule contains:

  * **rule**: The json-to-metadata rule configuration with the following fields:

    - **selectors**: A list of selectors that specifies how to extract a value from the JSON payload.
    - **on_present**: Metadata to write when the selector successfully extracts a value.
    - **on_missing**: Metadata to write when the selector path is not found.
    - **on_error**: Metadata to write when a parse error occurs.

**response_rules.max_buffer_size**
  Maximum size in bytes for incomplete (not yet parseable) buffered data before it is discarded.
  The limit is evaluated **after** the filter has consumed all complete EventStream messages from the
  buffer, so a single large read from the kernel that contains many valid frames will be processed
  normally even if the total byte count momentarily exceeds this value. Only the residual bytes that
  do not yet form a complete message are subject to the size check.

  Default is 1048576 bytes (1 MB), which is sufficient for most legitimate messages.
  Set to 0 to disable the limit (not recommended for production).
  Maximum allowed value is 26214400 bytes (25 MB, slightly larger than the AWS EventStream
  maximum payload size of 24 MB).

Statistics
----------

The aws_eventstream_to_metadata filter outputs statistics in the ``http.<stat_prefix>.aws_eventstream_to_metadata.resp.<parser_prefix>*`` namespace.
The :ref:`stat prefix <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
comes from the owning HTTP connection manager, and ``<parser_prefix>`` comes from the content parser (e.g., ``json.`` for the JSON parser).

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  resp.<parser_prefix>.metadata_added, Counter, Total number of metadata entries successfully written
  resp.<parser_prefix>.metadata_from_fallback, Counter, "Total number of metadata entries written using on_missing or on_error fallback values"
  resp.<parser_prefix>.mismatched_content_type, Counter, Total number of responses with content types that don't match the expected type
  resp.<parser_prefix>.empty_payload, Counter, Total number of EventStream messages with empty payloads
  resp.<parser_prefix>.parse_error, Counter, Total number of messages where the content parser failed to parse the payload
  resp.<parser_prefix>.preserved_existing_metadata, Counter, Total number of times metadata was not written due to preserve_existing_metadata_value being true
  resp.<parser_prefix>.buffer_too_large, Counter, Total number of times buffered data exceeded max_buffer_size and was discarded
  resp.<parser_prefix>.eventstream_error, Counter, "Total number of EventStream protocol errors (CRC mismatch, invalid format)"

AWS EventStream Protocol
------------------------

The filter implements the `AWS EventStream specification <https://smithy.io/2.0/aws/amazon-eventstream.html>`_:

* **Binary Protocol**: Parses the binary message format with prelude, headers, payload, and trailer
* **CRC Validation**: Validates both prelude CRC and message CRC to detect corruption
* **Message Framing**: Properly handles message boundaries and incomplete messages
* **Chunked Transfer**: Handles messages split across multiple TCP packets/HTTP chunks

Security Considerations
-----------------------

* The ``max_buffer_size`` configuration (default: 1 MB) limits how much incomplete data the filter
  will buffer while waiting for a full EventStream message. Because the check runs after all
  complete messages have been consumed, it guards specifically against a slow or malicious upstream
  that sends an unbounded partial message without ever completing it.
* CRC validation protects against corrupt or tampered messages
* For production deployments, it's recommended to keep ``max_buffer_size`` at a reasonable value
* Setting ``max_buffer_size: 0`` disables the limit but is not recommended for untrusted upstream sources
