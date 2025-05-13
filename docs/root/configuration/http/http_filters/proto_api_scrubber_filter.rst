.. _config_http_filters_proto_api_scrubber:

Proto Api Scrubber
==================

NOTE: This filter is currently WIP and not ready for use.

* gRPC :ref:`architecture overview <arch_overview_grpc>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.ProtoApiScrubberConfig``.

Terminology
-----------

1. Restriction

    A restriction is a constraint on a proto element (e.g., field, message,
    etc.) which can be defined using the unified matcher API. Although, unified
    matcher API supports a lot of ways to define these constraints, currently,
    only CEL expressions are supported for this filter.

2. Actions

    An action refers to the specific configuration or behavior that is applied
    whenever a restriction or constraint is satisfied. This filter adds support
    for a RemoveFieldAction which removes a field from request/response payload.
    However, any of the predefined envoy actions can be used as well.

Overview
--------

ProtoApiScrubber filter supports filtering of the request and response Protobuf
payloads based on the configured restrictions and actions.
The filter evaluates the configured restriction for each field to produce the
filtered output using the configured actions. This filter currently supports
only field level restrictions. Restriction support for other proto elements
(e.g., message level restriction, method level restriction, etc.) are planned to
be added in future. The design doc for this filter is available
`here <https://docs.google.com/document/d/1jgRe5mhucFRgmKYf-Ukk20jW8kusIo53U5bcF74GkK8>`_

Assumptions
-----------

This filter assumes the request and response payloads are backed by proto
descriptors which are provided as part of the filter config.

Process Flow
------------

1. Filter Initialization

  a. if parsed AST is provided, store it to be used later.
  b. otherwise, parse the provided CEL expression and store it to be used later.

2. On the request and response path

  a. bind envoy attributes to variables of the AST.
  b. buffer the incoming data to build the complete request body.
  c. filter each field of the request by evaluating the corresponding AST.

Config Requirements
-------------------

Here are config requirements

1. only CEL custom matcher is supported. More matchers would be added overtime on need-basis.
2. scrubbing of ``google.protobuf.Any`` type messages is not supported.

Example
-------
Consider the following API definition:

.. code-block:: proto

    package library;

    service BookService {
      rpc GetBook(GetBookRequest) returns GetBookResponse;
    }

    message GetBookRequest {
      // The id of the book.
      string book_id = 1;
    }

    message GetBookResponse {
      Book book = 1;
      // A field containing debugging information which is expected to be
      // visible only for the development team (i.e., USER_TYPE = DEV) and would
      // be filtered out for other users.
      string debug_info = 2;
    }

    message Book {
      // The title of the book.
      string title = 1;
      // The author of the book.
      string author = 2;
      // The publisher of the book.
      string publisher = 3;
      // Debugging information about a book which is expected to be visible only
      // for the development team (i.e., USER_TYPE = DEV) and would be filtered
      // out for other users.
      string debug_info = 4;
    }

The filter config for this API would look like below (in JSON). Note that the
fields GetBookResponse::debug_info and GetBookResponse::Book::debug_info have
restrictions set as per the comments specified above. It's configured as a CEL
expression which uses the request header attributes to match a header value.
The CEL expression evaluates to ``true`` if the request header ``USER_TYPE``
does not have the value ``DEV`` and the ``RemoveFieldAction`` is performed for
that field. Similarly, if the request header ``USER_TYPE`` has the value
``DEV``, the CEL expression evaluates to ``false`` and the field is not removed,
which is the expected behavior.

.. code-block:: json

    {
     "descriptor_set": {},
     "restrictions": {
       "method_restrictions": {
         "library.BookService.GetBook": {
           "request_field_restrictions": {},
           "response_field_restrictions": {
             "debug_info": {
               "matcher": {
                 "matcher_list": {
                   "matchers": [
                     "predicate": {
                       "single_predicate": {
                         "input": {
                           "@type": "type.googleapis.com/xds.type.matcher.v3.HttpAttributesCelMatchInput"
                         },
                         "custom_match": {
                           "typed_config": {
                             "@type": "type.googleapis.com/xds.type.matcher.v3.CelMatcher",
                             "expr_match": {
                               "cel_expr_string": "request.headers['X-User-Type'] != 'DEV'",
                             }
                           }
                         }
                       }
                     },
                     "on_match": {
                       "action": {
                         "typed_config": {
                           "@type": "type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction"
                         }
                       }
                     }
                   ],
                 }
               }
             },
             "book.debug_info": {
               "matcher": {
                 "matcher_list": {
                   "matchers": [
                     "predicate": {
                       "single_predicate": {
                         "input": {
                           "@type": "type.googleapis.com/xds.type.matcher.v3.HttpAttributesCelMatchInput"
                         },
                         "custom_match": {
                           "typed_config": {
                             "@type": "type.googleapis.com/xds.type.matcher.v3.CelMatcher",
                             "expr_match": {
                               "cel_expr_string": "request.headers['X-User-Type'] != 'DEV'",
                             }
                           }
                         }
                       }
                     },
                     "on_match": {
                       "action": {
                         "typed_config": {
                           "@type": "type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction"
                         }
                       }
                     }
                   ],
                 }
               }
             }
           }
         }
       }
     }
    }

Now, consider the following request headers and body received by the filter for
the BookService.GetBook method:

Request Headers

.. code-block:: json

    {
      "header1": "value1",
      "header2": "value2",
      "X-USER-TYPE": "PROD"
    }

Request Body

.. code-block:: json

    {
      "book_id": "ABC1234"
    }

And consider the following response body received by the filter corresponding
to the above request:

.. code-block:: json

    {
      "book": {
        "title": "Book Title",
        "author": "Book Author",
        "publisher": "Book Publisher",
        "debug_info": "This books metadata is stored in database shard - 0004"
      },
      "debug_info": "Served from server with IP: 172.164.1.2"
    }

The filtered response output by this filter will be the following:

.. code-block:: json

    {
      "book": {
        "title": "Book Title",
        "author": "Book Author",
        "publisher": "Book Publisher"
      }
    }

Note that the fields ``debug_info`` and ``book.debug_info`` are filtered out
from the response since the configured restrictions (i.e., the CEL
expressions) for these fields are not satisfied.
