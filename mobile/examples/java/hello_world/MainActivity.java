package io.envoyproxy.envoymobile.helloenvoy;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.support.v7.widget.DividerItemDecoration;
import android.support.v7.widget.LinearLayoutManager;
import android.support.v7.widget.RecyclerView;
import io.envoyproxy.envoymobile.AndroidEnvoyBuilder;
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
import java.util.concurrent.TimeUnit;

public class MainActivity extends Activity {
  private static final String REQUEST_HANDLER_THREAD_NAME = "hello_envoy_java";
  private static final String ENVOY_SERVER_HEADER = "server";
  private static final String REQUEST_AUTHORITY = "s3.amazonaws.com";
  private static final String REQUEST_PATH = "/api.lyft.com/static/demo/hello_world.txt";
  private static final String REQUEST_SCHEME = "http";

  private Envoy envoy;
  private RecyclerView recyclerView;

  private HandlerThread thread = new HandlerThread(REQUEST_HANDLER_THREAD_NAME);
  private ResponseRecyclerViewAdapter viewAdapter;

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_main);

    envoy = new AndroidEnvoyBuilder(getBaseContext()).build();

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
    Request request =
        new RequestBuilder(RequestMethod.GET, REQUEST_SCHEME, REQUEST_AUTHORITY, REQUEST_PATH)
            .build();

    ResponseHandler handler =
        new ResponseHandler(Runnable::run)
            .onHeaders((headers, status, endStream) -> {
              if (status == 200) {
                String serverHeaderField = headers.get(ENVOY_SERVER_HEADER).get(0);
                String body = "";
                recyclerView.post(() -> viewAdapter.add(new Success(body, serverHeaderField)));
              } else {
                recyclerView.post(
                    () -> viewAdapter.add(new Failure("failed with status " + status)));
              }
              return Unit.INSTANCE;
            })
            .onError(() -> {
              recyclerView.post(() -> viewAdapter.add(new Failure("failed with error ")));
              return Unit.INSTANCE;
            });

    envoy.send(request, null, Collections.emptyMap(), handler);
  }
}
