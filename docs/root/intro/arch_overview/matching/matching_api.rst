.. _arch_overview_matching_api:

Matching API
============

Envoy makes use of a matching API to allow the various subsystems to express actions that should be performed based on incoming data.

The matching API is designed as a tree structure to allow for sublinear matching algorithms, and make heavy use of extension points to make it
easy to extend to different inputs based on protocol or environment data, sublinear matchers and direct matchers. 

Within supported environments (currently only HTTP filters), a wrapper proto can be used to instantiate a matching filter associated with the
wrapped structure:

.. code-block:: yaml

    "@type": type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher
    extension_config:
        name: response-filter-config
        typed_config:
            "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig
            code: 403
    matcher:
        matcher_tree:
            input:
                name: request-headers
                typed_config:
                    "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                    header_name: some-header
            exact_match_map:
                # Note this additional indirection; this is a workaround for Protobuf oneof limitations.
                map:
                    skip_filter: # This is the header value we're trying to match against.
                        action:
                            name: skip
                            typed_config:
                                "@type": type.googleapis.com/envoy.extensions.filters.common.matcher.action.v3.SkipFilter

The above example wraps a HTTP filter (the SetResponseCode filter) in an ExtensionWithMatcher, allowing us to define
a match tree to be evaluated in conjunction with evaluation of the wrapped filter. Prior to data being made available
to the filter, it will be provided to the match tree, which will then attempt to evaluate the matching rules with
the provided data, triggering an action if match evaluation completes in an action.

In the above example, we are specifying that we want to match on the incoming request header `some-header` by setting the `input` to
:ref:`HttpRequestHeaderMatchInput <envoy_v3_api_msg_type.matcher.v3.HttpRequestHeaderMatchInput>` and configuring the header key to use.
Using the value contained by this header, the provided `exact_match_map` specifies which values we care about: we've configured a single
value (`skip_filter`) to match against. As a result, this config means that if we receive a request which contains `some-header: skip_filter`
as a header, the :ref:`SkipFilter <envoy_v3_api_msg_extensions.filters.common.matcher.action.v3.SkipFilter>` action will be resolved (causing
the associated HTTP filter to be skipped). If no such header is present, no action will be resolved and the filter will be applied as usual.

.. code-block:: yaml

    "@type": type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher
    extension_config:
        name: response-filter-config
        typed_config:
            "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig
            code: 403
    matcher:
        matcher_tree:
            input:
                name: request-headers
                typed_config:
                    "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                    header_name: some-header
            exact_match_map:
                # Note this additional indirection; this is a workaround for Protobuf oneof limitations.
                map:
                    skip_filter: # This is the header value we're trying to match against.
                        matcher_list:
                        matchers:
                        - predicate:
                            or_matcher:
                                predicate:
                                - single_predicate:
                                input:
                                        name: request-headers
                                        typed_config:
                                            "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                                            header_name: second-header
                                    value_match:
                                        exact_match: foo
                                - single_predicate:
                                input:
                                        name: request-headers
                                        typed_config:
                                            "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                                            header_name: second-header
                                    value_match:
                                        exact_match: bar
                            action:
                                name: skip
                                typed_config:
                                    "@type": type.googleapis.com/envoy.extensions.filters.common.matcher.action.v3.SkipFilter

Above is a slightly more complicated example which combines a top level tree matcher with a linear matcher. While the tree matchers provide
very efficient matching, they are not very expressive. The list matcher can be used to provide a much richer matching API, and can be combined
with the tree matcher in an arbitrary order. The example describes the following match logic: skip the filter if `some-header: skip_filter`
is present and `second-header` is set to *either* `foo` or `bar`.

HTTP Filter Iteration Impact
============================

The above example only demonstrates matching on request headers, which ends up being the simplest case due to it happening before the associated
filter receives any data. Matching on other HTTP input sources is supported (e.g. response headers), but some discussion is warranted on how this
works at a filter level. 

Currently the match evaluation for HTTP filters does not impact control flow at all: if insufficient data is available to perform the match,
callbacks will be sent to the associated filter as normal. Once sufficient data is available to match an action, this is provided to the filter.
A consequence of this is that if the filter wishes to gate some behavior on a match result, it has to manage stopping the iteration on its own.

When it comes to actions such as SkipFilter, this means that if the skip condition is based on anything but the request headers, the filter might
get partially applied until the match result is ready. This might result in surprising beahvior.