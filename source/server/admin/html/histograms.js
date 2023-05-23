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

// We merge percentiles and intervals in the display.
const PERCENTILE = 0;
const INTERVAL = 1;

// https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Intl/NumberFormat/NumberFormat
let format = Intl.NumberFormat('en', {
  notation: 'compact',
  maximumFractionDigits: 2
}).format;

let formatPercent = Intl.NumberFormat('en', {
  style: 'percent',
  maximumFractionDigits: 2
}).format;

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
    const formatRange = (lower_bound, width) => '[' + format(lower_bound) + ', ' +
          format(lower_bound + width) + ')';
    makeElement(detailPopup, 'div').textContent = formatRange(bucket.lower_bound, bucket.width);
    if (!showingCount) {
      makeElement(detailPopup, 'div').textContent = 'count=' + bucket.count;
    }
    if (bucket.annotations) {
      for (annotation of bucket.annotations) {
        const span = makeElement(detailPopup, 'div');
        if (annotation[1] == PERCENTILE) {
          span.textContent = 'P' + annotation[2] + ': ' + format(annotation[0]);
        } else {
          console.log('popping up interval annotation ' + annotation);
          span.textContent = 'Interval ' + formatRange(annotation[0], annotation[2]) +
              ': ' + annotation[3];
        }
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

function renderHistogram(histogramDiv, supported_percentiles, histogram, changeCount) {
  const div = makeElement(histogramDiv, 'div');
  const label = makeElement(div, 'span', 'histogram-name');
  label.textContent = histogram.name + (changeCount == null ? "" : " (" + changeCount + ")");

  const numBuckets = histogram.totals.length;
  if (numBuckets == 0) {
    makeElement(div, 'span', 'histogram-no-data').textContent = 'No recorded values';
    return;
  }

  let i = 0;
  const percentile_values = histogram.percentiles;
  let minXValue = null;
  let maxXValue = null;
  const updateMinMaxValue = (min, max) => {
    if (minXValue == null) {
      minXValue = min;
      maxXValue = max;
    } else if (min < minXValue) {
      minXValue = min;
    } else if (max > maxXValue) {
      maxXValue = max;
    }
  };

  for (i = 0; i < supported_percentiles.length; ++i) {
    const cumulative = percentile_values[i].cumulative;
    updateMinMaxValue(cumulative, cumulative);
  }
  const graphics = makeElement(div, 'div', 'histogram-graphics');
  const labels = makeElement(div, 'div', 'histogram-labels');

  // We have business logic to ensure only be one popup div is visible at a
  // time.  However, we need a separate popup div for each histogram
  // so that they can be positioned relative to the histogram's graphics.
  const detailPopup = makeElement(graphics, 'div', 'histogram-popup');

  detailPopup.addEventListener('mouseover', enterPopup);
  detailPopup.addEventListener('mouseout', leavePopup);

  // First we can over the detailed bucket value and count info, and determine
  // some limits so we can scale the histogram data to (for now) consume
  // the desired width and height, which we'll express as percentages, so
  // the graphics stretch when the user stretches the window.
  let maxCount = 0;
  let percentileIndex = 0;
  let intervalIndex = 0;
  let prevBucket = null;
  let annotationsDiv;

  // If there are too many buckets, per-bucket label text gets too dense and the
  // text becomes illegible, so skip some if needed. We always put the range in
  // the popup, and when skipped, we'll put the count in the popup as well, in
  // addition to any percentiles or interval ranges.
  const maxBucketsWithText = 30;
  const textInterval = Math.ceil(numBuckets / maxBucketsWithText);
  let textIntervalIndex = 0;

  for (bucket of histogram.totals) {
    maxCount = Math.max(maxCount, bucket.count);
    const upper_bound = bucket.lower_bound + bucket.width;
    updateMinMaxValue(bucket.lower_bound, upper_bound);

    // Attach percentile records with values between the previous bucket and
    // this one. We will drop percentiles before the first bucket. Thus each
    // bucket starting with the second one will have a 'percentiles' property
    // with pairs of [percentile, value], which can then be used while
    // rendering the bucket.
    if (prevBucket != null) {
      bucket.annotations = [];
      for (; percentileIndex < percentile_values.length; ++percentileIndex) {
        const percentileValue = percentile_values[percentileIndex].cumulative;
        if (percentileValue >= upper_bound) {
          break; // do not increment index; re-consider percentile for next bucket.
        }
        bucket.annotations.push(
            [percentileValue, PERCENTILE, supported_percentiles[percentileIndex]]);
      }

      for (; intervalIndex < histogram.intervals.length; ++intervalIndex) {
        const interval = histogram.intervals[intervalIndex];
        //const interval_upper_bound = interval.lower_bound + interval.width;
        if (interval.lower_bound >= upper_bound) {
          break; // do not increment index; re-consider interval for next bucket.
        }
        bucket.annotations.push([interval.lower_bound, INTERVAL, interval.width, interval.count]);
        console.log(histogram.name + ': adding interval to bucket lower_bound=' + bucket.lower_bound + ' lb=' +
                    interval.lower_bound + ' width=' + interval.width + ' count=' + interval.count);
      }

      if (bucket.annotations.length > 0) {
        bucket.annotations.sort((a, b) => a[0] < b[0]);
        if (!annotationsDiv) {
          annotationsDiv = makeElement(div, 'div', 'histogram-percentiles');
        }
      }
    }
    prevBucket = bucket;
  }

  // It's unlikely that an interval bucket value will increase the overall maxCount
  // but just to be sure we'll scan through them.
  for (bucket of histogram.intervals) {
    maxCount = Math.max(maxCount, bucket.count);
  }

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
  const bucketWidthVpx = 40 - 38/numBuckets;
  const marginWidthVpx = 20;
  const outerMarginFraction = 0.01;
  const percentileWidthAndMarginVpx = 20;
  const widthVpx = (numBuckets * bucketWidthVpx + (numBuckets + 1) * marginWidthVpx)
        * (1 + 2*outerMarginFraction);

  const vpxToFractionPosition = vpx => vpx / widthVpx + outerMarginFraction;
  const toPercentPosition = vpx => formatPercent(vpxToFractionPosition(vpx));
  const vpxToFractionWidth = vpx => vpx / widthVpx;
  const toPercentWidth = vpx => formatPercent(vpxToFractionWidth(vpx));
  let leftVpx = marginWidthVpx;
  let prevVpx = leftVpx;
  for (i = 0; i < numBuckets; ++i) {
    const bucket = histogram.totals[i];

    if (annotationsDiv && bucket.annotations &&
        bucket.annotations.length > 0) {
      // Keep track of where we write each percentile so if the next one is very close,
      // we can minimize overlapping the text. This is not perfect as the JS is not
      // tracking how wide the text actually is.
      let prevPercentileLabelVpx = 0;

      for (annotation of bucket.annotations) {
        // Find the ideal place to draw the percentile bar, by linearly
        // interpolating between the current bucket and the previous bucket.
        // We know that the next bucket does not come into play because
        // the percentiles held underneath a bucket are based on a value that
        // is at most as large as the current bucket.
        let percentileVpx = leftVpx;
        const bucketDelta = bucket.lower_bound - prevBucket.lower_bound;
        if (bucketDelta > 0) {
          const weight = (bucket.lower_bound - annotation[0]) / bucketDelta;
          percentileVpx = weight * prevVpx + (1 - weight) * leftVpx;
        }

        // We always put the marker proportionally between this bucket and
        // the next one.
        const span = makeElement(annotationsDiv, 'span',
                                 (annotation[1] == PERCENTILE) ? 'histogram-percentile' :
                                 'histogram-interval');
        let percentilePercent = toPercentPosition(percentileVpx);
        span.style.left = percentilePercent;

        // We try to put the text there too unless it's too close to the previous one.
        if (percentileVpx < prevPercentileLabelVpx) {
          percentileVpx = prevPercentileLabelVpx;
          percentilePercent = toPercentPosition(percentileVpx);
        }
        prevPercentileLabelVpx = percentileVpx + percentileWidthAndMarginVpx;

        // Don't draw textual labels for the percentiles and intervals if there are
        // more than one: they'll just get garbled. The user can over over the
        // bucket to see the detail.
        if (bucket.annotations.length == 1) {
          const percentilePLabel = makeElement(annotationsDiv, 'span', 'percentile-label');
          percentilePLabel.style.bottom = 0;
          if (annotation[1] == PERCENTILE) {
            percentilePLabel.textContent = 'P' + annotation[2];
          } else {
            percentilePLabel.textContent = 'i:' + annotation[3] + '[' + annotation[2] + ']';
          }
          percentilePLabel.style.left = percentilePercent;

          const percentileVLabel = makeElement(annotationsDiv, 'span', 'percentile-label');
          percentileVLabel.style.bottom = '30%';
          percentileVLabel.textContent = format(annotation[0]);
          percentileVLabel.style.left = percentilePercent;
        }
      }
    }

    // Now draw the bucket.
    const bucketSpan = makeElement(graphics, 'span', 'histogram-bucket');
    const heightPercent = maxCount == 0 ? 0 : formatPercent(bucket.count / maxCount);
    bucketSpan.style.height = heightPercent;
    const upper_bound = bucket.lower_bound + bucket.width;
    const nextVpx = bucketWidthVpx + marginWidthVpx + leftVpx;

    bucketSpan.style.width = toPercentWidth(bucketWidthVpx);
    bucketSpan.style.left = toPercentPosition(leftVpx);

    let showingCount = false;
    if (++textIntervalIndex == textInterval) {
      showingCount = true;
      textIntervalIndex = 0;
      const lower_label = makeElement(labels, 'span');
      lower_label.textContent = format(bucket.lower_bound);
      lower_label.style.left = toPercentPosition(leftVpx);
      lower_label.style.width = toPercentWidth(bucketWidthVpx);

      const bucketLabel = makeElement(graphics, 'span', 'bucket-label');
      bucketLabel.textContent = format(bucket.count);
      bucketLabel.style.left = toPercentPosition(leftVpx);
      bucketLabel.style.width = toPercentWidth(bucketWidthVpx);
      bucketLabel.style.bottom = heightPercent;
    }

    const bucketOnLeftSide = i <= numBuckets / 2;
    const bucketPosVpx = bucketOnLeftSide ? leftVpx :
          (widthVpx - (leftVpx + 2*bucketWidthVpx));

    bucketSpan.addEventListener('mouseover', showPopupFn(
        detailPopup, toPercentPosition(bucketPosVpx), bucketOnLeftSide, bucket, bucketSpan,
        showingCount));

    bucketSpan.addEventListener('mouseout', timeoutFn(detailPopup));

    prevVpx = leftVpx;
    leftVpx = nextVpx;
    prevBucket = bucket;
  }
}
