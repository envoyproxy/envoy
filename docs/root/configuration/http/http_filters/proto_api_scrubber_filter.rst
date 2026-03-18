.. _config_http_filters_proto_api_scrubber:

Proto API Scrubber
==================

Overview
--------

The Proto API Scrubber filter provides deep content inspection and redaction (scrubbing) capability for gRPC traffic. Unlike generic HTTP filters that operate on headers or raw bytes, this filter understands the Protobuf schema of the traffic. It converts the incoming gRPC byte stream into structured messages, evaluates configured matchers against specific fields or messages, and removes sensitive data before forwarding the request or returning the response.

This filter supports:

* **Field-Level Scrubbing**: Removing specific fields from a request or response message based on dynamic criteria (e.g., headers, dynamic metadata).
* **Method-Level Access Control**: Blocking entire gRPC methods based on dynamic criteria.
* **Message-Level Scrubbing**: Scrubbing fields or entire messages based on the Protobuf message type, regardless of where that message appears (including inside ``google.protobuf.Any`` or nested fields).
* **Deep Inspection**: It handles repeated fields, maps, enums, and recursive message structures.

Design and Resources
--------------------

For detailed technical specifications and the evolution of this filter's capabilities, refer to the following design documents:

* `Proto API Scrubber Foundation RFC <https://docs.google.com/document/d/1jgRe5mhucFRgmKYf-Ukk20jW8kusIo53U5bcF74GkK8>`_: The original proposal defining the filter's motivation, its comparison to existing filters (like PME and gRPC Field Extraction), and the foundational implementation for field-level scrubbing.
* `Message and Method Level Filtering Extension <https://docs.google.com/document/d/1ewm0_kmA3eIQ-DIYY4RnBAGK4OxlxFeXX_nuds7EjBE>`_: An extension doc detailing the hierarchical restriction model. It introduces **Early Rejection** (denying requests at the header phase), global message-level restrictions, and deep inspection support for ``google.protobuf.Any`` types.

Configuration
-------------

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.ProtoApiScrubberConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.proto_api_scrubber.v3.ProtoApiScrubberConfig>`
* The filter configuration requires a Protobuf descriptor set to understand the schema of the traffic passing through it.

.. note::

  The filter relies on the :ref:`Unified Matcher API <arch_overview_matching_api>` to define the matching logic. While the API is extensible, this filter primarily uses standard Envoy inputs (headers, filter state, etc.) and matchers (String matcher, Common Expression Language (CEL) matcher, etc.) to make scrubbing decisions.

How it Works
------------

1. **Descriptor Loading**: The filter loads a ``FileDescriptorSet`` provided in the configuration. This allows the filter to decode binary gRPC payloads into structured data.
2. **Transcoding**: The filter buffers the gRPC stream, decodes the Protobuf payload, and traverses the message structure.
3. **Matching & Scrubbing**:

   * The filter evaluates restrictions in a top-down hierarchy: **Method-level** (early rejection), **Message-level** (global scrubbing), and finally **Field-level** (fine-grained control).
   * For every target field or message, it evaluates the associated **Matcher**.
   * If a Matcher evaluates to ``true`` and triggers a ``RemoveFieldAction``, the data is removed (scrubbed) from the payload.
   * For **Map** fields: The filter preserves the map keys but can scrub the map values.
   * For **Enums**: The filter can scrub based on the numeric value or the string name of the enum.
   * For **Any**: The filter unpacks ``google.protobuf.Any`` fields and applies **Message-Level** restrictions to the unpacked content.

4. **Re-encoding**: The modified message is re-serialized and sent downstream or upstream.

Restrictions Hierarchy
----------------------

The filter supports a hierarchy of restrictions:

1. **Method Restrictions**: Rules applied to a specific gRPC service method (e.g., ``/package.Service/Method``).

   * **Method Level**: Can block the entire method execution (returns 403 Forbidden) via early rejection.
   * **Field Level**: Targets specific fields within the Request or Response message of that method.

2. **Message Restrictions**: Rules applied to a specific Protobuf Message Type (e.g., ``package.SensitiveData``).

   * These rules apply globally whenever this message type is encountered, including inside nested fields or ``google.protobuf.Any`` payloads.
   * **Message Level**: Can scrub the entire message instance (clearing all its fields).
   * **Field Level**: Targets specific fields within this message type.

Field Masking Reference
-----------------------

When defining restrictions, the "key" used in the configuration identifies which part of the Protobuf structure to inspect. The following conventions are used for field masks:

.. list-table::
   :header-rows: 1
   :widths: 20 40 40

   * - Target Element
     - Masking Format
     - Example / Notes
   * - **Methods**
     - Fully qualified gRPC path.
     - ``/package.Service/MethodName``
   * - **Messages**
     - Fully qualified Protobuf message name.
     - ``package.MessageName``
   * - **Standard Fields**
     - Dot-notation path from the message root.
     - ``outer_field.inner_field``
   * - **Enum Values**
     - Path to the enum field followed by the string value name.
     - ``status.HIDDEN`` (targets the specific value ``HIDDEN`` in the ``status`` enum field)
   * - **Arrays**
     - Use the field name to target all child fields.
     - ``items.id`` targets the ``id`` field of every element in the ``items`` array
   * - **Maps**
     - Use the ``.value`` suffix to target values.
     - ``tags.value`` targets all map values. Map keys are always preserved
   * - **Any Type**
     - Target the underlying type via **Message Restrictions**.
     - A restriction on ``package.SecretType`` applies even when nested inside an ``Any`` field

Matcher Reference
-----------------

The filter supports various matchers from the :ref:`Unified Matcher API <arch_overview_matching_api>` to define when a restriction is applied. Here are some example matchers' configuration for the filter.

String Matcher (Exact)
^^^^^^^^^^^^^^^^^^^^^^

Matches a request attribute against an exact string.

.. code-block:: yaml

  predicate:
    single_predicate:
      input:
        name: envoy.matching.inputs.request_headers
        typed_config:
          "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
          header_name: "x-user-role"
      value_match:
        exact: "guest"

String Matcher (Regex)
^^^^^^^^^^^^^^^^^^^^^^

Matches a request attribute against a regular expression.

.. code-block:: yaml

  predicate:
    single_predicate:
      input:
        name: envoy.matching.inputs.request_headers
        typed_config:
          "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
          header_name: "user-agent"
      value_match:
        safe_regex:
          google_re2: {}
          regex: ".*(Bot|Crawler).*"

CEL Matcher
^^^^^^^^^^^

Evaluates a Common Expression Language (CEL) expression. Note that this filter requires the pre-parsed expression tree (``cel_expr_parsed``).

.. code-block:: yaml

  predicate:
    single_predicate:
      input:
        name: envoy.matching.inputs.cel_data_input
        typed_config:
          "@type": type.googleapis.com/xds.type.matcher.v3.HttpAttributesCelMatchInput
      custom_match:
        name: envoy.matching.matchers.cel_matcher
        typed_config:
          "@type": type.googleapis.com/xds.type.matcher.v3.CelMatcher
          expr_match:
            cel_expr_parsed:
              expr:
                id: 1
                const_expr:
                  bool_value: true # Example: Always match

Configuration Examples
----------------------

To make the examples easier to understand, consider the following hypothetical Protobuf definition for a Banking Service.

.. code-block:: protobuf

    syntax = "proto3";
    package bank;

    service BankingService {
      rpc GetTransaction(TransactionRequest) returns (TransactionResponse);
    }

    message TransactionRequest {
      string account_id = 1;
      string request_id = 2;
    }

    message TransactionResponse {
      string transaction_id = 1;
      // Sensitive: Only visible to "admin" users.
      string raw_credit_card_data = 2;
      // Sensitive: Internal debugging info, should never leave the mesh.
      Status category = 3;
      // A container for arbitrary details.
      google.protobuf.Any details = 4;
    }

    enum Status {
      PUBLIC = 0;
      INTERNAL = 1;
    }

    // A sensitive message type that might be inside 'details' (Any).
    message SensitiveAuditLog {
      string admin_user = 1;
      string operation = 2;
    }

Example 1: Scrubbing a Response Field based on Header
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In this scenario, we remove the ``raw_credit_card_data`` field from the ``GetTransaction`` response **unless** the downstream user has the header ``x-user-role: admin``.

.. code-block:: yaml

  name: envoy.filters.http.proto_api_scrubber
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.ProtoApiScrubberConfig
    descriptor_set:
      filename: "/etc/envoy/descriptors/bank.pb"
    filtering_mode: OVERRIDE
    restrictions:
      method_restrictions:
        # Key is the fully qualified method name
        "/bank.BankingService/GetTransaction":
          response_field_restrictions:
            # Key is the field path within the response message
            "raw_credit_card_data":
              matcher:
                matcher_list:
                  matchers:
                  - predicate:
                      # This CEL expression evaluates true if role is NOT admin.
                      single_predicate: { ... }
                    on_match:
                      action:
                        name: remove
                        typed_config:
                          "@type": type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction

Example 2: Scrubbing a Specific Enum Value
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Remove the ``category`` field only if its value is ``INTERNAL``.

.. code-block:: yaml

  restrictions:
    method_restrictions:
      "/bank.BankingService/GetTransaction":
        response_field_restrictions:
          "category.INTERNAL":
            matcher:
              matcher_list:
                matchers:
                - predicate:
                    single_predicate:
                      # Logic: true (always remove if value is INTERNAL)
                      cel_expr_parsed: { ... }
                  on_match:
                    action:
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction

Example 3: Message-Level Scrubbing (Handling ``google.protobuf.Any``)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The filter will automatically unpack the ``Any`` field, check its type, and apply relevant **Message Restrictions**. Here we scrub ``admin_user`` from any instance of ``SensitiveAuditLog``.

.. code-block:: yaml

  restrictions:
    message_restrictions:
      # Key is the fully qualified message type name
      "bank.SensitiveAuditLog":
        field_restrictions:
          "admin_user":
            matcher:
              matcher_list:
                matchers:
                - predicate:
                    single_predicate:
                      # Logic: true (unconditionally scrub)
                      cel_expr_parsed: { ... }
                  on_match:
                    action:
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction

Example 4: Blocking a Method entirely
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

You can block a method entirely based on a condition. If the matcher evaluates to true, the request is rejected immediately via early rejection with a 403 Forbidden.

.. code-block:: yaml

  restrictions:
    method_restrictions:
      "/bank.BankingService/GetTransaction":
        method_restriction:
          matcher:
            matcher_list:
              matchers:
              - predicate:
                  single_predicate:
                    input:
                      name: request-header-match
                      typed_config:
                        "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                        header_name: "x-block-method"
                    value_match:
                      exact: "true"
                on_match:
                  action:
                    typed_config:
                      "@type": type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction

Observability
-------------

The filter outputs statistics in the ``http.<stat_prefix>.proto_api_scrubber.`` namespace.

Statistics
^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 40 20 40

   * - Name
     - Type
     - Description
   * - ``total_requests``
     - Counter
     - Total number of HTTP requests processed by the filter, regardless of protocol or content-type.
   * - ``total_requests_checked``
     - Counter
     - Total number of valid gRPC requests inspected by the filter logic.
   * - ``method_blocked``
     - Counter
     - Total requests rejected due to method-level restrictions.
   * - ``request_scrubbing_failed``
     - Counter
     - Total requests where the scrubbing operation failed (e.g., malformed proto payload).
   * - ``response_scrubbing_failed``
     - Counter
     - Total responses where the scrubbing operation failed.
   * - ``request_buffer_conversion_error``
     - Counter
     - Errors encountered when converting Envoy buffers to Protobuf messages for requests.
   * - ``response_buffer_conversion_error``
     - Counter
     - Errors encountered when converting Envoy buffers to Protobuf messages for responses.
   * - ``invalid_method_name``
     - Counter
     - Requests rejected because the gRPC method name in the ``:path`` header was invalid or malformed.
   * - ``request_scrubbing_latency``
     - Histogram
     - Time in milliseconds spent traversing and scrubbing the request payload.
   * - ``response_scrubbing_latency``
     - Histogram
     - Time in milliseconds spent traversing and scrubbing the response payload.

Tracing
^^^^^^^

The filter sets the following tags on the active span to provide visibility into scrubbing operations:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Tag Name
     - Description
   * - ``proto_api_scrubber.outcome``
     - Set to ``blocked`` if the request was denied by a method-level restriction.
   * - ``proto_api_scrubber.request_error``
     - Contains the error message if the request scrubbing process failed.
   * - ``proto_api_scrubber.response_error``
     - Contains the error message if the response scrubbing process failed.

Response Code Details
---------------------

The filter populates the following `Response Code Details <config_access_log_format_response_code_details>`_ for observability and debugging:

.. list-table::
   :header-rows: 1

   * - Detail
     - HTTP Status
     - Description
   * - ``proto_api_scrubber_INVALID_ARGUMENT{BAD_REQUEST}``
     - 400 (Bad Request)
     - The gRPC method specified in the ``:path`` header could not be found in the configured descriptor set.
   * - ``proto_api_scrubber_Forbidden{METHOD_BLOCKED}``
     - 404 (Not Found)
     - A method-level restriction matcher evaluated to true, triggering an early rejection of the request.
   * - ``proto_api_scrubber_FAILED_PRECONDITION{REQUEST_BUFFER_CONVERSION_FAIL}``
     - 400 (Bad Request)
     - Failed to convert the internal Envoy buffer to a Protobuf message stream (e.g., message too large).
   * - ``proto_api_scrubber_FAILED_PRECONDITION{RESPONSE_BUFFER_CONVERSION_FAIL}``
     - 400 (Bad Request)
     - Failed to convert the internal Envoy buffer to a Protobuf message stream on the response path.
