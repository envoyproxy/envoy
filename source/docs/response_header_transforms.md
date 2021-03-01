## `ResponseEntry::responseHeaderTransforms` API

### Overview

`ResponseEntry::responseHeaderTransforms` allows filters to obtain response
header transformations that would be applied by
`ResponseEntry::finalizeResponseHeaders`.

If you do not have full knowledge that there will be no further route
modifications later in the filter chain, you should not use this API.

### Usage

To obtain the response header transformations at request time:

``` cpp
Http::FilterHeadersStatus MyFilter::decodeHeaders(Http::RequestHeaderMap&
headers, bool) {
auto transforms =
decoder_callbacks_->route()->routeEntry()->responseHeaderTransforms(decoder_callbacks_->streamInfo());

// Send the response headers back to the client; they will process them later.
decoder_callbacks_->sendLocalReply(Envoy::Http::Code::OK,
MySerializedTransforms(transforms), nullptr, Envoy::Grpc::Status::Ok,
"local_reply");
}
```
