package io.envoyproxy.envoymobile.helloenvoykotlin

import android.app.Activity
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import androidx.recyclerview.widget.DividerItemDecoration
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import io.envoyproxy.envoymobile.AndroidEngineBuilder
import io.envoyproxy.envoymobile.Element
import io.envoyproxy.envoymobile.Engine
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.shared.Failure
import io.envoyproxy.envoymobile.shared.ResponseRecyclerViewAdapter
import io.envoyproxy.envoymobile.shared.Success
import java.io.IOException
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

private const val REQUEST_HANDLER_THREAD_NAME = "hello_envoy_kt"
private const val REQUEST_AUTHORITY = "api.lyft.com"
private const val REQUEST_PATH = "/ping"
private const val REQUEST_SCHEME = "https"
private val FILTERED_HEADERS =
  setOf(
    "server",
    "filter-demo",
    "buffer-filter-demo",
    "async-filter-demo",
    "x-envoy-upstream-service-time"
  )

/** The main activity of the app. */
class MainActivity : Activity() {
  private val thread = HandlerThread(REQUEST_HANDLER_THREAD_NAME)
  private lateinit var recyclerView: RecyclerView
  private lateinit var viewAdapter: ResponseRecyclerViewAdapter
  private lateinit var engine: Engine

  @Suppress("MaxLineLength")
  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    setContentView(R.layout.activity_main)

    engine =
      AndroidEngineBuilder(application)
        .addLogLevel(LogLevel.DEBUG)
        .enableProxying(true)
        .addPlatformFilter(::DemoFilter)
        .addPlatformFilter(::BufferDemoFilter)
        .addPlatformFilter(::AsyncDemoFilter)
        .addNativeFilter(
          "envoy.filters.http.buffer",
          "{\"@type\":\"type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer\",\"max_request_bytes\":5242880}"
        )
        .addStringAccessor("demo-accessor", { "PlatformString" })
        .setOnEngineRunning { Log.d("MainActivity", "Envoy async internal setup completed") }
        .setEventTracker({
          for (entry in it.entries) {
            Log.d("MainActivity", "Event emitted: ${entry.key}, ${entry.value}")
          }
        })
        .setLogger { Log.d("MainActivity", it) }
        .build()

    recyclerView = findViewById(R.id.recycler_view) as RecyclerView
    recyclerView.layoutManager = LinearLayoutManager(this)

    viewAdapter = ResponseRecyclerViewAdapter()
    recyclerView.adapter = viewAdapter
    val dividerItemDecoration =
      DividerItemDecoration(recyclerView.context, DividerItemDecoration.VERTICAL)
    recyclerView.addItemDecoration(dividerItemDecoration)
    thread.start()
    val handler = Handler(thread.looper)

    // Run a request loop and record stats until the application exits.
    handler.postDelayed(
      object : Runnable {
        override fun run() {
          try {
            makeRequest()
            recordStats()
          } catch (e: IOException) {
            Log.d("MainActivity", "exception making request or recording stats", e)
          }

          // Make a call and report stats again
          handler.postDelayed(this, TimeUnit.SECONDS.toMillis(1))
        }
      },
      TimeUnit.SECONDS.toMillis(1)
    )
  }

  override fun onDestroy() {
    super.onDestroy()
    thread.quit()
  }

  private fun makeRequest() {
    // Note: this request will use an h2 stream for the upstream request due to https scheme.
    // The Java example uses http so http/1.1. This is done on purpose to test both paths in
    // end-to-end tests in CI.
    val requestHeaders =
      RequestHeadersBuilder(RequestMethod.GET, REQUEST_SCHEME, REQUEST_AUTHORITY, REQUEST_PATH)
        .build()
    engine
      .streamClient()
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, _, _ ->
        val status = responseHeaders.httpStatus ?: 0L
        val message = "received headers with status $status"

        val sb = StringBuilder()
        for ((name, value) in responseHeaders.caseSensitiveHeaders()) {
          if (name in FILTERED_HEADERS) {
            sb.append(name).append(": ").append(value.joinToString()).append("\n")
          }
        }
        val headerText = sb.toString()

        Log.d("MainActivity", message)
        responseHeaders.value("filter-demo")?.first()?.let { filterDemoValue ->
          Log.d("MainActivity", "filter-demo: $filterDemoValue")
        }

        if (status == 200) {
          recyclerView.post { viewAdapter.add(Success(message, headerText)) }
        } else {
          recyclerView.post { viewAdapter.add(Failure(message)) }
        }
      }
      .setOnError { error, _ ->
        val attemptCount = error.attemptCount ?: -1
        val message = "failed with error after $attemptCount attempts: ${error.message}"
        Log.d("MainActivity", message)
        recyclerView.post { viewAdapter.add(Failure(message)) }
      }
      .start(Executors.newSingleThreadExecutor())
      .sendHeaders(requestHeaders, true)
  }

  private fun recordStats() {
    val counter = engine.pulseClient().counter(Element("foo"), Element("bar"), Element("counter"))
    counter.increment()
    counter.increment(5)
  }
}
