package io.envoyproxy.envoymobile.helloenvoybaselinetest

import android.app.Activity
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import androidx.recyclerview.widget.DividerItemDecoration
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.protobuf.Any
import com.google.protobuf.ByteString
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

private const val TAG = "MainActivity"
private const val REQUEST_HANDLER_THREAD_NAME = "hello_envoy_kt"
private const val REQUEST_AUTHORITY = "api.lyft.com"
private const val REQUEST_PATH = "/ping"
private const val REQUEST_SCHEME = "http"
private val FILTERED_HEADERS =
  setOf(
    "server",
    "filter-demo",
    "buffer-filter-demo",
    "async-filter-demo",
    "x-envoy-upstream-service-time"
  )

class MainActivity : Activity() {
  private val thread = HandlerThread(REQUEST_HANDLER_THREAD_NAME)
  private lateinit var recyclerView: RecyclerView
  private lateinit var viewAdapter: ResponseRecyclerViewAdapter
  private lateinit var engine: Engine

  @Suppress("MaxLineLength")
  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    setContentView(R.layout.activity_main)
    val config =
      Any.newBuilder()
        .setTypeUrl("type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer")
        .setValue(ByteString.empty())
        .build()
        .toByteArray()
        .toString(Charsets.UTF_8)

    engine =
      AndroidEngineBuilder(application)
        .setLogLevel(LogLevel.DEBUG)
        .setLogger { _, msg -> Log.d(TAG, msg) }
        .addPlatformFilter(::DemoFilter)
        .addNativeFilter("envoy.filters.http.buffer", config)
        .addStringAccessor("demo-accessor", { "PlatformString" })
        .setOnEngineRunning { Log.d(TAG, "Envoy async internal setup completed") }
        .setEventTracker({
          for (entry in it.entries) {
            Log.d(TAG, "Event emitted: ${entry.key}, ${entry.value}")
          }
        })
        .build()

    recyclerView = findViewById<RecyclerView>(R.id.recycler_view)!!
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
            Log.d(TAG, "exception making request or recording stats", e)
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
    // Note: this request will use an h2 stream for the upstream request.
    // The Java example uses http/1.1. This is done on purpose to test both paths in end-to-end
    // tests in CI.
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

        Log.d(TAG, message)
        responseHeaders.value("filter-demo")?.first()?.let { filterDemoValue ->
          Log.d(TAG, "filter-demo: $filterDemoValue")
        }

        // The endpoint redirects http://api.lyft.com/ping to https with a 301
        // .github/workflows/android_build.yml is hard-coded to only accept 301s so if changing this
        // code update the expected status code there.
        if (status == 301) {
          recyclerView.post { viewAdapter.add(Success(message, headerText)) }
        } else {
          recyclerView.post { viewAdapter.add(Failure(message)) }
        }
      }
      .setOnError { error, _ ->
        val attemptCount = error.attemptCount ?: -1
        val message = "failed with error after $attemptCount attempts: ${error.message}"
        Log.d(TAG, message)
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
