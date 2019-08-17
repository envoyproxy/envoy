package io.envoyproxy.envoymobile.helloenvoy;

import android.app.Activity;
import android.content.Context;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.support.v7.widget.DividerItemDecoration;
import android.support.v7.widget.LinearLayoutManager;
import android.support.v7.widget.RecyclerView;
import android.util.Log;
import io.envoyproxy.envoymobile.Envoy;
import io.envoyproxy.envoymobile.engine.AndroidEngineImpl;
import io.envoyproxy.envoymobile.engine.EnvoyEngine;
import io.envoyproxy.envoymobile.shared.Failure;
import io.envoyproxy.envoymobile.shared.Response;
import io.envoyproxy.envoymobile.shared.ResponseRecyclerViewAdapter;
import io.envoyproxy.envoymobile.shared.Success;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.List;
import java.util.concurrent.TimeUnit;

public class MainActivity extends Activity {
  private static final String ENDPOINT =
      "http://0.0.0.0:9001/api.lyft.com/static/demo/hello_world.txt";

  private static final String ENVOY_SERVER_HEADER = "server";

  private static final String REQUEST_HANDLER_THREAD_NAME = "hello_envoy_java";

  private Envoy envoy;
  private RecyclerView recyclerView;

  private HandlerThread thread = new HandlerThread(REQUEST_HANDLER_THREAD_NAME);

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_main);

    Context context = getBaseContext();

    // Create envoy instance with config.
    String config;
    try {
      config = loadEnvoyConfig(getBaseContext(), R.raw.config);
    } catch (RuntimeException e) {
      Log.d("MainActivity", "exception getting config.", e);
      throw new RuntimeException("Can't get config to run envoy.");
    }

    EnvoyEngine envoyEngine = new AndroidEngineImpl(context);
    envoy = new Envoy(envoyEngine, config);

    recyclerView = (RecyclerView)findViewById(R.id.recycler_view);
    recyclerView.setLayoutManager(new LinearLayoutManager(this));

    final ResponseRecyclerViewAdapter adapter = new ResponseRecyclerViewAdapter();
    recyclerView.setAdapter(adapter);
    DividerItemDecoration dividerItemDecoration =
        new DividerItemDecoration(recyclerView.getContext(), DividerItemDecoration.VERTICAL);
    recyclerView.addItemDecoration(dividerItemDecoration);
    thread.start();

    // Run a request loop until the application exits.
    final Handler handler = new Handler(thread.getLooper());
    handler.postDelayed(new Runnable() {
      @Override
      public void run() {
        final Response response = makeRequest();
        recyclerView.post((Runnable)() -> adapter.add(response));

        // Make a call again
        handler.postDelayed(this, TimeUnit.SECONDS.toMillis(1));
      }
    }, TimeUnit.SECONDS.toMillis(1));
  }

  protected void onDestroy() {
    super.onDestroy();
    thread.quit();
  }

  private Response makeRequest() {
    try {
      URL url = new URL(ENDPOINT);
      // Open connection to the envoy thread listening locally on port 9001.
      HttpURLConnection connection = (HttpURLConnection)url.openConnection();
      connection.setRequestProperty("host", "s3.amazonaws.com");
      int status = connection.getResponseCode();
      if (status == 200) {
        List<String> serverHeaderField = connection.getHeaderFields().get(ENVOY_SERVER_HEADER);
        InputStream inputStream = connection.getInputStream();
        String body = deserialize(inputStream);
        inputStream.close();
        Log.d("Response", "successful response!");
        return new Success(body,
                           serverHeaderField != null ? String.join(", ", serverHeaderField) : "");
      } else {
        return new Failure("failed with status " + status);
      }
    } catch (Exception e) {
      return new Failure(e.getMessage());
    }
  }

  private String deserialize(InputStream inputStream) throws IOException {
    StringBuilder stringBuilder = new StringBuilder();
    BufferedReader bufferedReader = new BufferedReader(new InputStreamReader(inputStream));
    String line = bufferedReader.readLine();
    while (line != null) {
      stringBuilder.append(line);
      line = bufferedReader.readLine();
    }
    bufferedReader.close();
    return stringBuilder.toString();
  }

  private String loadEnvoyConfig(Context context, int configResourceId) throws RuntimeException {
    InputStream inputStream = context.getResources().openRawResource(configResourceId);
    InputStreamReader inputReader = new InputStreamReader(inputStream);
    BufferedReader bufReader = new BufferedReader(inputReader);
    StringBuilder text = new StringBuilder();

    try {
      String line;
      while ((line = bufReader.readLine()) != null) {
        text.append(line);
        text.append('\n');
      }
    } catch (IOException e) {
      return null;
    }
    return text.toString();
  }
}
