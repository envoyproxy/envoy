package io.envoyproxy.envoymobile.helloenvoykotlin

import io.envoyproxy.envoymobile.AsyncResponseFilter
import io.envoyproxy.envoymobile.EnvoyError
import io.envoyproxy.envoymobile.FilterDataStatus
import io.envoyproxy.envoymobile.FilterHeadersStatus
import io.envoyproxy.envoymobile.FilterResumeStatus
import io.envoyproxy.envoymobile.FilterTrailersStatus
import io.envoyproxy.envoymobile.ResponseFilterCallbacks
import io.envoyproxy.envoymobile.ResponseHeaders
import io.envoyproxy.envoymobile.ResponseTrailers
import io.envoyproxy.envoymobile.StreamIntel
import java.nio.ByteBuffer
import java.util.Timer
import kotlin.concurrent.schedule

/**
 * Example of a more complex HTTP filter that pauses processing on the response filter chain,
 * buffers until the response is complete, then asynchronously triggers filter chain resumption
 * while setting a new header. Also demonstrates safety of re-entrancy in async callbacks.
 */
class AsyncDemoFilter : AsyncResponseFilter {
  private lateinit var callbacks: ResponseFilterCallbacks

  override fun onResponseHeaders(
    headers: ResponseHeaders,
    endStream: Boolean,
    streamIntel: StreamIntel
  ): FilterHeadersStatus<ResponseHeaders> {
    // If this is the end of the stream, asynchronously resume response processing via callback.
    if (endStream) {
      Timer("AsyncResume", false).schedule(100) {
        callbacks.resumeResponse()
      }
    }
    return FilterHeadersStatus.StopIteration()
  }

  override fun onResponseData(
    body: ByteBuffer,
    endStream: Boolean,
    streamIntel: StreamIntel
  ): FilterDataStatus<ResponseHeaders> {
    // If this is the end of the stream, asynchronously resume response processing via callback.
    if (endStream) {
      Timer("AsyncResume", false).schedule(100) {
        callbacks.resumeResponse()
      }
    }
    return FilterDataStatus.StopIterationAndBuffer()
  }

  override fun onResponseTrailers(
    trailers: ResponseTrailers,
    streamIntel: StreamIntel
  ): FilterTrailersStatus<ResponseHeaders, ResponseTrailers> {
    // Trailers imply end of stream, so asynchronously resume response processing via callbacka
    Timer("AsyncResume", false).schedule(100) {
      callbacks.resumeResponse()
    }
    return FilterTrailersStatus.StopIteration()
  }

  override fun setResponseFilterCallbacks(callbacks: ResponseFilterCallbacks) {
    this.callbacks = callbacks
  }

  override fun onResumeResponse(
    headers: ResponseHeaders?,
    data: ByteBuffer?,
    trailers: ResponseTrailers?,
    endStream: Boolean,
    streamIntel: StreamIntel
  ): FilterResumeStatus<ResponseHeaders, ResponseTrailers> {
    val builder = headers!!.toResponseHeadersBuilder()
      .add("async-filter-demo", "1")
    return FilterResumeStatus.ResumeIteration(builder.build(), data, trailers)
  }

  @Suppress("EmptyFunctionBlock")
  override fun onError(error: EnvoyError, streamIntel: StreamIntel) {
  }

  @Suppress("EmptyFunctionBlock")
  override fun onCancel(streamIntel: StreamIntel) {
  }
}
