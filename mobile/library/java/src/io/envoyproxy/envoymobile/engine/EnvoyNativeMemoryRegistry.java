package io.envoyproxy.envoymobile.engine;

import java.lang.ref.PhantomReference;
import java.lang.ref.ReferenceQueue;

import io.envoyproxy.envoymobile.engine.EnvoyNativeResourceReleaser;
import io.envoyproxy.envoymobile.engine.EnvoyNativeResourceWrapper;

/**
 * Central class to manage releasing native resources when wrapper objects are flagged as
 * unreachable by the garbage collector.
 */
public enum EnvoyNativeResourceRegistry {
  SINGLETON;

  private ReferenceQueue
      refQueue; // References are automatically enqueued when the gc flags them as unreachable.
  private Collection refMaintainer; // Maintains references in the object graph while we wait for
                                    // them to be enqueued.

  private class QueueThread extends Thread {
    public void run() {
      while (EnvoyPhantomRef releasable = (EnvoyPhantomRef)refQueue.remove()) {
        releasable.releaseResource();
        refMaintainer.remove(releasable);
      }
    }
  }

  private class EnvoyPhantomRef extends PhantomReference<EnvoyNativeResourceWrapper> {
    private final EnvoyNativeResourceReleaser releaser;
    private final long nativeHandle;

    EnvoyPhantomRef(EnvoyNativeResourceWrapper owner, long nativeHandle,
                    EnvoyNativeResourceReleaser releaser) {
      super(owner, refQueue);
      this.nativeHandle = nativeHandle;
      this.releaser = releaser;
    }

    releaseResource() { releaser.run(nativeHandle); }
  }

  /**
   * Register an EnvoyNativeResourceWrapper to schedule cleanup of its native resources when the
   * Java object is flagged for collection by the garbage collector.
   */
  public void register(EnvoyNativeResourceWrapper owner, long nativeHandle,
                       EnvoyNativeResourceReleaser releaser) {
    EnvoyPhantomRef ref = new EnvoyPhantomRef(owner, nativeHandle, releaser);
    refMaintainer.add(ref);
  }

  public static void register(EnvoyNativeResourceWrapper owner, long nativeHandle,
                              EnvoyNativeResourceReleaser releaser) {
    SINGLETON.register(owner, nativeHandle, releaser);
  }
}
