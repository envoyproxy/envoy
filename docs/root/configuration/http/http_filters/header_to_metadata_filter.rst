.. _config_http_filters_header_to_metadata:

Envoy Header-To-Metadata Filter
===============================
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.header_to_metadata.v3.Config>`
* This filter should be configured with the name *envoy.filters.http.header_to_metadata*.

This filter is configured with rules that will be matched against requests and responses.
Each rule has either a cookie or a header and can be triggered either when the header
or cookie is present or missing.

When a rule is triggered, dynamic metadata will be added based on the configuration of the rule.
If the header or cookie is present, it's value is extracted and used along with the specified
key as metadata. If the header or cookie is missing, on missing case is triggered and the value
specified is used for adding metadata.

The metadata can then be used for load balancing decisions, consumed from logs, etc.

A typical use case for this filter is to dynamically match requests with load balancer
subsets. For this, a given header's value would be extracted and attached to the request
as dynamic metadata which would then be used to match a subset of endpoints.

Example
-------

A sample filter configuration to route traffic to endpoints based on the presence or
absence of a version header could be:

.. code-block:: yaml

  http_filters:
    - name: envoy.filters.http.header_to_metadata
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.header_to_metadata.v3.Config
        request_rules:
          - header: x-version
            on_header_present:
              metadata_namespace: envoy.lb
              key: version
              type: STRING
            on_header_missing:
              metadata_namespace: envoy.lb
              key: default
              value: 'true'
              type: STRING
            remove: false

As with headers, the value of the specified cookie will be extracted from the request
and added as metadata with the key specified.
Removing a cookie when a rule matches is unsupported.

.. code-block:: yaml

  http_filters:
    - name: envoy.filters.http.header_to_metadata
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.header_to_metadata.v3.Config
        request_rules:
          - cookie: cookie
            on_header_present:
              metadata_namespace: envoy.lb
              key: version
              type: STRING
            on_header_missing:
              metadata_namespace: envoy.lb
              key: default
              value: 'true'
              type: STRING
            remove: false


A corresponding upstream cluster configuration could be:

.. code-block:: yaml

  clusters:
    - name: versioned-cluster
      type: EDS
      lb_policy: ROUND_ROBIN
      lb_subset_config:
        fallback_policy: ANY_ENDPOINT
        subset_selectors:
          - keys:
              - default
          - keys:
              - version

This would then allow requests with the ``x-version`` header set to be matched against
endpoints with the corresponding version. Whereas requests with that header missing
would be matched with the default endpoints.

If the header's value needs to be transformed before it's added to the request as
dynamic metadata, this filter supports regex matching and substitution:

.. code-block:: yaml

  http_filters:
    - name: envoy.filters.http.header_to_metadata
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.header_to_metadata.v3.Config
        request_rules:
          - header: ":path"
            on_header_present:
              metadata_namespace: envoy.lb
              key: cluster
              regex_value_rewrite:
                pattern:
                  google_re2: {}
                  regex: "^/(cluster[\\d\\w-]+)/?.*$"
                substitution: "\\1"

Note that this filter also supports per route configuration:

.. code-block:: yaml

  route_config:
    name: local_route
    virtual_hosts:
    - name: local_service
      domains: ["*"]
      routes:
      - match: { prefix: "/version-to-metadata" }
        route: { cluster: service }
        typed_per_filter_config:
          envoy.filters.http.header_to_metadata:
            "@type": type.googleapis.com/envoy.extensions.filters.http.header_to_metadata.v3.Config
            request_rules:
              - header: x-version
                on_header_present:
                  metadata_namespace: envoy.lb
                  key: version
                  type: STRING
                remove: false
      - match: { prefix: "/" }
        route: { cluster: some_service }

This can be used to either override the global configuration or if the global configuration
is empty (no rules), it can be used to only enable the filter at a per route level.

Statistics
----------

Currently, this filter generates no statistics.
