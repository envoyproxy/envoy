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

function renderHistogram(histogramDiv, supported_percentiles, histogram, changeCount) {
  const div = document.createElement('div');
  const label = document.createElement('span');
  label.className = 'histogram-name';
  label.textContent = histogram.name + (changeCount == null ? "" : " (" + changeCount + ")");
  div.appendChild(label);
  histogramDiv.appendChild(div);

  const numBuckets = histogram.totals.length;
  if (numBuckets == 0) {
    const no_data = document.createElement('span');
    no_data.className = 'histogram-no-data';
    no_data.textContent = 'No recorded values';
    div.appendChild(no_data);
    return;
  }

  let i = 0;
  const percentile_values = histogram.percentiles;
  let minValue = null;
  let maxValue = null;
  const updateMinMaxValue = (value) => {
    if (minValue == null) {
      minValue = value;
      maxValue = value;
    } else if (value < minValue) {
      minValue = value;
    } else if (value > minValue) {
      maxValue = value;
    }
  };

  for (i = 0; i < supported_percentiles.length; ++i) {
    updateMinMaxValue(percentile_values[i].cumulative);
  }
  const graphics = document.createElement('div');
  const labels = document.createElement('div');
  div.appendChild(label);
  div.appendChild(graphics);
  div.appendChild(labels);

  graphics.className = 'histogram-graphics';
  labels.className = 'histogram-labels';

  // First we can over the detailed bucket value and count info, and determine
  // some limits so we can scale the histogram data to (for now) consume
  // the desired width and height, which we'll express as percentages, so
  // the graphics stretch when the user stretches the window.
  let maxCount = 0;
  let percentileIndex = 0;
  let intervalIndex = 0;
  let prevBucket = null;
  let annotationsDiv;

  for (bucket of histogram.totals) {
    maxCount = Math.max(maxCount, bucket.count);
    updateMinMaxValue(bucket.value);

    // Attach percentile records with values between the previous bucket and
    // this one. We will drop percentiles before the first bucket. Thus each
    // bucket starting with the second one will have a 'percentiles' property
    // with pairs of [percentile, value], which can then be used while
    // rendering the bucket.
    if (prevBucket != null) {
      bucket.annotations = [];
      for (; percentileIndex < percentile_values.length; ++percentileIndex) {
        const percentileValue = percentile_values[percentileIndex].cumulative;
        if (percentileValue > bucket.value) {
          break; // do not increment index; re-consider percentile for next bucket.
        }
        bucket.annotations.push(
            [percentileValue, supported_percentiles[percentileIndex], PERCENTILE]);
      }

      for (; intervalIndex < histogram.intervals.length; ++intervalIndex) {
        const interval = histogram.intervals[intervalIndex];
        if (interval.value > bucket.value) {
          break; // do not increment index; re-consider interval for next bucket.
        }
        bucket.annotations.push([interval.value, interval.count, INTERVAL]);
      }

      if (bucket.annotations.length > 0) {
        bucket.annotations.sort((a, b) => a[0] < b[0]);
        if (!annotationsDiv) {
          annotationsDiv = document.createElement('div');
          div.appendChild(annotationsDiv);
          annotationsDiv.className = 'histogram-percentiles';
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

  const height = maxValue - minValue;
  const scaledValue = value => 9*((value - minValue) / height) + 1; // between 1 and 10;
  const valueToPercent = value => Math.round(80 * log10(scaledValue(value)));

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
  const bucketWidthVpx = 20;
  const marginWidthVpx = 40;
  const percentileWidthAndMarginVpx = 20;
  const widthVpx = numBuckets * (bucketWidthVpx + marginWidthVpx);

  const toPercent = vpx => formatPercent(vpx / widthVpx);

  let leftVpx = marginWidthVpx / 2;
  let prevVpx = 0;
  for (i = 0; i < histogram.totals.length; ++i) {
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
        const prevBucket = histogram.totals[i - 1];
        const bucketDelta = bucket.value - prevBucket.value;
        if (bucketDelta > 0) {
          const weight = (bucket.value - annotation[0]) / bucketDelta;
          percentileVpx = weight * prevVpx + (1 - weight) * leftVpx;
        }

        // We always put the marker proportionally between this bucket and
        // the next one.
        const span = document.createElement('span');
        span.className = (annotation[2] == PERCENTILE) ? 'histogram-percentile' :
            'histogram-interval';
        let percentilePercent = toPercent(percentileVpx);
        span.style.left = percentilePercent;

        // We try to put the text there too unless it's too close to the previous one.
        if (percentileVpx < prevPercentileLabelVpx) {
          percentileVpx = prevPercentileLabelVpx;
          percentilePercent = toPercent(percentileVpx);
        }
        prevPercentileLabelVpx = percentileVpx + percentileWidthAndMarginVpx;

        const percentilePLabel = document.createElement('span');
        percentilePLabel.className = 'percentile-label';
        percentilePLabel.style.bottom = 0;
        if (annotation[2] == PERCENTILE) {
          percentilePLabel.textContent = 'P' + annotation[1];
        } else {
          percentilePLabel.textContent = 'i:' + annotation[1];
        }
        percentilePLabel.style.left = percentilePercent; // percentileLeft;

        const percentileVLabel = document.createElement('span');
        percentileVLabel.className = 'percentile-label';
        percentileVLabel.style.bottom = '30%';
        percentileVLabel.textContent = format(annotation[0]);
        percentileVLabel.style.left = percentilePercent; // percentileLeft;

        annotationsDiv.appendChild(span);
        annotationsDiv.appendChild(percentilePLabel);
        annotationsDiv.appendChild(percentileVLabel);
      }
    }

    // Now draw the bucket.
    const bucketSpan = document.createElement('span');
    bucketSpan.className = 'histogram-bucket';
    const heightPercent = maxCount == 0 ? 0 : formatPercent(bucket.count / maxCount);
    bucketSpan.style.height = heightPercent;
    const left = toPercent(leftVpx);
    bucketSpan.style.left = left;
    const label = document.createElement('span');
    label.textContent = format(bucket.value);
    label.style.left = left;

    const bucketLabel = document.createElement('span');
    bucketLabel.className = 'bucket-label';
    bucketLabel.textContent = format(bucket.count);
    bucketLabel.style.left = left;
    bucketLabel.style.bottom = heightPercent;

    graphics.appendChild(bucketSpan);
    graphics.appendChild(bucketLabel);
    labels.appendChild(label);
    prevVpx = leftVpx;
    leftVpx += bucketWidthVpx + marginWidthVpx;;
  }
}
