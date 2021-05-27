package io.envoyproxy.envoymobile.helloenvoy;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;
import io.envoyproxy.envoymobile.AndroidEngineBuilder;
import io.envoyproxy.envoymobile.Counter;
import io.envoyproxy.envoymobile.Distribution;
import io.envoyproxy.envoymobile.Engine;
import io.envoyproxy.envoymobile.Element;
import io.envoyproxy.envoymobile.Gauge;
import io.envoyproxy.envoymobile.LogLevel;
import io.envoyproxy.envoymobile.RequestHeaders;
import io.envoyproxy.envoymobile.RequestHeadersBuilder;
import io.envoyproxy.envoymobile.RequestMethod;
import io.envoyproxy.envoymobile.ResponseHeaders;
import io.envoyproxy.envoymobile.Timer;
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
  private static final String REQUEST_HANDLER_THREAD_NAME = "hello_envoy_java";
  private static final String ENVOY_SERVER_HEADER = "server";
  private static final String REQUEST_AUTHORITY = "api.lyft.com";
  private static final String REQUEST_PATH = "/ping";
  private static final String REQUEST_SCHEME = "https";
  private static final Set<String> FILTERED_HEADERS = new HashSet<String>() {
    {
      add("server");
      add("filter-demo");
      add("x-envoy-upstream-service-time");
    }
  };

  private Engine engine;
  private RecyclerView recyclerView;

  private HandlerThread thread = new HandlerThread(REQUEST_HANDLER_THREAD_NAME);
  private ResponseRecyclerViewAdapter viewAdapter;

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_main);

    engine = new AndroidEngineBuilder(getApplication())
                 .addLogLevel(LogLevel.DEBUG)
                 .setOnEngineRunning(() -> {
                   Log.d("MainActivity", "Envoy async internal setup completed");
                   return null;
                 })
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
    RequestHeaders requestHeaders = new RequestHeadersBuilder(RequestMethod.GET, REQUEST_SCHEME,
                                                              REQUEST_AUTHORITY, REQUEST_PATH)
                                        .build();
    engine.streamClient()
        .newStreamPrototype()
        .setOnResponseHeaders((responseHeaders, endStream) -> {
          Integer status = responseHeaders.getHttpStatus();
          String message = "received headers with status " + status;

          StringBuilder sb = new StringBuilder();
          for (Map.Entry<String, List<String>> entry : responseHeaders.getHeaders().entrySet()) {
            String name = entry.getKey();
            if (FILTERED_HEADERS.contains(name)) {
              sb.append(name).append(": ").append(String.join(", ", entry.getValue())).append("\n");
            }
          }
          String headerText = sb.toString();

          Log.d("MainActivity", message);
          if (status == 200) {
            String serverHeaderField = responseHeaders.value(ENVOY_SERVER_HEADER).get(0);
            recyclerView.post(() -> viewAdapter.add(new Success(message, headerText)));
          } else {
            recyclerView.post(() -> viewAdapter.add(new Failure(message)));
          }
          return Unit.INSTANCE;
        })
        .setOnError((error) -> {
          String message = "failed with error after " + error.getAttemptCount() +
                           " attempts: " + error.getMessage();
          Log.d("MainActivity", message);
          recyclerView.post(() -> viewAdapter.add(new Failure(message)));
          return Unit.INSTANCE;
        })
        .start(Executors.newSingleThreadExecutor())
        .sendHeaders(requestHeaders, true);
  }

  private void recordStats() {
    final Counter counter = engine.pulseClient().counter(new Element("foo"), new Element("bar"),
                                                         new Element("counter"));

    final Gauge gauge =
        engine.pulseClient().gauge(new Element("foo"), new Element("bar"), new Element("gauge"));

    final Timer timer =
        engine.pulseClient().timer(new Element("foo"), new Element("bar"), new Element("timer"));
    final Distribution distribution = engine.pulseClient().distribution(
        new Element("foo"), new Element("bar"), new Element("distribution"));

    counter.increment(1);
    counter.increment(5);

    gauge.set(5);
    gauge.add(10);
    gauge.sub(1);

    timer.recordDuration(15);
    distribution.recordValue(15);
  }
}
