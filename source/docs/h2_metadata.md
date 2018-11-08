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

### Limitation

For ease of implementation and compatibility purposes, metadata will only be
supported in HTTP/2. Metadata sent in any other protocol should result in protocol
errors or be ignored.

### Envoy metadata handling

Envoy provides the functionality to proxy, process and add metadata.

## Proxying metadata

(To be implemented)

If not specified, all the metadata received by Envoy is proxied to the next hop
unmodified. Note that, we do not guarantee the same frame order will be preserved from
hop by hop. That is, metadata from upstream at the beginning of a stream can be
received by the downstream at the end of the stream.

## Consuming metadata

(To be implemented)

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

(To be implemented)

Envoy filters can be used to add new metadata to a stream.

If users need to add new metadata for a request from downstream to upstream, a
StreamDecoderFilter should be created. The StreamDecoderFilterCallbacks object that Envoy passes to the
StreamDecoderFilter has an interface MetadataMap&
StreamDecoderFilterCallbacks::addDecodedMetadata(). By calling the interface,
users get a reference to the metadata map associated with the request stream. Users can
insert new metadata to the metadata map, and Envoy will proxy the new metadata
map to the upstream.

If users need to add new metadata for a response proxied to downstream, a
StreamEncoderFilter should be created. The StreamEncoderFilterCallbacks object that Envoy passes to the
StreamEncoderFilter has an interface MetadataMap&
StreamEncoderFilterCallbacks::addEncodedMetadata(). By calling the interface,
users get a reference to the metadata map associated with the response stream. Users can
insert new metadata to the metadata map, and Envoy will proxy the new metadata
map to the downstream.

### Metadata implementation

## Metadata as extension HTTP/2 frames.

Envoy supports metadata by utilizing nghttp2 extension frames. Envoy defines a
new extension frame type METADATA frame in nghttp2:

type = 0x4D

The METADATA frame uses a standard frame header, as described in the
[HTTP/2 spec](https://httpwg.github.io/specs/rfc7540.html#FrameHeader.)
The payload of the METADATA frame is a block of key-value pairs encoded using the [HPACK Literal
Header Field Never Indexed representation](
http://httpwg.org/specs/rfc7541.html#literal.header.never.indexed). Each
key-value pair represents one piece of metadata.

The METADATA frame defines the following flags:

END\_METADATA (0x4).

If the flag is set, it indicates that this frame ends a metadata
payload.

The METADATA frame payload is not subject to HTTP/2 flow control, but the size
of the payload is bounded by the maximum frame size negotiated in SETTINGS.
There are no restrictions on the set of octets that may be used in keys or values.
TODO(soya3129): decide if we allow METADATA frame to terminate a stream.
