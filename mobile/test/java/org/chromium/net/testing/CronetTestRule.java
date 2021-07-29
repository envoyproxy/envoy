package org.chromium.net.testing;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;

import android.content.Context;
import android.os.StrictMode;
import android.util.Log;
import androidx.test.platform.app.InstrumentationRegistry;
import io.envoyproxy.envoymobile.LogLevel;
import io.envoyproxy.envoymobile.engine.AndroidJniLibrary;
import java.io.File;
import java.lang.annotation.Annotation;
import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import java.lang.reflect.Field;
import java.net.URL;
import java.net.URLStreamHandlerFactory;
import org.chromium.net.ApiVersion;
import org.chromium.net.CronetEngine;
import org.chromium.net.ExperimentalCronetEngine;
import org.chromium.net.UrlResponseInfo;
import org.chromium.net.impl.CronetEngineBuilderImpl;
import org.chromium.net.impl.JavaCronetEngine;
import org.chromium.net.impl.JavaCronetProvider;
import org.chromium.net.impl.UserAgent;
import org.junit.Assert;
import org.junit.rules.TestRule;
import org.junit.runner.Description;
import org.junit.runners.model.Statement;

/**
 * Custom TestRule for Cronet instrumentation tests.
 */
public final class CronetTestRule implements TestRule {
  private static final String PRIVATE_DATA_DIRECTORY_SUFFIX = "cronet_test";

  private CronetTestFramework mCronetTestFramework;

  // {@code true} when test is being run against system HttpURLConnection implementation.
  private boolean mTestingSystemHttpURLConnection;
  private boolean mTestingJavaImpl;
  private StrictMode.VmPolicy mOldVmPolicy;
  private CronetEngine mUrlConnectionCronetEngine;
  private static Context mContext;

  /**
   * Name of the file that contains the test server certificate in PEM format.
   */
  public static final String SERVER_CERT_PEM = "quic-chain.pem";

  /**
   * Name of the file that contains the test server private key in PKCS8 PEM format.
   */
  public static final String SERVER_KEY_PKCS8_PEM = "quic-leaf-cert.key.pkcs8.pem";

  private static final String TAG = CronetTestRule.class.getSimpleName();

  /**
   * Creates and holds pointer to CronetEngine.
   */
  public static class CronetTestFramework {
    public ExperimentalCronetEngine mCronetEngine;

    public CronetTestFramework(Context context) { this(createEngine(context)); }

    private static ExperimentalCronetEngine createEngine(Context context) {
      ExperimentalCronetEngine.Builder builder = new ExperimentalCronetEngine.Builder(context);
      ((CronetEngineBuilderImpl)builder.getBuilderDelegate()).setLogLevel(LogLevel.DEBUG);
      return builder.enableQuic(true).build();
    }

    private CronetTestFramework(ExperimentalCronetEngine cronetEngine) {
      mCronetEngine = cronetEngine;
      // Start collecting metrics.
      mCronetEngine.getGlobalMetricsDeltas();
    }
  }

  int getMaximumAvailableApiLevel() {
    // Prior to M59 the ApiVersion.getMaximumAvailableApiLevel API didn't exist
    if (ApiVersion.getCronetVersion().compareTo("59") < 0) {
      return 3;
    }
    return ApiVersion.getMaximumAvailableApiLevel();
  }

  public static Context getContext() {
    if (mContext == null) {
      mContext = InstrumentationRegistry.getInstrumentation().getTargetContext();
    }
    return mContext;
  }

  @Override
  public Statement apply(final Statement base, final Description desc) {
    return new Statement() {
      @Override
      public void evaluate() throws Throwable {
        try {
          setUp();
          runBase(base, desc);
        } finally {
          tearDown();
        }
      }
    };
  }

  /**
   * Returns {@code true} when test is being run against system HttpURLConnection implementation.
   */
  public boolean testingSystemHttpURLConnection() { return mTestingSystemHttpURLConnection; }

  /**
   * Returns {@code true} when test is being run against the java implementation of CronetEngine.
   */
  public boolean testingJavaImpl() { return mTestingJavaImpl; }

  private void runBase(Statement base, Description desc) throws Throwable {
    setTestingSystemHttpURLConnection(false);
    setTestingJavaImpl(false);
    String packageName = desc.getTestClass().getPackage().getName();

    // Find the API version required by the test.
    int requiredApiVersion = getMaximumAvailableApiLevel();
    for (Annotation a : desc.getTestClass().getAnnotations()) {
      if (a instanceof RequiresMinApi) {
        requiredApiVersion = ((RequiresMinApi)a).value();
      }
    }
    for (Annotation a : desc.getAnnotations()) {
      if (a instanceof RequiresMinApi) {
        // Method scoped requirements take precedence over class scoped
        // requirements.
        requiredApiVersion = ((RequiresMinApi)a).value();
      }
    }

    if (requiredApiVersion > getMaximumAvailableApiLevel()) {
      Log.i(TAG, desc.getMethodName() + " skipped because it requires API " + requiredApiVersion +
                     " but only API " + getMaximumAvailableApiLevel() + " is present.");
    } else if (packageName.equals("org.chromium.net.urlconnection")) {
      try {
        if (desc.getAnnotation(CompareDefaultWithCronet.class) != null) {
          // Run with the default HttpURLConnection implementation first.
          setTestingSystemHttpURLConnection(true);
          base.evaluate();
          // Use Cronet's implementation, and run the same test.
          setTestingSystemHttpURLConnection(false);
          base.evaluate();
        } else {
          // For all other tests.
          base.evaluate();
        }
      } catch (Throwable e) {
        throw new Throwable("Cronet Test failed.", e);
      }
    } else if (packageName.equals("org.chromium.net")) {
      try {
        base.evaluate();
        if (desc.getAnnotation(OnlyRunNativeCronet.class) == null) {
          setTestingJavaImpl(true);
          base.evaluate();
        }
      } catch (Throwable e) {
        throw new Throwable("CronetTestBase#runTest failed.", e);
      }
    } else {
      base.evaluate();
    }
  }

  void setUp() {
    AndroidJniLibrary.loadTestLibrary();
    ContextUtils.initApplicationContext(getContext().getApplicationContext());
    PathUtils.setPrivateDataDirectorySuffix(PRIVATE_DATA_DIRECTORY_SUFFIX);
    prepareTestStorage();
    mOldVmPolicy = StrictMode.getVmPolicy();
    // Only enable StrictMode testing after leaks were fixed in crrev.com/475945
    if (getMaximumAvailableApiLevel() >= 7) {
      StrictMode.setVmPolicy(new StrictMode.VmPolicy.Builder()
                                 .detectLeakedClosableObjects()
                                 .penaltyLog()
                                 .penaltyDeath()
                                 .build());
    }
  }

  void tearDown() {
    if (mCronetTestFramework != null) {
      mCronetTestFramework.mCronetEngine.shutdown();
      mCronetTestFramework = null;
    }

    if (mUrlConnectionCronetEngine != null) {
      mUrlConnectionCronetEngine.shutdown();
      mUrlConnectionCronetEngine = null;
    }

    resetURLStreamHandlerFactory();

    try {
      // Run GC and finalizers a few times to pick up leaked closeables
      for (int i = 0; i < 10; i++) {
        System.gc();
        System.runFinalization();
      }
      System.gc();
      System.runFinalization();
    } finally {
      StrictMode.setVmPolicy(mOldVmPolicy);
    }
  }

  /**
   * Starts the CronetTest framework.
   */
  public CronetTestFramework startCronetTestFramework() {
    if (mCronetTestFramework != null) {
      mCronetTestFramework.mCronetEngine.shutdown();
    }
    if (testingJavaImpl()) {
      ExperimentalCronetEngine.Builder builder = createJavaEngineBuilder();
      builder.setUserAgent(UserAgent.from(getContext()));
      mCronetTestFramework = new CronetTestFramework(builder.build());
      // Make sure that the instantiated engine is JavaCronetEngine.
      assert mCronetTestFramework.mCronetEngine.getClass() == JavaCronetEngine.class;
    } else {
      mCronetTestFramework = new CronetTestFramework(getContext());
    }
    return mCronetTestFramework;
  }

  /**
   * Creates and returns {@link ExperimentalCronetEngine.Builder} that creates
   * Java (platform) based {@link CronetEngine.Builder}.
   *
   * @return the {@code CronetEngine.Builder} that builds Java-based {@code Cronet engine}.
   */
  public ExperimentalCronetEngine.Builder createJavaEngineBuilder() {
    return (ExperimentalCronetEngine.Builder) new JavaCronetProvider(getContext()).createBuilder();
  }

  public void assertResponseEquals(UrlResponseInfo expected, UrlResponseInfo actual) {
    assertEquals(expected.getAllHeaders(), actual.getAllHeaders());
    assertEquals(expected.getAllHeadersAsList(), actual.getAllHeadersAsList());
    assertEquals(expected.getHttpStatusCode(), actual.getHttpStatusCode());
    assertEquals(expected.getHttpStatusText(), actual.getHttpStatusText());
    assertEquals(expected.getUrlChain(), actual.getUrlChain());
    assertEquals(expected.getUrl(), actual.getUrl());
    // Transferred bytes and proxy server are not supported in pure java
    if (!testingJavaImpl()) {
      // TODO("https://github.com/envoyproxy/envoy-mobile/issues/1426"): uncomment the assert
      // assertEquals(expected.getReceivedByteCount(), actual.getReceivedByteCount());
      assertEquals(expected.getProxyServer(), actual.getProxyServer());
      // This is a place where behavior intentionally differs between native and java
      assertEquals(expected.getNegotiatedProtocol(), actual.getNegotiatedProtocol());
    }
  }

  public static void assertContains(String expectedSubstring, String actualString) {
    Assert.assertNotNull(actualString);
    if (!actualString.contains(expectedSubstring)) {
      fail("String [" + actualString + "] doesn't contain substring [" + expectedSubstring + "]");
    }
  }

  public CronetEngine.Builder enableDiskCache(CronetEngine.Builder cronetEngineBuilder) {
    cronetEngineBuilder.setStoragePath(getTestStorage());
    cronetEngineBuilder.enableHttpCache(CronetEngine.Builder.HTTP_CACHE_DISK, 1000 * 1024);
    return cronetEngineBuilder;
  }

  /**
   * Sets the {@link URLStreamHandlerFactory} from {@code cronetEngine}. This should be called
   * during setUp() and is installed by {@code runTest()} as the default when Cronet is tested.
   */
  public void setStreamHandlerFactory(CronetEngine cronetEngine) {
    mUrlConnectionCronetEngine = cronetEngine;
    if (testingSystemHttpURLConnection()) {
      URL.setURLStreamHandlerFactory(null);
    } else {
      URL.setURLStreamHandlerFactory(mUrlConnectionCronetEngine.createURLStreamHandlerFactory());
    }
  }

  /**
   * Clears the {@link URL}'s {@code factory} field. This is called
   * during teardown() so as to start each test with the system's URLStreamHandler.
   */
  private void resetURLStreamHandlerFactory() {
    try {
      Field factory = URL.class.getDeclaredField("factory");
      factory.setAccessible(true);
      factory.set(null, null);
    } catch (IllegalAccessException | NoSuchFieldException e) {
      throw new RuntimeException("CronetTestRule#shutdown: factory could not be reset", e);
    }
  }

  /**
   * Annotation for test methods in org.chromium.net.urlconnection pacakage that runs them
   * against both Cronet's HttpURLConnection implementation, and against the system's
   * HttpURLConnection implementation.
   */
  @Target(ElementType.METHOD)
  @Retention(RetentionPolicy.RUNTIME)
  public @interface CompareDefaultWithCronet {}

  /**
   * Annotation for test methods in org.chromium.net.urlconnection pacakage that runs them
   * only against Cronet's HttpURLConnection implementation, and not against the system's
   * HttpURLConnection implementation.
   */
  @Target(ElementType.METHOD)
  @Retention(RetentionPolicy.RUNTIME)
  public @interface OnlyRunCronetHttpURLConnection {}

  /**
   * Annotation for test methods in org.chromium.net package that disables rerunning the test
   * against the Java-only implementation. When this annotation is present the test is only run
   * against the native implementation.
   */
  @Target(ElementType.METHOD)
  @Retention(RetentionPolicy.RUNTIME)
  public @interface OnlyRunNativeCronet {}

  /**
   * Annotation allowing classes or individual tests to be skipped based on the version of the
   * Cronet API present. Takes the minimum API version upon which the test should be run.
   * For example if a test should only be run with API version 2 or greater:
   *   @RequiresMinApi(2)
   *   public void testFoo() {}
   */
  @Target({ElementType.TYPE, ElementType.METHOD})
  @Retention(RetentionPolicy.RUNTIME)
  public @interface RequiresMinApi {
    int value();
  }

  /**
   * Prepares the path for the test storage (http cache, QUIC server info).
   */
  public static void prepareTestStorage() {
    File storage = new File(getTestStorageDirectory());
    if (storage.exists()) {
      Assert.assertTrue(recursiveDelete(storage));
    }
    ensureTestStorageExists();
  }

  /**
   * Returns the path for the test storage (http cache, QUIC server info).
   * Also ensures it exists.
   */
  public static String getTestStorage() {
    ensureTestStorageExists();
    return getTestStorageDirectory();
  }

  /**
   * Returns the path for the test storage (http cache, QUIC server info).
   * NOTE: Does not ensure it exists; tests should use {@link #getTestStorage}.
   */
  private static String getTestStorageDirectory() {
    return PathUtils.getDataDirectory() + "/test_storage";
  }

  /**
   * Ensures test storage directory exists, i.e. creates one if it does not exist.
   */
  private static void ensureTestStorageExists() {
    File storage = new File(getTestStorageDirectory());
    if (!storage.exists()) {
      Assert.assertTrue(storage.mkdir());
    }
  }

  private static boolean recursiveDelete(File path) {
    if (path.isDirectory()) {
      for (File c : path.listFiles()) {
        if (!recursiveDelete(c)) {
          return false;
        }
      }
    }
    return path.delete();
  }

  private void setTestingSystemHttpURLConnection(boolean value) {
    mTestingSystemHttpURLConnection = value;
  }

  private void setTestingJavaImpl(boolean value) { mTestingJavaImpl = value; }
}
