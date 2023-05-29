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
//      be rare enough that it's likely not worth investing time to fix at the moment.

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
  maxBucketsWithText: 30
};

let globalState = {
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
  highlightedBucket: null
};

// Formula to compute bucket width based on number of buckets. This formula
// was derived from trial and error, to improve the appearance of the display.
// Consider this table of values:
//     numBuckets   `bucketWidthVpx`  Ratio vs `marginWidthVpx`
//     1            2                 1:10
//     2            21                1:2
//     3            27                2:3
//     10           36                1.8:1
//     20           38                1.9:1
// These ratios appear to look good across a wide range of numBuckets.
function computeBucketWidthVpx(numBuckets) {
  return 40 - 38/numBuckets;
}

// Top-level entry point to render all the detailed histograms found in a JSON
// stats response.
function renderHistograms(histogramDiv, data) {
  histogramDiv.replaceChildren();
  for (stat of data.stats) {
    const histograms = stat.histograms;
    if (histograms) {
      if (histograms.supported_percentiles && histograms.details) {
        renderHistogramDetail(histogramDiv, histograms.supported_percentiles, histograms.details);
      }
    }
  }
}

// formats a number using up to 2 decimal places. See
// https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Intl/NumberFormat/NumberFormat
let format = Intl.NumberFormat('en', {
  notation: 'compact',
  maximumFractionDigits: 2
}).format;

// Formats a percentage using up to 2 decimal places. These are
// used for writing CSS percents.
let formatPercent = Intl.NumberFormat('en', {
  style: 'percent',
  maximumFractionDigits: 2
}).format;

// Formats a range for a histogram bucket, using inclusive/beginning and
// exclusive end, computed from the width.
function formatRange(lower_bound, width) {
  return '[' + format(lower_bound) + ', ' + format(lower_bound + width) + ')';
}

// Renders an array histograms.
function renderHistogramDetail(histogramDiv, supported_percentiles, details) {
  for (histogram of details) {
    renderHistogram(histogramDiv, supported_percentiles, histogram, null);
  }
}

// Generates a function to render histogram information, capturing the
// variables passed in. This is used to establish a mouse-enter list on
// the histogram bucket, but without pulling in the entire drawing context.
function showPopupFn(detailPopup, bucketPosPercent, bucketOnLeftSide, bucket, bucketSpan,
                     showingCount) {
  return (event) => {
    if (globalState.pendingTimeout) {
      window.clearTimeout(globalState.pendingTimeout);
      globalState.pendingLeave();
    }

    if (bucketOnLeftSide) {
      detailPopup.style.left = bucketPosPercent;
      detailPopup.style.right = '';
    } else {
      detailPopup.style.left = '';
      detailPopup.style.right = bucketPosPercent;
    }
    detailPopup.style.visibility = 'visible';
    bucketSpan.style.backgroundColor = 'yellow';
    highlightedBucket = bucketSpan;

    detailPopup.replaceChildren();
    makeElement(detailPopup, 'div').textContent = formatRange(bucket.lower_bound, bucket.width);
    if (!showingCount) {
      makeElement(detailPopup, 'div').textContent = 'count=' + bucket.count;
    }
    if (bucket.annotations) {
      for (annotation of bucket.annotations) {
        const span = makeElement(detailPopup, 'div');
        span.textContent = annotation.detail();
      }
    }
  };
}

// Generates a timeout function for the provided popup div.
function timeoutFn(detailPopup) {
  return (event) => {
    globalState.pendingLeave = leaveHandlerFn(detailPopup);
    globalState.pendingTimeout = window.setTimeout(globalState.pendingLeave, 2000);
  };
}

// Generates a handler function to be called when the mouse leaves
// a popup.
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

function leavePopup() {
  globalState.pendingTimeout = window.setTimeout(globalState.pendingLeave, 2000);
}

function makeElement(parent, type, className) {
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
 */
function assignPercentilesAndIntervalsToBuckets(histogram, supported_percentiles) {
  let maxCount = 0;
  let percentileIndex = 0;
  let intervalIndex = 0;
  const percentile_values = histogram.percentiles;
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
    for (; percentileIndex < percentile_values.length; ++percentileIndex) {
      const percentileValue = percentile_values[percentileIndex].cumulative;
      if (nextBucket && percentileValue >= nextBucket.lower_bound) {
        break; // do not increment index; re-consider percentile for next bucket.
      }
      bucket.annotations.push(new Percentile(
          percentileValue, supported_percentiles[percentileIndex]));
    }

    for (; intervalIndex < histogram.intervals.length; ++intervalIndex) {
      const interval = histogram.intervals[intervalIndex];
      if (nextBucket && interval.lower_bound >= nextBucket.lower_bound) {
        break; // do not increment index; re-consider interval for next bucket.
      }
      bucket.annotations.push(new Interval(interval.lower_bound, interval.width, interval.count));
      //console.log(histogram.name + ': adding interval to bucket lower_bound=' + bucket.lower_bound + ' lb=' +
      //interval.lower_bound + ' width=' + interval.width + ' count=' + interval.count);

      // It's unlikely that an interval bucket value will increase the overall maxCount
      // but just to be sure we'll scan through them.
      maxCount = Math.max(maxCount, interval.count);
    }

    if (bucket.annotations.length > 0) {
      bucket.annotations.sort((a, b) => a.value < b.value);
    }
  }
  return maxCount;
}

class Annotation {
  constructor(value) {
    this.value = value;
  }

  cssClass() { throw new Error('pure virtual function cssClass'); }
  toString() { throw new Error('pure virtual function toString'); }
  detail() { throw new Error('pure virtual function detail'); }
}

class Percentile extends Annotation {
  constructor(value, percentile) {
    super(value);
    this.percentile = percentile;
  }

  cssClass() { return 'histogram-percentile'; }
  toString() { return 'P' + this.percentile; }
  detail() { return 'P' + this.percentile + ': ' + format(this.value); }
}

class Interval extends Annotation {
  constructor(value, width, count) {
    super(value)
    this.width = width;
    this.count = count;
  }

  cssClass() { return 'histogram-interval'; }
  toString() { return 'i:' + this.count; }
  detail() {
    return 'Interval ' + formatRange(this.value, this.width) + ': ' + this.count;
  }
}

// Captures context needed to lay out the histogram graphically.
class Painter {
  constructor(div, numBuckets, maxCount) {
    this.numBuckets = numBuckets;
    this.maxCount = maxCount;
    this.leftVpx = constants.marginWidthVpx;
    this.bucketWidthVpx = computeBucketWidthVpx(numBuckets);
    this.widthVpx = (numBuckets * this.bucketWidthVpx + (numBuckets + 1) * constants.marginWidthVpx)
        * (1 + 2*constants.outerMarginFraction);
    this.textIntervalIndex = 0;
    this.bucketWidthPercent = formatPercent(this.vpxToWidth(this.bucketWidthVpx));

    this.graphics = makeElement(div, 'div', 'histogram-graphics');
    this.labels = makeElement(div, 'div', 'histogram-labels');
    this.annotationsDiv = makeElement(div, 'div', 'histogram-annotations');

    // We have business logic to ensure only be one popup div is visible at a
    // time.  However, we need a separate popup div for each histogram
    // so that they can be positioned relative to the histogram's graphics.
    this.detailPopup = makeElement(this.graphics, 'div', 'histogram-popup');
    this.detailPopup.addEventListener('mouseover', enterPopup);
    this.detailPopup.addEventListener('mouseout', leavePopup);

    this.textInterval = Math.ceil(numBuckets / constants.maxBucketsWithText);
  }

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
    const span = makeElement(this.annotationsDiv, 'span', annotation.cssClass());
    let percentilePercent = formatPercent(this.vpxToPosition(percentileVpx));
    span.style.left = percentilePercent;

    // Don't draw textual labels for the percentiles and intervals if there are
    // more than one: they'll just get garbled. The user can over over the
    // bucket to see the detail.
    if (bucket.annotations.length == 1) {
      const percentilePLabel = makeElement(this.annotationsDiv, 'span', 'percentile-label');
      percentilePLabel.style.bottom = 0;
      percentilePLabel.textContent = annotation.toString();
      percentilePLabel.style.left = percentilePercent;

      const percentileVLabel = makeElement(this.annotationsDiv, 'span', 'percentile-label');
      percentileVLabel.style.bottom = '30%';
      percentileVLabel.textContent = format(annotation.value);
      percentileVLabel.style.left = percentilePercent;
    }
  }

  drawBucket(bucket, index) {
    this.leftPercent = formatPercent(this.vpxToPosition(this.leftVpx));

    const bucketSpan = makeElement(this.graphics, 'span', 'histogram-bucket');
    const heightPercent = this.maxCount == 0 ? 0 :
          formatPercent(constants.baseHeightFraction + (bucket.count / this.maxCount) *
                        (1 - constants.baseHeightFraction));
    bucketSpan.style.height = heightPercent;
    const upper_bound = bucket.lower_bound + bucket.width;

    bucketSpan.style.width = this.bucketWidthPercent;
    bucketSpan.style.left = this.leftPercent;

    let showingCount = false;
    if (++this.textIntervalIndex == this.textInterval) {
      showingCount = true;
      this.textIntervalIndex = 0;
      this.drawBucketLabels(bucket, heightPercent);
    }

    const bucketOnLeftSide = true; //index <= this.numBuckets / 2;
    const bucketPosVpx = bucketOnLeftSide ? this.leftVpx :
          (this.widthVpx - (this.leftVpx + 2*this.bucketWidthVpx));

    bucketSpan.addEventListener('mouseover', showPopupFn(
        this.detailPopup, formatPercent(this.vpxToPosition(bucketPosVpx)), bucketOnLeftSide,
        bucket, bucketSpan, showingCount));

    bucketSpan.addEventListener('mouseout', timeoutFn(this.detailPopup));

    this.leftVpx += this.bucketWidthVpx + constants.marginWidthVpx;
  }

  drawBucketLabels(bucket, heightPercent) {
    const widthPercent = this.bucketWidthPercent
    const lower_label = makeElement(this.labels, 'span');
    lower_label.textContent = format(bucket.lower_bound);
    lower_label.style.left = this.leftPercent;
    lower_label.style.width = widthPercent;

    const bucketLabel = makeElement(this.graphics, 'span', 'bucket-label');
    bucketLabel.textContent = format(bucket.count);
    bucketLabel.style.left = this.leftPercent;
    bucketLabel.style.width = widthPercent;
    bucketLabel.style.bottom = heightPercent;
  }

  vpxToPosition(vpx) {
    return this.vpxToWidth(vpx) + constants.outerMarginFraction;
  }

  vpxToWidth(vpx) {
    return vpx / this.widthVpx;
  }
}

function renderHistogram(histogramDiv, supported_percentiles, histogram, changeCount) {
  const div = makeElement(histogramDiv, 'div');
  const label = makeElement(div, 'span', 'histogram-name');
  label.textContent = histogram.name + (changeCount == null ? "" : " (" + changeCount + ")");

  const numBuckets = histogram.totals.length;
  if (numBuckets == 0) {
    makeElement(div, 'span', 'histogram-no-data').textContent = 'No recorded values';
    return;
  }

  const maxCount = assignPercentilesAndIntervalsToBuckets(histogram, supported_percentiles);

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
    painter.drawBucket(bucket, i);
  }
}
