package org.chromium.net;

import android.content.Context;
import android.util.Log;
import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

/**
 * Provides a factory method to create {@link CronetEngine.Builder} instances. A {@code
 * CronetEngine.Builder} instance can be used to create a specific {@link CronetEngine}
 * implementation. To get the list of available {@link CronetProvider}s call {@link
 * #getAllProviders(Context)}.
 *
 * <p><b>NOTE:</b> This class is for advanced users that want to select a particular Cronet
 * implementation. Most users should simply use {@code new} {@link
 * CronetEngine.Builder#CronetEngine.Builder(android.content.Context)}.
 *
 * <p>{@hide}
 */
public abstract class CronetProvider {
  /**
   * String returned by {@link CronetProvider#getName} for {@link CronetProvider} that provides
   * native Cronet implementation packaged inside an application. This implementation offers
   * significantly higher performance relative to the fallback Cronet implementations (see {@link
   * #PROVIDER_NAME_FALLBACK}).
   */
  public static final String PROVIDER_NAME_APP_PACKAGED = "App-Packaged-Cronet-Provider";

  /**
   * String returned by {@link CronetProvider#getName} for {@link CronetProvider} that provides
   * Cronet implementation based on the system's {@link java.net.HttpURLConnection} implementation.
   * This implementation offers significantly degraded performance relative to native Cronet
   * implementations (see {@link #PROVIDER_NAME_APP_PACKAGED}).
   */
  public static final String PROVIDER_NAME_FALLBACK = "Fallback-Cronet-Provider";

  /**
   * The name of an optional key in the app string resource file that contains the class name of an
   * alternative {@code CronetProvider} implementation.
   */
  private static final String RES_KEY_CRONET_IMPL_CLASS = "CronetProviderClassName";

  private static final String TAG = CronetProvider.class.getSimpleName();

  protected final Context mContext;

  protected CronetProvider(Context context) {
    if (context == null) {
      throw new IllegalArgumentException("Context must not be null");
    }
    mContext = context;
  }

  /**
   * Creates and returns an instance of {@link CronetEngine.Builder}.
   *
   * <p><b>NOTE:</b> This class is for advanced users that want to select a particular Cronet
   * implementation. Most users should simply use {@code new} {@link
   * CronetEngine.Builder#CronetEngine.Builder(android.content.Context)}.
   *
   * @return {@code CronetEngine.Builder}.
   * @throws IllegalStateException if the provider is not enabled (see {@link #isEnabled}.
   */
  public abstract CronetEngine.Builder createBuilder();

  /**
   * Returns the provider name. The well-know provider names include:
   *
   * <ul>
   *   <li>{@link #PROVIDER_NAME_APP_PACKAGED}
   *   <li>{@link #PROVIDER_NAME_FALLBACK}
   * </ul>
   *
   * @return provider name.
   */
  public abstract String getName();

  /**
   * Returns the provider version. The version can be used to select the newest available provider
   * if multiple providers are available.
   *
   * @return provider version.
   */
  public abstract String getVersion();

  /**
   * Returns whether the provider is enabled and can be used to instantiate the Cronet engine. A
   * provider being out-of-date (older than the API) and needing updating is one potential reason it
   * could be disabled. Please read the provider documentation for enablement procedure.
   *
   * @return {@code true} if the provider is enabled.
   */
  public abstract boolean isEnabled();

  @Override
  public String toString() {
    return "["
        + "class=" + getClass().getName() + ", "
        + "name=" + getName() + ", "
        + "version=" + getVersion() + ", "
        + "enabled=" + isEnabled() + "]";
  }

  /** Name of the Java {@link CronetProvider} class. */
  private static final String JAVA_CRONET_PROVIDER_CLASS =
      "org.chromium.net.impl.JavaCronetProvider";

  /** Name of the native {@link CronetProvider} class. */
  private static final String NATIVE_CRONET_PROVIDER_CLASS =
      "org.chromium.net.impl.NativeCronetProvider";

  /** {@link CronetProvider} class that is packaged with Google Play Services. */
  private static final String PLAY_SERVICES_CRONET_PROVIDER_CLASS =
      "com.google.android.gms.net.PlayServicesCronetProvider";

  /**
   * {@link CronetProvider} a deprecated class that may be packaged with some old versions of Google
   * Play Services.
   */
  private static final String GMS_CORE_CRONET_PROVIDER_CLASS =
      "com.google.android.gms.net.GmsCoreCronetProvider";

  /**
   * Returns an unmodifiable list of all available {@link CronetProvider}s. The providers are
   * returned in no particular order. Some of the returned providers may be in a disabled state and
   * should be enabled by the invoker. See {@link CronetProvider#isEnabled()}.
   *
   * @return the list of available providers.
   */
  public static List<CronetProvider> getAllProviders(Context context) {
    // Use LinkedHashSet to preserve the order and eliminate duplicate providers.
    Set<CronetProvider> providers = new LinkedHashSet<>();
    addCronetProviderFromResourceFile(context, providers);
    addCronetProviderImplByClassName(context, PLAY_SERVICES_CRONET_PROVIDER_CLASS, providers,
                                     false);
    addCronetProviderImplByClassName(context, GMS_CORE_CRONET_PROVIDER_CLASS, providers, false);
    addCronetProviderImplByClassName(context, NATIVE_CRONET_PROVIDER_CLASS, providers, false);
    addCronetProviderImplByClassName(context, JAVA_CRONET_PROVIDER_CLASS, providers, false);
    return Collections.unmodifiableList(new ArrayList<>(providers));
  }

  /**
   * Attempts to add a new provider referenced by the class name to a set.
   *
   * @param className the class name of the provider that should be instantiated.
   * @param providers the set of providers to add the new provider to.
   * @return {@code true} if the provider was added to the set; {@code false} if the provider
   *     couldn't be instantiated.
   */
  private static boolean addCronetProviderImplByClassName(Context context, String className,
                                                          Set<CronetProvider> providers,
                                                          boolean logError) {
    ClassLoader loader = context.getClassLoader();
    try {
      Class<? extends CronetProvider> providerClass =
          loader.loadClass(className).asSubclass(CronetProvider.class);
      Constructor<? extends CronetProvider> ctor = providerClass.getConstructor(Context.class);
      providers.add(ctor.newInstance(context));
      return true;
    } catch (InstantiationException e) {
      logReflectiveOperationException(className, logError, e);
    } catch (InvocationTargetException e) {
      logReflectiveOperationException(className, logError, e);
    } catch (NoSuchMethodException e) {
      logReflectiveOperationException(className, logError, e);
    } catch (IllegalAccessException e) {
      logReflectiveOperationException(className, logError, e);
    } catch (ClassNotFoundException e) {
      logReflectiveOperationException(className, logError, e);
    }
    return false;
  }

  /**
   * De-duplicates exception handling logic in {@link #addCronetProviderImplByClassName}. It should
   * be removed when support of API Levels lower than 19 is deprecated.
   */
  private static void logReflectiveOperationException(String className, boolean logError,
                                                      Exception e) {
    if (logError) {
      Log.e(TAG, "Unable to load provider class: " + className, e);
    } else {
      if (Log.isLoggable(TAG, Log.DEBUG)) {
        Log.d(TAG, "Tried to load " + className + " provider class but it wasn't"
                       + " included in the app classpath");
      }
    }
  }

  /**
   * Attempts to add a provider specified in the app resource file to a set.
   *
   * @param providers the set of providers to add the new provider to.
   * @return {@code true} if the provider was added to the set; {@code false} if the app resources
   *     do not include the string with {@link #RES_KEY_CRONET_IMPL_CLASS} key.
   * @throws RuntimeException if the provider cannot be found or instantiated.
   */
  private static boolean addCronetProviderFromResourceFile(Context context,
                                                           Set<CronetProvider> providers) {
    int resId = context.getResources().getIdentifier(RES_KEY_CRONET_IMPL_CLASS, "string",
                                                     context.getPackageName());
    // Resource not found
    if (resId == 0) {
      // The resource wasn't included in the app; therefore, there is nothing to add.
      return false;
    }
    String className = context.getResources().getString(resId);

    // If the resource specifies a well known provider, don't load it because
    // there will be an attempt to load it anyways.
    if (className == null || className.equals(PLAY_SERVICES_CRONET_PROVIDER_CLASS) ||
        className.equals(GMS_CORE_CRONET_PROVIDER_CLASS) ||
        className.equals(JAVA_CRONET_PROVIDER_CLASS) ||
        className.equals(NATIVE_CRONET_PROVIDER_CLASS)) {
      return false;
    }

    if (!addCronetProviderImplByClassName(context, className, providers, true)) {
      Log.e(TAG, "Unable to instantiate Cronet implementation class " + className +
                     " that is listed as in the app string resource file under " +
                     RES_KEY_CRONET_IMPL_CLASS + " key");
    }
    return true;
  }
}
