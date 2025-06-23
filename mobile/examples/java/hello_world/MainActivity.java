package io.envoyproxy.envoymobile.helloenvoy;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;
import io.envoyproxy.envoymobile.AndroidEngineBuilder;
import io.envoyproxy.envoymobile.Counter;
import io.envoyproxy.envoymobile.Engine;
import io.envoyproxy.envoymobile.Element;
import io.envoyproxy.envoymobile.LogLevel;
import io.envoyproxy.envoymobile.RequestHeaders;
import io.envoyproxy.envoymobile.RequestHeadersBuilder;
import io.envoyproxy.envoymobile.RequestMethod;
import io.envoyproxy.envoymobile.ResponseHeaders;
import androidx.recyclerview.widget.DividerItemDecoration;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import io.envoyproxy.envoymobile.shared.Failure;
import io.envoyproxy.envoymobile.shared.ResponseRecyclerViewAdapter;
import io.envoyproxy.envoymobile.shared.Success;
import kotlin.Unit;

import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

public class MainActivity extends Activity {
  private static final String TAG = "MainActivity";
  private static final String REQUEST_HANDLER_THREAD_NAME = "hello_envoy_java";
  private static final String ENVOY_SERVER_HEADER = "server";
  private static final String REQUEST_AUTHORITY = "api.lyft.com";
  private static final String REQUEST_PATH = "/ping";
  private static final String REQUEST_SCHEME_HTTP = "http";
  private static final String REQUEST_SCHEME_HTTPS = "https";
  private static final Set<String> FILTERED_HEADERS =
      Set.of("server", "filter-demo", "x-envoy-upstream-service-time");

  private Engine engine;
  private RecyclerView recyclerView;

  private HandlerThread thread = new HandlerThread(REQUEST_HANDLER_THREAD_NAME);
  private ResponseRecyclerViewAdapter viewAdapter;
  private Boolean clear_text = true;

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_main);

    engine = new AndroidEngineBuilder(getApplication())
                 .setLogLevel(LogLevel.DEBUG)
                 .setLogger((level, message) -> {
                   Log.d(TAG, message);
                   return null;
                 })
                 .setOnEngineRunning(() -> {
                   Log.d(TAG, "Envoy async internal setup completed");
                   return null;
                 })
                 .enablePlatformCertificatesValidation(true)
                 .build();

    recyclerView = (RecyclerView)findViewById(R.id.recycler_view);
    recyclerView.setLayoutManager(new LinearLayoutManager(this));

    viewAdapter = new ResponseRecyclerViewAdapter();
    recyclerView.setAdapter(viewAdapter);
    DividerItemDecoration dividerItemDecoration =
        new DividerItemDecoration(recyclerView.getContext(), DividerItemDecoration.VERTICAL);
    recyclerView.addItemDecoration(dividerItemDecoration);
    thread.start();

    // Run a request loop and record stats until the application exits.
    final Handler handler = new Handler(thread.getLooper());
    handler.postDelayed(new Runnable() {
      @Override
      public void run() {
        makeRequest();
        recordStats();
        // Make a call and record stats again
        handler.postDelayed(this, TimeUnit.SECONDS.toMillis(1));
      }
    }, TimeUnit.SECONDS.toMillis(1));
  }

  protected void onDestroy() {
    super.onDestroy();
    thread.quit();
  }

  private void makeRequest() {
    // Note: this request will use an http/1.1 stream for the upstream request.
    // The Kotlin example uses h2. This is done on purpose to test both paths in
    // end-to-end tests in CI.
    String scheme = clear_text ? REQUEST_SCHEME_HTTP : REQUEST_SCHEME_HTTPS;
    RequestHeaders requestHeaders =
        new RequestHeadersBuilder(RequestMethod.GET, scheme, REQUEST_AUTHORITY, REQUEST_PATH)
            .build();
    engine.streamClient()
        .newStreamPrototype()
        .setOnResponseHeaders((responseHeaders, endStream, ignored) -> {
          Integer status = responseHeaders.getHttpStatus();
          String message = "received headers with status " + status + " for " + scheme + " request";

          StringBuilder sb = new StringBuilder();
          for (Map.Entry<String, List<String>> entry :
               responseHeaders.caseSensitiveHeaders().entrySet()) {
            String name = entry.getKey();
            if (FILTERED_HEADERS.contains(name)) {
              sb.append(name).append(": ").append(String.join(", ", entry.getValue())).append("\n");
            }
          }
          String headerText = sb.toString();

          Log.d(TAG, message);
          if ((scheme == REQUEST_SCHEME_HTTP && status == 301) ||
              (scheme == REQUEST_SCHEME_HTTPS && status == 200)) {
            // The server returns 301 to http request and 200 to https request.
            String serverHeaderField = responseHeaders.value(ENVOY_SERVER_HEADER).get(0);
            recyclerView.post(() -> viewAdapter.add(new Success(message, headerText)));
          } else {
            recyclerView.post(() -> viewAdapter.add(new Failure(message)));
          }
          return Unit.INSTANCE;
        })
        .setOnError((error, ignored) -> {
          String message = "failed with error after " + error.getAttemptCount() +
                           " attempts: " + error.getMessage() + " for " + scheme + " request";
          Log.d(TAG, message);
          recyclerView.post(() -> viewAdapter.add(new Failure(message)));
          return Unit.INSTANCE;
        })
        .start(Executors.newSingleThreadExecutor())
        .sendHeaders(requestHeaders, /* endStream= */ true, /* idempotent= */ false);

    clear_text = !clear_text;
  }

  private void recordStats() {
    final Counter counter = engine.pulseClient().counter(new Element("foo"), new Element("bar"),
                                                         new Element("counter"));
    counter.increment(1);
    counter.increment(5);
  }
}
