### Overview

Envoy provides a way for users to communicate extra information associating with a stream that is
not carried in the standard HTTP(s) and HTTP/2 headers and payloads. The
information is represented by a string key-value pair. For example, users can
pass along RTT information associated with a stream using a key of "rtt info", and a value of
"100ms". In Envoy, we call this type of information metadata.
A stream can be associated with multiple metadata, and the multiple metadata
are represented by a map.

Note: the metadata implementation is still in progress, and the doc is in draft
version.

### Limitation and conditions

For ease of implementation and compatibility purposes, metadata will only be
supported in HTTP/2. Metadata sent in any other protocol should result in protocol
errors or be ignored.

To simplify the implementation, we don't allow metadata frames to carry end of
stream flag. Because metadata frames must be associated with an existing frame, users must
ensure metadata frames to be received before the end of stream is received by the
peer.

Metadata associated with a stream can be sent before headers, after headers,
between data or after data. If metadata frames have to be sent last,
users must put the end of stream in an empty data frame and send the empty data frame after metadata frames.

Envoy only allows up to 1M metadata to be sent per stream. If the accumulated
metadata size exceeds the limit, the stream will be reset.

### Envoy metadata handling

Envoy provides the functionality to proxy, process and add metadata.

## Proxying metadata

If not specified, all the metadata received by Envoy is proxied to the next hop
unmodified. Note that, we do not guarantee the same frame order will be preserved from
hop by hop. That is, metadata from upstream at the beginning of a stream can be
received by the downstream at the end of the stream.

## Consuming metadata

If Envoy needs to take actions when a metadata frame is received, users should
create a new filter.

If Envoy needs to parse a metadata sent on a request from downstream to upstream, a
StreamDecodeFilter should be created. The interface to override is

FilterMetadataStatus StreamDecoderFilter::decodeMetadata(MetadataMapPtr&& metadata);

The metadata passed in is a map of the metadata associated with the request stream. After metadata
have been parsed, the filter can choose to remove metadata from the map, or keep
it untouched.

If Envoy needs to parse a metadata sent on a response from upstream to downstream, a
StreamEncoderFilter should be created. The interface to override is

FilterMetadatasStatus StreamEncoderFilter::encodeMetadata(MetadataMap& metadata);

The metadata passed in is a map of the metadata associated with the response stream. After metadata
have been parsed, the filter can choose to remove metadata from the map, or keep
it untouched.

Note that, if the metadata in a request or a response is removed from the map after consuming, the metadata
will not be passed to the next hop. An empty map means no metadata will be sent to the next hop.
If the metadata is left in the map, it will be passed to the next hop.

## Inserting metadata

Envoy filters can be used to add new metadata to a stream.

If users need to add new metadata for a request from downstream to upstream, a
StreamDecoderFilter should be created. The StreamDecoderFilterCallbacks object that Envoy passes to the
StreamDecoderFilter has an interface MetadataMapVector&
StreamDecoderFilterCallbacks::addDecodedMetadata(). By calling the interface,
users get a reference to a vector of metadata map associated with the request stream. Users can
insert new metadata map to the metadata map vector, and Envoy will proxy the new metadata
map to the upstream. StreamDecoderFilterCallbacks::addDecodedMetadata() can be called in
StreamDecoderFilter::decodeHeaders(), StreamDecoderFilter::decodeData() and
StreamDecoderFilter::decodeTrailers(). Do not call
StreamDecoderFilterCallbacks::addDecodedMetadata() in
StreamDecoderFilter::decodeMetadata(MetadataMap metadata\_map). New metadata can
be added directly to metadata\_map.

If users need to add new metadata for a response to downstream, a
StreamEncoderFilter should be created. Users pass the metadata to be added to
StreamEncoderFilterCallbacks::addEncodedMetadata(MetadataMapPtr&&
metadata\_map\_ptr). This function can be called in
StreamEncoderFilter::encode100ContinueHeaders(HeaderMap& headers), StreamEncoderFilter::encodeHeaders(HeaderMap& headers, bool end\_stream),
StreamEncoderFilter::encodeData(Buffer::Instance& data, bool end\_stream), StreamEncoderFilter::encodeTrailers(HeaderMap& trailers).
Consequently, the new metadata will be passed through all the encoding filters that follow the filter
where the new metadata are added.

If users receive metadata from upstream, new metadata can be added directly to
the input argument metadata\_map in StreamFilter::encodeMetadata(MetadataMap& metadata\_map).

### Metadata implementation

## Metadata as extension HTTP/2 frames

Envoy supports metadata by utilizing nghttp2 extension frames. Envoy defines a
new extension frame type METADATA frame in nghttp2:

type = 0x4D

The METADATA frame uses a standard frame header, as described in the
[HTTP/2 spec](https://httpwg.github.io/specs/rfc7540.html#FrameHeader.)
The payload of the METADATA frame is a block of key-value pairs encoded using the [HPACK Literal
Header Field Never Indexed representation](
https://httpwg.org/specs/rfc7541.html#literal.header.never.indexed). Each
key-value pair represents one piece of metadata.

The METADATA frame defines the following flags:

END\_METADATA (0x4).

If the flag is set, it indicates that this frame ends a metadata
payload.

The METADATA frame payload is not subject to HTTP/2 flow control, but the size
of the payload is bounded by the maximum frame size negotiated in SETTINGS.
There are no restrictions on the set of octets that may be used in keys or values.

We do not allow METADATA frame to terminate a stream. DATA, HEADERS or RST\_STREAM must
be used for that purpose.

## Response metadata handling

We call metadata that need to be forwarded to downstream the response metadata.
Response metadata can be received from upstream or generated locally.

Response metadata is generally a hop by hop message, so Envoy doesn't
need to hold response metadata locally to wait for some events or data. As a result,
filters handling response metadata don't need to stop the filter iteration and wait. Instead response
metadata can be forwarded through targeted filters and sequentially to the
next hop as soon as they are
available, no matter if the metadata are locally generated or received from
upstream. The same statement is also true for metadata from downstream to upstream (request metadata). However,
request metadata may need to wait for the upstream connection to be ready before going to the next hop.
In this section, we focus on response metadata handling.

We first explain how response metadata get consumed or proxied.
In function EnvoyConnectionManagerImpl::ActiveStream::encodeMetadata(ActiveStreamEncoderFilter\* filter,
MetadataMapPtr&& metadata\_map\_ptr), Envoy passes response metadata received from upstream to filters by
calling the following filter interface:

FilterMetadatasStatus StreamEncoderFilter::encodeMetadata(MetadataMapVector& metadata\_map).

Filters, by implementing the interface, can consume response metadata. After going through
the filter chain, function EnvoyConnectionManagerImpl::ActiveStream::encodeMetadata(ActiveStreamEncoderFilter\* filter,
MetadataMapPtr&& metadata\_map\_ptr) immediately forwards the updated or remaining response metadata to the next hop by
calling the metadata encoding function in codec:

ConnectionManagerImpl::ActiveStream::response\_encoder\_-\>encodeMetadata(MetadataMapVector& metadata\_map\_vector).

If no filter consumes the response metadata, the response metadata is proxied to
the downstream untouched.

Envoy can also add new response metadata through filters's encoding interfaces (See section
[Inserting metadata](#inserting-metadata) for detailed interfaces). Filters can add new
metadata by calling StreamDecoderFilterCallbacks::encodeMetadata(MetadataMapPtr&& metadata\_map\_ptr),
which triggers
ConnectionManagerImpl::ActiveStream::encodeMetadata(ActiveStreamEncoderFilter\* filter, MetadataMapPtr&& metadata\_map)
to go through all the encoding filters.
Or new metadata can be added to metadata\_map in
StreamFilter::encodeMetadata(MetadataMap& metadata\_map) directly.

## Request metadata handling

We first explain how request metadata get consumed or proxied.
In function EnvoyConnectionManagerImpl::ActiveStream::decodeMetadata(ActiveStreamDecoderFilter\* filter,
MetadataMap& metadata\_map), Envoy passes request metadata received from downstream to filters by
calling the following filter interface:

FilterMetadatasStatus StreamDecoderFilter::decodeMetadata(MetadataMap& metadata\_map).

Filters, by implementing the interface, can consume or modify request metadata. If no filter
touches the metadata, it is proxied to upstream unchanged.

The last filter in the filter chain is router filter. The router filter calls
Filter::request\_encoder\_-\>encodeMetadata(const MetadataMapVector& metadata\_map\_vector) to pass
the metadata to codec, and codec encodes and forwards the metadata to the upstream. If the connection
to the upstream has not been established when metadata is received, the metadata is temporarily stored in
Filter::downstream\_metadata\_map\_vector\_. When the connection is ready
(Filter::UpstreamRequest::onPoolReady()), the metadata is then passed to codec, and forwarded to
the upstream.

Envoy can also add new request metadata through filters's decoding interfaces (See section
[Inserting metadata](#inserting-metadata) for detailed interfaces). Filters can add new
metadata to ActiveStream::request\_metadata\_map\_vector\_ by calling
StreamDecoderFilterCallbacks::addDecodedMetadata(). After calling each filter's decoding function,
Envoy checks if new metadata is added to ActiveStream::request\_metadata\_map\_vector\_. If so,
then Envoy calls ConnectionManagerImpl::ActiveStream::decodeMetadata(ActiveStreamEncoderFilter\* filter,
MetadataMapPtr&& metadata\_map) to go through all the filters.

Note that, because metadata frames do not carry end\_stream, if new metadata is added to a headers
only request, Envoy moves end\_stream from headers to an empty data frame which is sent after the new
metadata. In addition, Envoy drains metadata in router filter before any other types of
frames except headers to make sure end\_stream is handled correctly.

