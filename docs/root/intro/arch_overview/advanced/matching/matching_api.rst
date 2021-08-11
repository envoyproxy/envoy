.. _arch_overview_matching_api:

Matching API
============

.. attention::

   The matching API is alpha and is currently under active development.
   Capabilities will be expanded over time and the configuration structures are likely to change.

Envoy makes use of a :ref:`matching API <envoy_v3_api_msg_config.common.matcher.v3.Matcher>`
to allow the various subsystems to express actions that should be performed based on incoming data.

The matching API is designed as a tree structure to allow for sublinear matching algorithms for
better performance than the linear list matching as seen in Envoy's HTTP routing. It makes heavy
use of extension points to make it easy to extend to different inputs based on protocol or
environment data as well as custom sublinear matchers and direct matchers.

Within supported environments (currently only HTTP filters), a wrapper proto can be used to
instantiate a matching filter associated with the wrapped structure:

.. literalinclude:: _include/simple.yaml
    :language: yaml

The above example wraps a HTTP filter (the
:ref:`HTTPFault <envoy_v3_api_msg_extensions.filters.http.fault.v3.HttpFault>` filter) in an
:ref:`ExtensionWithMatcher <envoy_v3_api_msg_extensions.common.matching.v3.ExtensionWithMatcher>`,
allowing us to define a match tree to be evaluated in conjunction with evaluation of the wrapped
filter. Prior to data being made available to the filter, it will be provided to the match tree,
which will then attempt to evaluate the matching rules with the provided data, triggering an
action if match evaluation completes in an action.

In the above example, we are specifying that we want to match on the incoming request header
``some-header`` by setting the ``input`` to
:ref:`HttpRequestHeaderMatchInput <envoy_v3_api_msg_type.matcher.v3.HttpRequestHeaderMatchInput>`
and configuring the header key to use. Using the value contained by this header, the provided
``exact_match_map`` specifies which values we care about: we've configured a single value
(``some_value_to_match_on``) to match against. As a result, this config means that if we
receive a request which contains ``some-header: some_value_to_match_on`` as a header, the
:ref:`SkipFilter <envoy_v3_api_msg_extensions.filters.common.matcher.action.v3.SkipFilter>`
action will be resolved (causing the associated HTTP filter to be skipped). If no such header is
present, no action will be resolved and the filter will be applied as usual.

.. literalinclude:: _include/complicated.yaml
    :language: yaml

Above is a slightly more complicated example which combines a top level tree matcher with a
linear matcher. While the tree matchers provide very efficient matching, they are not very
expressive. The list matcher can be used to provide a much richer matching API, and can be combined
with the tree matcher in an arbitrary order. The example describes the following match logic: skip
the filter if ``some-header: skip_filter`` is present and ``second-header`` is set to *either* ``foo`` or
``bar``.

.. _arch_overview_matching_api_iteration_impact:

HTTP Filter Iteration Impact
============================

The above example only demonstrates matching on request headers, which ends up being the simplest
case due to it happening before the associated filter receives any data. Matching on other HTTP
input sources is supported (e.g. response headers), but some discussion is warranted on how this
works at a filter level.

Currently the match evaluation for HTTP filters does not impact control flow at all: if
insufficient data is available to perform the match, callbacks will be sent to the associated
filter as normal. Once sufficient data is available to match an action, this is provided to the
filter. A consequence of this is that if the filter wishes to gate some behavior on a match result,
it has to manage stopping the iteration on its own.

When it comes to actions such as
:ref:`SkipFilter <envoy_v3_api_msg_extensions.filters.common.matcher.action.v3.SkipFilter>`,
this means that if the skip condition is based on anything but the request headers, the filter might
get partially applied, which might result in surprising behavior. An example of this would be to
have a matching tree that attempts to skip the gRPC-Web filter based on response headers: clients
assume that if they send a gRPC-Web request to Envoy, the filter will transform that into a gRPC
request before proxying it upstream, then back into a gRPC-Web response on the encoding path. By
skipping the filter based on response headers, the forward transformation will happen (the upstream
receives a gRPC request), but the response is never converted back to gRPC-Web. As a result, the
client will receive an invalid response back from Envoy. If the skip action was instead resolved on
trailers, the same gRPC-Web filter would consume all the data but never write it back out (as this
happens when it sees the trailers), resulting in a gRPC-Web response with an empty body.

Match Tree Validation
=====================

As the match tree structure is very flexible, some filters might need to impose additional restrictions
on what kind of match trees can be used. This system is somewhat inflexible at the moment, only supporting
limiting the input sources to a specific set. For example, a filter might specify that it only works with
request headers: in this case a match tree that attempts to match on request trailers or response headers
will fail during configuration load, reporting back which data input was invalid.

This is done for example to limit the issues talked about in
:ref:`the above section <arch_overview_matching_api_iteration_impact>` or to help users understand in what
context a match tree can be used for a specific filter. Due to the limitations of the validations framework
at the current time, it is not used for all filters.

For HTTP filters, the restrictions are specified by the filter implementation, so consult the individual
filter documentation to understand whether there are restrictions in place.

For example, in the example below, the match tree could not be used with a filter that restricts the the
match tree to only use
:ref:`HttpRequestHeaderMatchInput <envoy_v3_api_msg_type.matcher.v3.HttpRequestHeaderMatchInput>`.

.. literalinclude:: _include/request_response.yaml
    :language: yaml
