package io.envoyproxy.envoymobile.helloenvoykotlin

import android.app.Activity
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.support.v7.widget.DividerItemDecoration
import android.support.v7.widget.LinearLayoutManager
import android.support.v7.widget.RecyclerView
import android.util.Log
import io.envoyproxy.envoymobile.AndroidEnvoyBuilder
import io.envoyproxy.envoymobile.CancelableStream
import io.envoyproxy.envoymobile.Envoy
import io.envoyproxy.envoymobile.RequestBuilder
import io.envoyproxy.envoymobile.ResponseHandler
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.shared.Failure
import io.envoyproxy.envoymobile.shared.Response
import io.envoyproxy.envoymobile.shared.ResponseRecyclerViewAdapter
import io.envoyproxy.envoymobile.shared.Success
import java.io.IOException
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.TimeUnit
import java.util.concurrent.Executor;


private const val REQUEST_HANDLER_THREAD_NAME = "hello_envoy_kt"
private const val ENVOY_SERVER_HEADER = "server"
private const val REQUEST_AUTHORITY = "s3.amazonaws.com"
private const val REQUEST_PATH = "/api.lyft.com/static/demo/hello_world.txt"
private const val REQUEST_SCHEME = "http"

class MainActivity : Activity() {
  private val thread = HandlerThread(REQUEST_HANDLER_THREAD_NAME)
  private lateinit var recyclerView: RecyclerView
  private lateinit var viewAdapter: ResponseRecyclerViewAdapter
  private lateinit var envoy: Envoy

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    setContentView(R.layout.activity_main)

    envoy = AndroidEnvoyBuilder(baseContext).build()

    recyclerView = findViewById(R.id.recycler_view) as RecyclerView
    recyclerView.layoutManager = LinearLayoutManager(this)

    viewAdapter = ResponseRecyclerViewAdapter()
    recyclerView.adapter = viewAdapter
    val dividerItemDecoration = DividerItemDecoration(recyclerView.context, DividerItemDecoration.VERTICAL)
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

  private fun makeRequest(): Unit {
    val request = RequestBuilder(RequestMethod.GET, REQUEST_SCHEME,
                                 REQUEST_AUTHORITY, REQUEST_PATH).build()
    val handler = ResponseHandler(object : Executor {
      override fun execute(r : Runnable) {
        r.run()
      }
    })
    .onHeaders({ headers, status, _ ->
      if (status == 200) {
        val serverHeaderField = headers[ENVOY_SERVER_HEADER]?.first() ?: ""
        val body = "" // fake data
        recyclerView.post { viewAdapter.add(Success(body, serverHeaderField)) }
      } else {
        recyclerView.post { viewAdapter.add(Failure("failed with status: $status")) }
      }
    })

    envoy.send(request, null, emptyMap(), handler)
  }

  private fun deserialize(inputStream: InputStream): String {
    return inputStream.bufferedReader().use { reader -> reader.readText() }
  }
}
