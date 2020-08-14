package io.envoyproxy.envoymobile.helloenvoykotlin

import android.util.Log
import io.envoyproxy.envoymobile.EnvoyError
import io.envoyproxy.envoymobile.FilterDataStatus
import io.envoyproxy.envoymobile.FilterHeadersStatus
import io.envoyproxy.envoymobile.FilterTrailersStatus
import io.envoyproxy.envoymobile.ResponseFilter
import io.envoyproxy.envoymobile.ResponseFilterCallbacks
import io.envoyproxy.envoymobile.ResponseHeaders
import io.envoyproxy.envoymobile.ResponseTrailers
import java.nio.ByteBuffer

class DemoFilter : ResponseFilter {

  override fun setResponseFilterCallbacks(callbacks: ResponseFilterCallbacks) {
    // No-op
  }

  override fun onResponseHeaders(headers: ResponseHeaders, endStream: Boolean):
    FilterHeadersStatus<ResponseHeaders> {
      Log.d("DemoFilter", "On headers!")
      return FilterHeadersStatus.Continue(headers)
    }

  override fun onResponseData(body: ByteBuffer, endStream: Boolean): FilterDataStatus {
    Log.d("DemoFilter", "On data!")
    return FilterDataStatus.Continue(body)
  }

  override fun onResponseTrailers(trailers: ResponseTrailers):
    FilterTrailersStatus<ResponseTrailers> {
      Log.d("DemoFilter", "On trailers!")
      return FilterTrailersStatus.Continue(trailers)
    }

  override fun onError(error: EnvoyError) {
    Log.d("DemoFilter", "On error!")
  }

  override fun onCancel() {
    Log.d("DemoFilter", "On cancel!")
  }
}
