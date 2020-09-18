.. _why_is_envoy_sending_http2_resets:

Why is Envoy sending HTTP/2 resets?
===================================

The HTTP/2 reset path is mostly governed by the codec Envoy uses to frame HTTP/2, nghttp2. nghttp2 has
extremely good adherence to the HTTP/2 spec, but as many clients are not exactly as compliant, this
mismatch can cause unexpected resets. Unfortunately, unlike the debugging the 
:ref:`internal response path <why_is_envoy_sending_internal_responses>`, Envoy has limited visibility into
the specific reason nghttp2 reset a given stream.

If you have a reproducible failure case, you can run it against a debug Envoy with "-l trace" to get
detailed nghttp2 error logs, which often indicate which header failed compliance checks. Alternately,
if you can afford to run with "-l trace" on a machine encountering the errors, you can look for logs
from the file "source/common/http/http2/codec_impl.cc" of the form
`invalid http2: [nghttp2 error detail]`
for example:
`invalid http2: Invalid HTTP header field was received: frame type: 1, stream: 1, name: [content-length], value: [3]`

You can also check :ref:`HTTP/2 stats`<config_http_conn_man_stats_per_codec>`: in many cases where
Envoy resets streams, for example if there are more headers than allowed by configuration or flood
detection kicks in, http2 counters will be incremented as the streams are reset.


