package io.envoyproxy.envoymobile.helloenvoy;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.support.v7.widget.DividerItemDecoration;
import android.support.v7.widget.LinearLayoutManager;
import android.support.v7.widget.RecyclerView;
import android.util.Log;
import io.envoyproxy.envoymobile.AndroidEnvoyClientBuilder;
import io.envoyproxy.envoymobile.Envoy;
import io.envoyproxy.envoymobile.Request;
import io.envoyproxy.envoymobile.RequestBuilder;
import io.envoyproxy.envoymobile.RequestMethod;
import io.envoyproxy.envoymobile.ResponseHandler;
import io.envoyproxy.envoymobile.shared.Failure;
import io.envoyproxy.envoymobile.shared.ResponseRecyclerViewAdapter;
import io.envoyproxy.envoymobile.shared.Success;
import kotlin.Unit;

import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

public class MainActivity extends Activity {
  private static final String REQUEST_HANDLER_THREAD_NAME = "hello_envoy_java";
  private static final String ENVOY_SERVER_HEADER = "server";
  private static final String REQUEST_AUTHORITY = "api.lyft.com";
  private static final String REQUEST_PATH = "/ping";
  private static final String REQUEST_SCHEME = "https";

  private Envoy envoy;
  private RecyclerView recyclerView;

  private HandlerThread thread = new HandlerThread(REQUEST_HANDLER_THREAD_NAME);
  private ResponseRecyclerViewAdapter viewAdapter;

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_main);

    envoy = new AndroidEnvoyClientBuilder(getApplication()).build();

    recyclerView = findViewById(R.id.recycler_view);
    recyclerView.setLayoutManager(new LinearLayoutManager(this));

    viewAdapter = new ResponseRecyclerViewAdapter();
    recyclerView.setAdapter(viewAdapter);
    DividerItemDecoration dividerItemDecoration =
        new DividerItemDecoration(recyclerView.getContext(), DividerItemDecoration.VERTICAL);
    recyclerView.addItemDecoration(dividerItemDecoration);
    thread.start();

    // Run a request loop until the application exits.
    final Handler handler = new Handler(thread.getLooper());
    handler.postDelayed(new Runnable() {
      @Override
      public void run() {
        makeRequest();
        // Make a call again
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
    // end-to-end tests
    // in CI.
    Request request =
        new RequestBuilder(RequestMethod.GET, REQUEST_SCHEME, REQUEST_AUTHORITY, REQUEST_PATH)
            .build();
    Map<String, List<String>> responseHeaders = new HashMap<>();
    AtomicInteger responseStatus = new AtomicInteger();
    ResponseHandler handler =
        new ResponseHandler(Runnable::run)
            .onHeaders((headers, status, endStream) -> {
              responseHeaders.putAll(headers);
              responseStatus.set(status);
              Log.d("MainActivity", "successful response!");
              return Unit.INSTANCE;
            })
            .onData((buffer, endStream) -> {
              if (responseStatus.get() == 200 && buffer.hasArray()) {
                String serverHeaderField = responseHeaders.get(ENVOY_SERVER_HEADER).get(0);
                String body = new String(buffer.array());
                recyclerView.post(() -> viewAdapter.add(new Success(body, serverHeaderField)));
              } else {
                recyclerView.post(()
                                      -> viewAdapter.add(new Failure("failed with status " +
                                                                     responseStatus.get())));
              }
              return Unit.INSTANCE;
            })
            .onError((error) -> {
              String msg = "failed with error after " + error.getAttemptCount() +
                           " attempts: " + error.getMessage();
              Log.d("MainActivity", msg);
              recyclerView.post(() -> viewAdapter.add(new Failure(msg)));
              return Unit.INSTANCE;
            });

    envoy.send(request, null, null, handler);
  }
}
