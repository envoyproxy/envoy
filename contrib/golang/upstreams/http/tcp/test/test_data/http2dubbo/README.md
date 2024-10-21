# Documentation for http2dubbo

* To support http2dubbo by go extension on envoy, we need to write two extension by golang: **golang-tcp-upstream** and **golang-http**.


## Documentation for golang-tcp-upstream
1. Support encoding message processing for upstream TCP requests in **EncodeData**(route and cluster have been determined, and targeted message processing can be performed for route and cluster)

2. Support the handling of conn connection status during the encoding stage of upstream TCP requests in **EncodeData**(for example, by setting end_stream=false to avoid envoy semi connected status)

3. Support decoding message processing and aggregation for upstream TCP response in **onUpstreamData**.(for example, by setting end_stream=true to indicate that the message is encapsulated and can be passed to downstream)

4. Aggregate the tcp messages received multiple times by onUpstreamData of tcp response in **OnUpstreamData**.

5. Support obtaining route and cluster information, which can be referenced for targeted processing in the above stages.

## Documentation for golang-http

1. Support pure_protocol_convert from http2dubbo(in **DecodeData**) and dubbo2http(in **EncodeData**)