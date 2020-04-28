package io.envoyproxy.envoymobile.helloenvoykotlin

import android.app.Activity
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.support.v7.widget.DividerItemDecoration
import android.support.v7.widget.LinearLayoutManager
import android.support.v7.widget.RecyclerView
import android.util.Log
import io.envoyproxy.envoymobile.AndroidEnvoyClientBuilder
import io.envoyproxy.envoymobile.Envoy
import io.envoyproxy.envoymobile.RequestBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.ResponseHandler
import io.envoyproxy.envoymobile.UpstreamHttpProtocol

import io.envoyproxy.envoymobile.shared.Failure
import io.envoyproxy.envoymobile.shared.ResponseRecyclerViewAdapter
import io.envoyproxy.envoymobile.shared.Success
import java.io.IOException
import java.util.HashMap
import java.util.concurrent.Executor
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger


private const val REQUEST_HANDLER_THREAD_NAME = "hello_envoy_kt"
private const val ENVOY_SERVER_HEADER = "server"
private const val REQUEST_AUTHORITY = "api.lyft.com"
private const val REQUEST_PATH = "/ping"
private const val REQUEST_SCHEME = "https"

class MainActivity : Activity() {
  private val thread = HandlerThread(REQUEST_HANDLER_THREAD_NAME)
  private lateinit var recyclerView: RecyclerView
  private lateinit var viewAdapter: ResponseRecyclerViewAdapter
  private lateinit var envoy: Envoy

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    setContentView(R.layout.activity_main)

    envoy = AndroidEnvoyClientBuilder(getApplication()).build()

    recyclerView = findViewById(R.id.recycler_view) as RecyclerView
    recyclerView.layoutManager = LinearLayoutManager(this)

    viewAdapter = ResponseRecyclerViewAdapter()
    recyclerView.adapter = viewAdapter
    val dividerItemDecoration = DividerItemDecoration(
      recyclerView.context, DividerItemDecoration.VERTICAL)
    recyclerView.addItemDecoration(dividerItemDecoration)
    thread.start()
    val handler = Handler(thread.looper)

    // Run a request loop until the application exits.
    handler.postDelayed(object : Runnable {
      override fun run() {
        try {
          makeRequest()
        } catch (e: IOException) {
          Log.d("MainActivity", "exception making request.", e)
        }

        // Make a call again
        handler.postDelayed(this, TimeUnit.SECONDS.toMillis(1))
      }
    }, TimeUnit.SECONDS.toMillis(1))
  }

  override fun onDestroy() {
    super.onDestroy()
    thread.quit()
  }

  private fun makeRequest() {
    // Note: this request will use an h2 stream for the upstream request.
    // The Java example uses http/1.1. This is done on purpose to test both paths in end-to-end
    // tests in CI.
    val request = RequestBuilder(RequestMethod.GET, REQUEST_SCHEME, REQUEST_AUTHORITY, REQUEST_PATH)
        .addUpstreamHttpProtocol(UpstreamHttpProtocol.HTTP2)
        .build()
    val responseHeaders = HashMap<String, List<String>>()
    val responseStatus = AtomicInteger()
    val handler = ResponseHandler(Executor { it.run() })
        .onHeaders { headers, status, _ ->
          responseHeaders.putAll(headers)
          responseStatus.set(status)
          Unit
        }
        .onData { buffer, _ ->
          if (responseStatus.get() == 200 && buffer.hasArray()) {
            val serverHeaderField = responseHeaders[ENVOY_SERVER_HEADER]!![0]
            val body = String(buffer.array())
            Log.d("MainActivity", "successful response!")
            recyclerView.post { viewAdapter.add(Success(body, serverHeaderField)) }
          } else {
            recyclerView.post {
              viewAdapter.add(Failure("failed with status " + responseStatus.get()))
            }
          }
          Unit
        }
        .onError { error ->
          val msg = "failed with error after ${error.attemptCount ?: -1} attempts: ${error.message}"
          Log.d("MainActivity", msg)
          recyclerView.post { viewAdapter.add(Failure(msg)) }
          Unit
        }

    envoy.send(request, null, null, handler)
  }
}
