// Functions to render a JSON histogram representation into an HTML/CSS.
// There are several ways to do this:
//   1. lay out in CSS with flex-boxes
//   2. draw using SVG
//   3. render as `divs` with pixel positions
//   4. render as `divs` with percentage positions
// This implements option #4. There are pros/cons to each of these. The benefits of #4:
//   1. The user can get a clearer picture by making the window bigger, without having
//      to re-layout the graphics.
//   2. The `divs` can be made sensitive to mouse enter/leave to pop up
//      more detail
// There are also drawbacks:
//   1. Need to write graphics layout code, and think about coordinate systems, resulting
//      in several hundred lines of JavaScript.
//   2. Some risk of having the text look garbled in some scenarios. This appears to
//      to be mitigated with heuristics and use of popup elements when more than one
//      percentile or interval-data falls within a bucket.

const constants = {
  // Horizontal spacing between buckets, expressed in `VPX` (Virtual Pixels) in a
  // coordinate system invented to make arithmetic and debugging easier.
  marginWidthVpx: 20,

  // Horizontal spacing between the edge of the window and the histogram buckets,
  // expressed as a fraction.
  outerMarginFraction: 0.01,

  // The minimum height of a bucket bar, expressed as a percentage of the
  // configured height of a configured bar. By giving a histogram with count=1
  // a minimum height we make it easier for the mouse to hover over it, in
  // order to pop up more detail.
  baseHeightFraction: 0.03,

  // If there are too many buckets, per-bucket label text gets too dense and the
  // text becomes illegible, so skip some if needed. We always put the range in
  // the popup, and when skipped, we'll put the count in the popup as well, in
  // addition to any percentiles or interval ranges.
  maxBucketsWithText: 20,
};


const globalState = {
  // Holds a function to be called when the mouse leaves a popup. This is
  // null when there is no visible popup.
  pendingLeave: null,

  // Holds the timer object for the currently-visible popup. This is
  // maintained so we can cancel the timeout if the user moves to a new
  // bucket, in which case we'll immediately hide the popup so we can
  // display the new one.
  pendingTimeout: null,

  // Holds the histogram bucket object that is currently highlighted,
  // which makes it possible to erase the highlight after the mouse moves
  // out of it
  highlightedBucket: null,
};


/**
 * Formula to compute bucket width based on number of buckets. This formula
 * was derived from trial and error, to improve the appearance of the display.
 * Consider this table of values:
 *     numBuckets   `bucketWidthVpx`  Ratio vs `marginWidthVpx`
 *     1            2                 1:10
 *     2            21                1:2
 *     3            27                2:3
 *     10           36                1.8:1
 *     20           38                1.9:1
 * These ratios appear to look good across a wide range of numBuckets.
 *
 * @param {number} numBuckets the number of buckets in the histogram.
 * @return {number} The bucket width in virtual pixels.
 */
function computeBucketWidthVpx(numBuckets) {
  return 40 - 38/numBuckets;
}


/**
 * Top-level entry point to render all the detailed histograms found in a JSON
 * stats response.
 *
 * @param {!Element} histogramDiv the element in which to render the histograms.
 * @param {!Object} data the stats JSON structure obtained from the server /stats method.
 */
function renderHistograms(histogramDiv, data) { // eslint-disable-line no-unused-vars
  histogramDiv.replaceChildren();
  for (stat of data.stats) {
    const histograms = stat.histograms;
    if (histograms) {
      if (histograms.supported_percentiles && histograms.details) {
        for (histogram of histograms.details) {
          renderHistogram(histogramDiv, histograms.supported_percentiles, histogram);
        }
      }
    }
  }
}


/**
 * Formats a number using up to 2 decimal places. See
 * https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Intl/NumberFormat/NumberFormat
 */
const format = Intl.NumberFormat('en', { // eslint-disable-line new-cap
  notation: 'compact',
  maximumFractionDigits: 2,
}).format;


// Formats a percentage using up to 2 decimal places. These are
// used for writing CSS percents.
const formatPercent = Intl.NumberFormat('en', { // eslint-disable-line new-cap
  style: 'percent',
  maximumFractionDigits: 2,
}).format;


/**
 * Formats a range for a histogram bucket, using inclusive/beginning and
 * exclusive end, computed from the width.
 *
 * @param {number} lowerBound the lower bound of the range.
 * @param {number} width the range.
 * @return {string} A formatted string of the form "[lowerBound, upperBound)".
 */
function formatRange(lowerBound, width) {
  return '[' + format(lowerBound) + ', ' + format(lowerBound + width) + ')';
}


/**
 * Generates a function to render histogram information, capturing the
 * variables passed in. This is used to establish a mouse-enter list on
 * the histogram bucket, but without pulling in the entire drawing context.
 *
 * @param {!Element} detailPopup the popup div for this histogram.
 * @param {string} bucketPosPercent The bucket position, expressed as a string percentage.
 * @param {!Object} bucket the bucket record from JSON, augmented with an annotations list
 * @param {!Element} bucketSpan a span element for the bucket; used to color it yellow.
 * @return {!Function<!Event>} a function to call when the mouse enters the span.
 */
 function showPopupFn(detailPopup, bucketPosPercent, bucket, bucketSpan) {
  return (event) => {
    if (globalState.pendingTimeout) {
      window.clearTimeout(globalState.pendingTimeout);
      globalState.pendingLeave();
    }
    detailPopup.style.left = bucketPosPercent;
    detailPopup.style.visibility = 'visible';
    bucketSpan.style.backgroundColor = 'yellow';
    highlightedBucket = bucketSpan;

    detailPopup.replaceChildren();
    appendNewElement(detailPopup, 'div').textContent = formatRange(bucket.lower_bound, bucket.width);
    appendNewElement(detailPopup, 'div').textContent = 'count=' + bucket.count;
    if (bucket.annotations) {
      for (annotation of bucket.annotations) {
        const span = appendNewElement(detailPopup, 'div');
        span.textContent = annotation.detail();
      }
    }
  };
}


/**
 * Generates a timeout function for the provided popup div.
 *
 * @param {!Element} detailPopup the popup div.
 * @return {!Function<!Event>} a function to be called when the mouse leaves the span.
 */
function timeoutFn(detailPopup) {
  return (event) => {
    globalState.pendingLeave = leaveHandlerFn(detailPopup);
    globalState.pendingTimeout = window.setTimeout(globalState.pendingLeave, 2000);
  };
}


/**
 * Generates a handler function to be called when the mouse leaves
 * a popup.
 *
 * @param {!Element} detailPopup the popup div.
 * @return {!Function<!Event>} a function to hide the popup.
 */
function leaveHandlerFn(detailPopup) {
  return (event) => {
    globalState.pendingTimeout = null;
    globalState.pendingLeave = null;
    detailPopup.style.visibility = 'hidden';
    if (highlightedBucket) {
      highlightedBucket.style.backgroundColor = '#e6d7ff';
      highlightedBucket = null;
    }
  };
}


/**
 * When the user moves the mouse into the popup, we cancel the timer
 * to hide the popup, held in globalState.pendingTimeout. However the
 * globalState.pendingLeave function is left in place so a timeout can
 * be re-established when the mouse leaves the popup.
 */
function enterPopup() {
  // If the mouse enters the popup then cancel the timer that would
  // erase it. This should make it easier to cut&paste the contents
  // of the popup.
  if (globalState.pendingTimeout) {
    window.clearTimeout(globalState.pendingTimeout);
    globalState.pendingTimeout = null;
    // The 'leave' handler needs to remain -- we'll call that 2 seconds
    // after the mouse leaves the popup.
  }
}


/**
 * When the user moves the mouse out of the popup, we re-enable the
 * timeout that was (a) previously established when the mouse entered
 * the bucket span, and (b) disabled when the mouse entered the popup.
 */
function leavePopup() {
  globalState.pendingTimeout = window.setTimeout(globalState.pendingLeave, 2000);
}


/**
 * Helper function to create an element, append it to a parent, and optionally
 * install a CSS class.
 *
 * @param {!Element} parent the parent element.
 * @param {string} type the HTML element type, e.g. 'div'.
 * @param {?string} className optional CSS class name.
 * @return {!Element} the new element.
 */
function appendNewElement(parent, type, className) {
  const element = document.createElement(type);
  if (className) {
    element.className = className;
  }
  parent.appendChild(element);
  return element;
}


/**
 * Assigns percentiles and intervals to buckets. We do not expect percentiles or
 * intervals. If any occur, they will be assigned to the first or last bucket.
 *
 * We will only consider the lower_bound of interval-buckets and will ignore the
 * width of those buckets.
 *
 * We will assign intervals and percentiles to the bucket with the highest
 * lower-bound that is not greater than the interval lower_bound or percentile
 * value.
 *
 * @param {!Object} histogram JSON structure for a histogram.
 * @param {!Array<number>} supportedPercentiles Array of supported histograms.
 * @return {number} the maximum count across all buckets.
 */
function assignPercentilesAndIntervalsToBuckets(histogram, supportedPercentiles) {
  let maxCount = 0;
  let percentileIndex = 0;
  let intervalIndex = 0;
  const percentileValues = histogram.percentiles;
  let nextBucket = histogram.totals[0];

  for (let i = 0; i < histogram.totals.length; ++i) {
    const bucket = nextBucket;
    if (i < histogram.totals.length - 1) {
      nextBucket = histogram.totals[i + 1];
    } else {
      nextBucket = null;
    }
    maxCount = Math.max(maxCount, bucket.count);
    bucket.annotations = [];

    // Attach percentile records with values between the previous bucket and
    // this one. We will drop percentiles before the first bucket. Thus each
    // bucket starting with the second one will have a 'percentiles' property
    // with pairs of [percentile, value], which can then be used while
    // rendering the bucket.
    for (; percentileIndex < percentileValues.length; ++percentileIndex) {
      const percentileValue = percentileValues[percentileIndex].cumulative;
      if (nextBucket && percentileValue >= nextBucket.lower_bound) {
        break; // do not increment index; re-consider percentile for next bucket.
      }
      bucket.annotations.push(new Percentile(
          percentileValue, supportedPercentiles[percentileIndex]));
    }

    for (; intervalIndex < histogram.intervals.length; ++intervalIndex) {
      const interval = histogram.intervals[intervalIndex];
      if (nextBucket && interval.lower_bound >= nextBucket.lower_bound) {
        break; // do not increment index; re-consider interval for next bucket.
      }
      bucket.annotations.push(new Interval(interval.lower_bound, interval.width, interval.count));

      // It's unlikely that an interval bucket value will increase the overall maxCount
      // but just to be sure we'll scan through them.
      maxCount = Math.max(maxCount, interval.count);
    }

    if (bucket.annotations.length > 0) {
      bucket.annotations.sort((a, b) => {
        if (a.value == b.value) {
          if (a.cssClass() == b.cssClass()) {
            return 0;
          }
          if (a.cssClass() == 'histogram-percentile') {
            return -1;
          }
          return 1;
        }
        return a.value - b.value;
      });
      let aa = 0;
      for (a of bucket.annotations) {
        console.log('[' + aa++ + ']: ' + a.value + ' (' + a.cssClass() + ')');
      }
    }
  }
  return maxCount;
}

/**
 * Represents an annotation, which can be a percentile or interval-bucket.
 */
class Annotation {
  /**
   * @param {number} value The numeric value of the annotation.
   */
  constructor(value) {
    this.value = value;
  }

  /**
   * Returns the CSS class name used for rendering this annotation
   */
  cssClass() {
    throw new Error('pure virtual function cssClass');
  }

  /**
   * Returns formats the annotation as an abbreviated string.
   */
  toString() {
    throw new Error('pure virtual function toString');
  }

  /**
   * Returns formats the annotation with greater detail, suitable for a popup.
   */
  detail() {
    throw new Error('pure virtual function detail');
  }
}

/**
 * Represents a percentile.
 */
class Percentile extends Annotation {
  /**
   * @param {number} value The numeric value of the annotation.
   * @param {number} percentile The percentile number.
   */
  constructor(value, percentile) {
    super(value);
    this.percentile = percentile;
  }

  /**
   * @return {string} the css class name.
   */
  cssClass() {
    return 'histogram-percentile';
  }

  /**
   * @return {string} brief format for graphical annotation.
   */
  toString() {
    return 'P' + this.percentile;
  }

  /**
   * @return {string} detailed for popup.
   */
  detail() {
    return `P${this.percentile}: ${format(this.value)}`;
  }
}

/**
 * Represents a interval bucket.
 */
class Interval extends Annotation {
  /**
   * @param {number} value The lower bound of the bucket.
   * @param {number} width The width of the bucket.
   * @param {number} count The height of the bucket.
   */
  constructor(value, width, count) {
    super(value);
    this.width = width;
    this.count = count;
  }

  /**
   * @return {string} the css class name.
   */
  cssClass() {
    return 'histogram-interval';
  }

  /**
   * @return {string} brief format for graphical annotation.
   */
  toString() {
    return 'i:' + this.count;
  }

  /**
   * @return {string} detailed for popup.
   */
  detail() {
    return 'Interval ' + formatRange(this.value, this.width) + ': ' + this.count;
  }
}

/**
 * Captures context needed to lay out the histogram graphically.
 */
class Painter {
  /**
   * @param {!Element} div the HTML element into which to draw the histogram
   * @param {number} numBuckets the number of buckets.
   * @param {number} maxCount the maximum count for all buckets.
   */
  constructor(div, numBuckets, maxCount) {
    this.numBuckets = numBuckets;
    this.maxCount = maxCount;
    this.leftVpx = constants.marginWidthVpx;
    this.bucketWidthVpx = computeBucketWidthVpx(numBuckets);
    this.widthVpx = (numBuckets * this.bucketWidthVpx +
                     ((numBuckets + 1) * constants.marginWidthVpx) *
                     (1 + 2*constants.outerMarginFraction));
    this.textIntervalIndex = 0;
    this.bucketWidthPercent = formatPercent(this.vpxToWidth(this.bucketWidthVpx));

    this.graphics = appendNewElement(div, 'div', 'histogram-graphics');
    this.labels = appendNewElement(div, 'div', 'histogram-labels');
    this.annotationsDiv = appendNewElement(div, 'div', 'histogram-annotations');

    // We have business logic to ensure only be one popup div is visible at a
    // time.  However, we need a separate popup div for each histogram
    // so that they can be positioned relative to the histogram's graphics.
    this.detailPopup = appendNewElement(this.graphics, 'div', 'histogram-popup');
    this.detailPopup.addEventListener('mouseenter', enterPopup);
    this.detailPopup.addEventListener('mouseleave', leavePopup);

    this.textInterval = Math.ceil(numBuckets / constants.maxBucketsWithText);
    this.prevAnnotationVpx = null;
  }

  /**
   * @param {!Object} bucket the JSON info for the current bucket.
   * @param {?Object} nextBucket the JSON info for next bucket, or null if
   *                  bucket is the last one.
   * @param {!Annotation} annotation an annotation to draw.
   */
  drawAnnotation(bucket, nextBucket, annotation) {
    // Find the ideal place to draw the percentile bar, by linearly
    // interpolating between the current bucket and the previous bucket.
    // We know that the next bucket does not come into play because
    // the percentiles held underneath a bucket are based on a value that
    // is at most as large as the current bucket.
    let percentileVpx = this.leftVpx;
    const bucketDelta = nextBucket ? (nextBucket.lower_bound - bucket.lower_bound) : bucket.width;
    if (bucketDelta > 0) {
      let widthVpx = this.bucketWidthVpx;
      if (nextBucket) {
        widthVpx += constants.marginWidthVpx;
      }
      const nextVpx = this.leftVpx + widthVpx;
      const weight = (bucket.lower_bound + bucketDelta - annotation.value) / bucketDelta;
      percentileVpx = weight * this.leftVpx + (1 - weight) * nextVpx;
    }

    // We always put the marker proportionally between this bucket and
    // the next one.
    const span = appendNewElement(this.annotationsDiv, 'span', annotation.cssClass());
    const percentilePercent = formatPercent(this.vpxToPosition(percentileVpx));
    span.style.left = percentilePercent;

    // Don't draw textual labels for the percentiles and intervals if there are
    // more than one: they'll just get garbled. The user can over over the
    // bucket to see the detail.
    if (bucket.annotations.length == 1 &&
        (!this.prevAnnotationVpx || percentileVpx - this.prevAnnotationVpx > this.widthVpx/20)) {
      const percentilePLabel = appendNewElement(this.annotationsDiv, 'span', 'percentile-label');
      percentilePLabel.style.bottom = 0;
      percentilePLabel.textContent = annotation.toString();
      percentilePLabel.style.left = percentilePercent;

      const percentileVLabel = appendNewElement(this.annotationsDiv, 'span', 'percentile-label');
      percentileVLabel.style.bottom = '30%';
      percentileVLabel.textContent = format(annotation.value);
      percentileVLabel.style.left = percentilePercent;
      this.prevAnnotationVpx = percentileVpx;
    }
  }

  /**
   * Draws a bucket.
   *
   * @param {!Object} bucket the bucket to draw.
   */
  drawBucket(bucket) {
    this.leftPercent = formatPercent(this.vpxToPosition(this.leftVpx));

    const bucketSpan = appendNewElement(this.graphics, 'span', 'histogram-bucket');
    const heightPercent = this.maxCount == 0 ? 0 :
          formatPercent(constants.baseHeightFraction + (bucket.count / this.maxCount) *
                        (1 - constants.baseHeightFraction));
    bucketSpan.style.height = heightPercent;
    bucketSpan.style.width = this.bucketWidthPercent;
    bucketSpan.style.left = this.leftPercent;

    if (++this.textIntervalIndex == this.textInterval) {
      this.textIntervalIndex = 0;
      this.drawBucketLabels(bucket, heightPercent);
    }

    // Position the popup so it's left edge aligns with the left edge of the
    // bucket by default. Adjust it to the left by a bit so it doesn't get
    // clipped by the right edge of the viewport. This is heuristic but seems to
    // work well for a few test cases.
    let popupPos = this.leftVpx;
    if (popupPos / this.widthVpx > 0.9) {
      popupPos -= this.widthVpx/15;
    }
    bucketSpan.addEventListener('mouseenter', showPopupFn(
        this.detailPopup, formatPercent(this.vpxToPosition(popupPos)), bucket, bucketSpan));

    bucketSpan.addEventListener('mouseleave', timeoutFn(this.detailPopup));

    this.leftVpx += this.bucketWidthVpx + constants.marginWidthVpx;
  }

  /**
   * Draws the labels for a bucket.
   *
   * @param {!Object} bucket the bucket to draw.
   * @param {string} heightPercent The height of the bucket, expressed as a percent.
   */
  drawBucketLabels(bucket, heightPercent) {
    const lowerLabel = appendNewElement(this.labels, 'span');
    lowerLabel.textContent = format(bucket.lower_bound);
    lowerLabel.style.left = this.leftPercent;
    lowerLabel.style.width = this.bucketWidthPercent;

    const bucketLabel = appendNewElement(this.graphics, 'span', 'bucket-label');
    bucketLabel.textContent = format(bucket.count);
    bucketLabel.style.left = this.leftPercent;
    bucketLabel.style.width = this.bucketWidthPercent;
    bucketLabel.style.bottom = heightPercent;
  }

  /**
   * @param {number} virtualPixels the number of virtual pixels.
   * @return {number} the x-position as a percent, including an offset.
   */
  vpxToPosition(virtualPixels) {
    return this.vpxToWidth(virtualPixels) + constants.outerMarginFraction;
  }

  /**
   * @param {number} virtualPixels the number of virtual pixels.
   * @return {number} the x-position as a numeric percent.
   */
  vpxToWidth(virtualPixels) {
    return virtualPixels / this.widthVpx;
  }
}

/**
 * @param {!Element} histogramDiv the element in which to render the histograms.
 * @param {!Array<number>} supportedPercentiles Array of supported histograms.
 * @param {!Object} histogram the stats JSON structure obtained from the server /stats method.
 * @param {?number} changeCount the number of times this histogram has changed value.
 */
function renderHistogram(histogramDiv, supportedPercentiles, histogram, changeCount) {
  const div = appendNewElement(histogramDiv, 'div');
  const label = appendNewElement(div, 'span', 'histogram-name');
  label.textContent = histogram.name + (changeCount == null ? '' : ' (' + changeCount + ')');

  const numBuckets = histogram.totals.length;
  if (numBuckets == 0) {
    appendNewElement(div, 'span', 'histogram-no-data').textContent = 'No recorded values';
    return;
  }

  const maxCount = assignPercentilesAndIntervalsToBuckets(histogram, supportedPercentiles);

  // Lay out the buckets evenly, independent of the bucket values. It's up
  // to the `circlhist` library to space out the buckets in a shape tuned to
  // the data.
  //
  // We will not draw percentile lines outside of the bucket values. E.g. we
  // may skip drawing outer percentiles like P0 and P100 etc.
  //
  // We lay out horizontally based on CSS percentage so users can see the
  // graphics better if they make the window wider. We do this by inventing
  // arbitrary "virtual pixels" (variables with `Vpx` suffix) during the
  // computation in JS and converting them to percentages for writing element
  // style.
  const painter = new Painter(div, numBuckets, maxCount);

  for (let i = 0; i < numBuckets; ++i) {
    const bucket = histogram.totals[i];
    const nextBucket = (i < histogram.totals.length - 1) ? histogram.totals[i + 1] : null;
    for (annotation of bucket.annotations) {
      painter.drawAnnotation(bucket, nextBucket, annotation);
    }
    painter.drawBucket(bucket);
  }
}
