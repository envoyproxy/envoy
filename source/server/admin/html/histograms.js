const marginWidthVpx = 20;
const computeBucketWidthVpx = numBuckets => 40 - 38/numBuckets;
const outerMarginFraction = 0.01;
const baseHeightFraction = 0.03;

// If there are too many buckets, per-bucket label text gets too dense and the
// text becomes illegible, so skip some if needed. We always put the range in
// the popup, and when skipped, we'll put the count in the popup as well, in
// addition to any percentiles or interval ranges.
const maxBucketsWithText = 30;

function renderHistograms(histogramDiv, data) {
  histogramDiv.replaceChildren();
  for (stat of data.stats) {
    const histograms = stat.histograms;
    if (histograms) {
      if (histograms.supported_percentiles && histograms.details) {
        renderHistogramDetail(histogramDiv, histograms.supported_percentiles, histograms.details);
      }
      continue;
    }
  }
}

const log_10 = Math.log(10);
function log10(num) {
  return Math.log(num) / log_10;
}

// https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Intl/NumberFormat/NumberFormat
let format = Intl.NumberFormat('en', {
  notation: 'compact',
  maximumFractionDigits: 2
}).format;

let formatPercent = Intl.NumberFormat('en', {
  style: 'percent',
  maximumFractionDigits: 2
}).format;

const formatRange = (lower_bound, width) => '[' + format(lower_bound) + ', ' +
      format(lower_bound + width) + ')';

function renderHistogramDetail(histogramDiv, supported_percentiles, details) {
  for (histogram of details) {
    renderHistogram(histogramDiv, supported_percentiles, histogram, null);
  }
}

let pendingLeave;
let pendingTimeout;
let highlightedBucket;

function showPopupFn(detailPopup, bucketPosPercent, bucketOnLeftSide, bucket, bucketSpan,
                    showingCount) {
  return () => {
    if (pendingTimeout) {
      window.clearTimeout(pendingTimeout);
      pendingLeave();
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

function timeoutFn(detailPopup) {
  return (event) => {
    pendingLeave = leaveHandlerFn(detailPopup);
    pendingTimeout = window.setTimeout(pendingLeave, 2000);
  };
}

function leaveHandlerFn(detailPopup) {
  return () => {
    pendingTimeout = null;
    pendingLeave = null;
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
  if (pendingTimeout) {
    window.clearTimeout(pendingTimeout);
    pendingTimeout = null;
    // The 'leave' handler needs to remain -- we'll call that 2 seconds
    // after the mouse leaves the popup.
  }
}

function leavePopup() {
  pendingTimeout = window.setTimeout(pendingLeave, 2000);
}

function makeElement(parent, type, className) {
  const element = document.createElement(type);
  if (className) {
    element.className = className;
  }
  parent.appendChild(element);
  return element;
}

function assignPercentilesAndIntervalsToBuckets(histogram, supported_percentiles) {
  let maxCount = 0;
  let prevBucket = null;
  let percentileIndex = 0;
  let intervalIndex = 0;
  const percentile_values = histogram.percentiles;

  for (bucket of histogram.totals) {
    maxCount = Math.max(maxCount, bucket.count);
    const upper_bound = bucket.lower_bound + bucket.width;
    bucket.annotations = [];

    // Attach percentile records with values between the previous bucket and
    // this one. We will drop percentiles before the first bucket. Thus each
    // bucket starting with the second one will have a 'percentiles' property
    // with pairs of [percentile, value], which can then be used while
    // rendering the bucket.
    if (prevBucket != null) {
      for (; percentileIndex < percentile_values.length; ++percentileIndex) {
        const percentileValue = percentile_values[percentileIndex].cumulative;
        if (percentileValue >= upper_bound) {
          break; // do not increment index; re-consider percentile for next bucket.
        }
        bucket.annotations.push(new Percentile(
            percentileValue, supported_percentiles[percentileIndex]));
      }

      for (; intervalIndex < histogram.intervals.length; ++intervalIndex) {
        const interval = histogram.intervals[intervalIndex];
        //const interval_upper_bound = interval.lower_bound + interval.width;
        if (interval.lower_bound >= upper_bound) {
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
    prevBucket = bucket;
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
    this.leftVpx = marginWidthVpx;
    this.prevVpx = this.leftVpx;
    this.prevBucket = null;
    this.bucketWidthVpx = computeBucketWidthVpx(numBuckets);
    this.widthVpx = (numBuckets * this.bucketWidthVpx + (numBuckets + 1) * marginWidthVpx)
        * (1 + 2*outerMarginFraction);
    this.textIntervalIndex = 0;
    this.bucketWidthPercent = formatPercent(this.vpxToWidth(this.bucketWidthVpx));

    this.graphics = makeElement(div, 'div', 'histogram-graphics');
    this.labels = makeElement(div, 'div', 'histogram-labels');
    this.annotationsDiv = makeElement(div, 'div', 'histogram-percentiles');

    // We have business logic to ensure only be one popup div is visible at a
    // time.  However, we need a separate popup div for each histogram
    // so that they can be positioned relative to the histogram's graphics.
    this.detailPopup = makeElement(this.graphics, 'div', 'histogram-popup');
    this.detailPopup.addEventListener('mouseover', enterPopup);
    this.detailPopup.addEventListener('mouseout', leavePopup);

    this.textInterval = Math.ceil(numBuckets / maxBucketsWithText);
  }

  drawAnnotation(bucket, annotation) {
    if (!this.prevBucket) {
      alert('unexpected call of annotation from first bucket');
    }

    // Find the ideal place to draw the percentile bar, by linearly
    // interpolating between the current bucket and the previous bucket.
    // We know that the next bucket does not come into play because
    // the percentiles held underneath a bucket are based on a value that
    // is at most as large as the current bucket.
    let percentileVpx = this.leftVpx;
    const bucketDelta = bucket.lower_bound - this.prevBucket.lower_bound;
    if (bucketDelta > 0) {
      const weight = (bucket.lower_bound - annotation.value) / bucketDelta;
      percentileVpx = weight * this.prevVpx + (1 - weight) * this.leftVpx;
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
          formatPercent(baseHeightFraction + (bucket.count / this.maxCount) *
                        (1 - baseHeightFraction));
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

    const bucketOnLeftSide = index <= this.numBuckets / 2;
    const bucketPosVpx = bucketOnLeftSide ? this.leftVpx :
          (this.widthVpx - (this.leftVpx + 2*this.bucketWidthVpx));

    bucketSpan.addEventListener('mouseover', showPopupFn(
        this.detailPopup, formatPercent(this.vpxToPosition(bucketPosVpx)), bucketOnLeftSide,
        bucket, bucketSpan, showingCount));

    bucketSpan.addEventListener('mouseout', timeoutFn(this.detailPopup));

    this.prevVpx = this.leftVpx;
    this.leftVpx += this.bucketWidthVpx + marginWidthVpx;

    // First we can over the detailed bucket value and count info, and determine
    // some limits so we can scale the histogram data to (for now) consume
    // the desired width and height, which we'll express as percentages, so
    // the graphics stretch when the user stretches the window.
    this.prevBucket = bucket;
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
    return this.vpxToWidth(vpx) + outerMarginFraction;
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
    for (annotation of bucket.annotations) {
      painter.drawAnnotation(bucket, annotation);
    }
    painter.drawBucket(bucket, i);
  }
}
