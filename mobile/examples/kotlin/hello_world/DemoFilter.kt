package io.envoyproxy.envoymobile.helloenvoykotlin

import android.util.Log
import io.envoyproxy.envoymobile.EnvoyError
import io.envoyproxy.envoymobile.FilterDataStatus
import io.envoyproxy.envoymobile.FilterHeadersStatus
import io.envoyproxy.envoymobile.FilterTrailersStatus
import io.envoyproxy.envoymobile.ResponseFilter
import io.envoyproxy.envoymobile.ResponseHeaders
import io.envoyproxy.envoymobile.ResponseTrailers
import io.envoyproxy.envoymobile.StreamIntel
import java.nio.ByteBuffer

class DemoFilter : ResponseFilter {
  override fun onResponseHeaders(
    headers: ResponseHeaders,
    endStream: Boolean,
    streamIntel: StreamIntel
  ): FilterHeadersStatus<ResponseHeaders> {
    Log.d("DemoFilter", "On headers!")
    val builder = headers.toResponseHeadersBuilder()
    builder.add("filter-demo", "1")
    return FilterHeadersStatus.Continue(builder.build())
  }

  override fun onResponseData(
    body: ByteBuffer,
    endStream: Boolean,
    streamIntel: StreamIntel
  ): FilterDataStatus<ResponseHeaders> {
    Log.d("DemoFilter", "On data!")
    return FilterDataStatus.Continue(body)
  }

  override fun onResponseTrailers(
    trailers: ResponseTrailers,
    streamIntel: StreamIntel
  ): FilterTrailersStatus<ResponseHeaders, ResponseTrailers> {
    Log.d("DemoFilter", "On trailers!")
    return FilterTrailersStatus.Continue(trailers)
  }

  override fun onError(error: EnvoyError, streamIntel: StreamIntel) {
    Log.d("DemoFilter", "On error!")
  }

  override fun onCancel(streamIntel: StreamIntel) {
    Log.d("DemoFilter", "On cancel!")
  }
}
