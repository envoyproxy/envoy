package org.chromium.net;

/**
 * {@link BidirectionalStream} that exposes experimental features. To obtain an instance of this
 * class, cast a {@code BidirectionalStream} to this type. Every instance of {@code
 * BidirectionalStream} can be cast to an instance of this class, as they are backed by the same
 * implementation and hence perform identically. Instances of this class are not meant for general
 * use, but instead only to access experimental features. Experimental features may be deprecated in
 * the future. Use at your own risk.
 *
 * <p>{@hide prototype}
 */
public abstract class ExperimentalBidirectionalStream extends BidirectionalStream {
  /**
   * {@link BidirectionalStream#Builder} that exposes experimental features. To obtain an instance
   * of this class, cast a {@code BidirectionalStream.Builder} to this type. Every instance of
   * {@code BidirectionalStream.Builder} can be cast to an instance of this class, as they are
   * backed by the same implementation and hence perform identically. Instances of this class are
   * not meant for general use, but instead only to access experimental features. Experimental
   * features may be deprecated in the future. Use at your own risk.
   */
  public abstract static class Builder extends BidirectionalStream.Builder {
    /**
     * Associates the annotation object with this request. May add more than one. Passed through to
     * a {@link RequestFinishedInfo.Listener}, see {@link RequestFinishedInfo#getAnnotations}.
     *
     * @param annotation an object to pass on to the {@link RequestFinishedInfo.Listener} with a
     *     {@link RequestFinishedInfo}.
     * @return the builder to facilitate chaining.
     */
    public Builder addRequestAnnotation(Object annotation) { return this; }

    /**
     * Sets {@link android.net.TrafficStats} tag to use when accounting socket traffic caused by
     * this request. See {@link android.net.TrafficStats} for more information. If no tag is set
     * (e.g. this method isn't called), then Android accounts for the socket traffic caused by this
     * request as if the tag value were set to 0.
     *
     * <p><b>NOTE:</b>Setting a tag disallows sharing of sockets with requests with other tags,
     * which may adversely effect performance by prohibiting connection sharing. In other words use
     * of multiplexed sockets (e.g. HTTP/2 and QUIC) will only be allowed if all requests have the
     * same socket tag.
     *
     * @param tag the tag value used to when accounting for socket traffic caused by this request.
     *     Tags between 0xFFFFFF00 and 0xFFFFFFFF are reserved and used internally by system
     *     services like {@link android.app.DownloadManager} when performing traffic on behalf of an
     *     application.
     * @return the builder to facilitate chaining.
     */
    public Builder setTrafficStatsTag(int tag) { return this; }

    /**
     * Sets specific UID to use when accounting socket traffic caused by this request. See {@link
     * android.net.TrafficStats} for more information. Designed for use when performing an operation
     * on behalf of another application. Caller must hold {@link
     * android.Manifest.permission#MODIFY_NETWORK_ACCOUNTING} permission. By default traffic is
     * attributed to UID of caller.
     *
     * <p><b>NOTE:</b>Setting a UID disallows sharing of sockets with requests with other UIDs,
     * which may adversely effect performance by prohibiting connection sharing. In other words use
     * of multiplexed sockets (e.g. HTTP/2 and QUIC) will only be allowed if all requests have the
     * same UID set.
     *
     * @param uid the UID to attribute socket traffic caused by this request.
     * @return the builder to facilitate chaining.
     */
    public Builder setTrafficStatsUid(int uid) { return this; }

    // To support method chaining, override superclass methods to return an
    // instance of this class instead of the parent.

    @Override public abstract Builder setHttpMethod(String method);

    @Override public abstract Builder addHeader(String header, String value);

    @Override public abstract Builder setPriority(int priority);

    @Override
    public abstract Builder
    delayRequestHeadersUntilFirstFlush(boolean delayRequestHeadersUntilFirstFlush);

    @Override public abstract ExperimentalBidirectionalStream build();
  }
}
