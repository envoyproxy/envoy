## `RouteEntry::requestHeaderTransforms` and `ResponseEntry::responseHeaderTransforms` APIs

### Overview

`RouteEntry::requestHeaderTransforms`
and `ResponseEntry::responseHeaderTransforms` allows filters to obtain request
and response header transformations that would be applied by
`RouteEntry::finalizeRequestHeaders`
and `ResponseEntry::finalizeResponseHeaders` respectively.

If you do not have full knowledge that there will be no further route
modifications later in the filter chain, you should not use these APIs.

### Example usage

To obtain the response header transformations at request time:

``` cpp
Http::FilterHeadersStatus MyFilter::decodeHeaders(
    Http::RequestHeaderMap& headers, bool) {
  auto transforms =
      decoder_callbacks_->route()->routeEntry()->responseHeaderTransforms(
          decoder_callbacks_->streamInfo());

  // Send the response headers back to the client; they will process them later.
  decoder_callbacks_->sendLocalReply(
      Envoy::Http::Code::OK, MySerializedTransforms(transforms), nullptr, Envoy::Grpc::Status::Ok,
      "local_reply");
}
```

If you want to retrieve the original values rather than the formatted ones, pass
`/*do_formatting=*/false` as the second argument to `responseHeaderTransforms`.
