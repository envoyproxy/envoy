package io.envoyproxy.envoymobile.helloenvoy;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;
import io.envoyproxy.envoymobile.AndroidStreamClientBuilder;
import io.envoyproxy.envoymobile.RequestHeaders;
import io.envoyproxy.envoymobile.RequestHeadersBuilder;
import io.envoyproxy.envoymobile.RequestMethod;
import io.envoyproxy.envoymobile.ResponseHeaders;
import androidx.recyclerview.widget.DividerItemDecoration;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import io.envoyproxy.envoymobile.StreamClient;
import io.envoyproxy.envoymobile.shared.Failure;
import io.envoyproxy.envoymobile.shared.ResponseRecyclerViewAdapter;
import io.envoyproxy.envoymobile.shared.Success;
import kotlin.Unit;

import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

public class MainActivity extends Activity {
  private static final String REQUEST_HANDLER_THREAD_NAME = "hello_envoy_java";
  private static final String ENVOY_SERVER_HEADER = "server";
  private static final String REQUEST_AUTHORITY = "api.lyft.com";
  private static final String REQUEST_PATH = "/ping";
  private static final String REQUEST_SCHEME = "https";

  private StreamClient streamClient;
  private RecyclerView recyclerView;

  private HandlerThread thread = new HandlerThread(REQUEST_HANDLER_THREAD_NAME);
  private ResponseRecyclerViewAdapter viewAdapter;

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_main);

    streamClient = new AndroidStreamClientBuilder(getApplication()).build();

    recyclerView = (RecyclerView)findViewById(R.id.recycler_view);
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
    // end-to-end tests in CI.
    RequestHeaders requestHeaders = new RequestHeadersBuilder(RequestMethod.GET, REQUEST_SCHEME,
                                                              REQUEST_AUTHORITY, REQUEST_PATH)
                                        .build();
    streamClient.newStreamPrototype()
        .setOnResponseHeaders((responseHeaders, endStream) -> {
          Integer status = responseHeaders.getHttpStatus();
          String message = "received headers with status " + status;
          Log.d("MainActivity", message);
          if (status == 200) {
            String serverHeaderField = responseHeaders.value(ENVOY_SERVER_HEADER).get(0);
            recyclerView.post(() -> viewAdapter.add(new Success(message, serverHeaderField)));
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
}
