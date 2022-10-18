package org.chromium.net.testing;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;
import static org.junit.Assume.assumeTrue;

import android.content.Context;
import android.os.Build;
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
import org.chromium.net.impl.NativeCronetProvider;
import org.chromium.net.impl.UserAgent;
import org.junit.Assert;
import org.junit.rules.TestRule;
import org.junit.runner.Description;
import org.junit.runners.model.Statement;
import org.chromium.net.AndroidNetworkLibrary;

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
  public static final String SERVER_CERT_PEM =
      "../envoy/test/config/integration/certs/upstreamcert.pem";

  /**
   * Name of the file that contains the test server private key in PKCS8 PEM format.
   */
  public static final String SERVER_KEY_PKCS8_PEM =
      "../envoy/test/config/integration/certs/upstreamkey.pem";

  private static final String TAG = CronetTestRule.class.getSimpleName();

  /**
   * Creates and holds pointer to CronetEngine.
   */
  public static class CronetTestFramework {
    public ExperimentalCronetEngine mCronetEngine;
    public ExperimentalCronetEngine.Builder mBuilder;

    private Context mContext;
    private boolean mIsTestingJavaImpl;

    private CronetTestFramework(Context context, boolean isTestingJavaImpl) {
      mContext = context;
      mIsTestingJavaImpl = isTestingJavaImpl;
      mBuilder = mIsTestingJavaImpl ? createJavaEngineBuilder() : createNativeEngineBuilder();
    }

    private static CronetTestFramework createUsingJavaImpl(Context context) {
      return new CronetTestFramework(context, true /* isTestingJavaImpl */);
    }

    private static CronetTestFramework createUsingNativeImpl(Context context) {
      return new CronetTestFramework(context, false /* isTestingJavaImpl */);
    }

    public ExperimentalCronetEngine startEngine() {
      assert mCronetEngine == null;

      mCronetEngine = mBuilder.build();
      if (mIsTestingJavaImpl) {
        // Make sure that the instantiated engine is JavaCronetEngine.
        assert mCronetEngine.getClass() == JavaCronetEngine.class;
      }

      // Start collecting metrics.
      mCronetEngine.getGlobalMetricsDeltas();

      return mCronetEngine;
    }

    public void shutdownEngine() {
      if (mCronetEngine == null)
        return;
      mCronetEngine.shutdown();
      mCronetEngine = null;
    }

    private ExperimentalCronetEngine.Builder createJavaEngineBuilder() {
      return CronetTestRule.createJavaEngineBuilder(mContext)
          .setUserAgent(UserAgent.from(getContext()))
          .enableQuic(true);
    }

    private ExperimentalCronetEngine.Builder createNativeEngineBuilder() {
      return CronetTestRule.createNativeEngineBuilder(mContext).enableQuic(true);
    }
  }

  int getMaximumAvailableApiLevel() {
    // Prior to M59 the ApiVersion.getMaximumAvailableApiLevel API didn't exist
    int cronetMajorVersion = Integer.parseInt(ApiVersion.getCronetVersion().split("\\.")[0]);
    if (cronetMajorVersion < 59) {
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

    boolean onlyRunTestForNative = desc.getAnnotation(OnlyRunNativeCronet.class) != null;
    boolean onlyRunTestForJava = desc.getAnnotation(OnlyRunJavaCronet.class) != null;
    if (onlyRunTestForNative && onlyRunTestForJava) {
      throw new IllegalArgumentException(desc.getMethodName() +
                                         " skipped because it specified both "
                                         + "OnlyRunNativeCronet and OnlyRunJavaCronet annotations");
    }
    boolean doRunTestForNative = onlyRunTestForNative || !onlyRunTestForJava;
    boolean doRunTestForJava = onlyRunTestForJava || !onlyRunTestForNative;

    // Find the API version required by the test.
    int requiredApiVersion = getMaximumAvailableApiLevel();
    int requiredAndroidApiVersion = Build.VERSION_CODES.KITKAT;
    for (Annotation a : desc.getTestClass().getAnnotations()) {
      if (a instanceof RequiresMinApi) {
        requiredApiVersion = ((RequiresMinApi)a).value();
      }
      if (a instanceof RequiresMinAndroidApi) {
        requiredAndroidApiVersion = ((RequiresMinAndroidApi)a).value();
      }
    }
    for (Annotation a : desc.getAnnotations()) {
      if (a instanceof RequiresMinApi) {
        // Method scoped requirements take precedence over class scoped
        // requirements.
        requiredApiVersion = ((RequiresMinApi)a).value();
      }
      if (a instanceof RequiresMinAndroidApi) {
        requiredAndroidApiVersion = ((RequiresMinAndroidApi)a).value();
      }
    }
    assumeTrue(desc.getMethodName() + " skipped because it requires API " + requiredApiVersion +
                   " but only API " + getMaximumAvailableApiLevel() + " is present.",
               getMaximumAvailableApiLevel() >= requiredApiVersion);
    assumeTrue(desc.getMethodName() + " skipped because it Android's API level " +
                   requiredAndroidApiVersion + " but test device supports only API " +
                   Build.VERSION.SDK_INT,
               Build.VERSION.SDK_INT >= requiredAndroidApiVersion);

    if (packageName.equals("org.chromium.net.urlconnection")) {
      if (desc.getAnnotation(CompareDefaultWithCronet.class) != null) {
        try {
          // Run with the default HttpURLConnection implementation first.
          setTestingSystemHttpURLConnection(true);
          base.evaluate();
          // Use Cronet's implementation, and run the same test.
          setTestingSystemHttpURLConnection(false);
          base.evaluate();
        } catch (Throwable e) {
          Log.e(TAG, String.format("CronetTestBase#runTest failed for %s implementation.",
                                   testingSystemHttpURLConnection() ? "System" : "Cronet"));
          throw e;
        }
      } else {
        // For all other tests.
        base.evaluate();
      }
    } else if (packageName.startsWith("org.chromium.net")) {
      try {
        if (doRunTestForNative) {
          Log.i(TAG, "Running test against Native implementation.");
          base.evaluate();
        }
        if (doRunTestForJava) {
          Log.i(TAG, "Running test against Java implementation.");
          setTestingJavaImpl(true);
          base.evaluate();
        }
      } catch (Throwable e) {
        Log.e(TAG, String.format("CronetTestBase#runTest failed for %s implementation.",
                                 testingJavaImpl() ? "Java" : "Native"));
        throw e;
      }
    } else {
      base.evaluate();
    }
  }

  private void setUp() {
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
    AndroidNetworkLibrary.setFakeCertificateVerificationForTesting(true);
  }

  private void tearDown() {
    if (mCronetTestFramework != null) {
      mCronetTestFramework.mCronetEngine.shutdown();
      mCronetTestFramework = null;
    }

    if (mUrlConnectionCronetEngine != null) {
      mUrlConnectionCronetEngine.shutdown();
      mUrlConnectionCronetEngine = null;
    }

    resetURLStreamHandlerFactory();
    AndroidNetworkLibrary.setFakeCertificateVerificationForTesting(false);

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

  private CronetTestFramework createCronetTestFramework() {
    mCronetTestFramework = testingJavaImpl()
                               ? CronetTestFramework.createUsingJavaImpl(getContext())
                               : CronetTestFramework.createUsingNativeImpl(getContext());
    return mCronetTestFramework;
  }

  /**
   * Builds and starts the CronetTest framework.
   */
  public CronetTestFramework startCronetTestFramework() {
    createCronetTestFramework();
    mCronetTestFramework.startEngine();
    return mCronetTestFramework;
  }

  /**
   * Builds the CronetTest framework.
   */
  public CronetTestFramework buildCronetTestFramework() { return createCronetTestFramework(); }

  /**
   * Creates and returns {@link ExperimentalCronetEngine.Builder} that creates
   * Java (platform) based {@link CronetEngine.Builder}.
   *
   * @return the {@code CronetEngine.Builder} that builds Java-based {@code Cronet engine}.
   */
  public static ExperimentalCronetEngine.Builder createJavaEngineBuilder(Context context) {
    return (ExperimentalCronetEngine.Builder) new JavaCronetProvider(context).createBuilder();
  }

  /**
   * Creates and returns {@link ExperimentalCronetEngine.Builder} that creates
   * Chromium (native) based {@link CronetEngine.Builder}.
   *
   * @return the {@code CronetEngine.Builder} that builds Chromium-based {@code Cronet engine}.
   */
  public static ExperimentalCronetEngine.Builder createNativeEngineBuilder(Context context) {
    return (ExperimentalCronetEngine.Builder) new NativeCronetProvider(context).createBuilder();
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
      assertEquals(expected.getReceivedByteCount(), actual.getReceivedByteCount());
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
   * Annotation for test methods in org.chromium.net package that disables rerunning the test
   * against the Native/Chromium implementation. When this annotation is present the test is only
   * run against the Java implementation.
   */
  @Target(ElementType.METHOD)
  @Retention(RetentionPolicy.RUNTIME)
  public @interface OnlyRunJavaCronet {}

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
   * Annotation allowing classes or individual tests to be skipped based on the Android OS version
   * installed in the deviced used for testing. Takes the minimum API version upon which the test
   * should be run. For example if a test should only be run with Android Oreo or greater:
   *   @RequiresMinApi(Build.VERSION_CODES.O)
   *   public void testFoo() {}
   */
  @Target({ElementType.TYPE, ElementType.METHOD})
  @Retention(RetentionPolicy.RUNTIME)
  public @interface RequiresMinAndroidApi {
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
