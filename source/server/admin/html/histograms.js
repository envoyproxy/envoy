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

let layout_evenly = true;

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
    updateMinMaxValue(percentile_values[i].cumulative, percentile_values[i].cumulative);
  }
  const graphics = document.createElement('div');
  const labels = document.createElement('div');
  div.appendChild(label);
  div.appendChild(graphics);
  div.appendChild(labels);

  const detailPopup = document.createElement('div');
  detailPopup.className = 'histogram-popup';
  graphics.appendChild(detailPopup);

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

  // If there are too many buckets, per-bucket label text gets too dense, so skip
  // some if needed.
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
        if (percentileValue > upper_bound) {
          break; // do not increment index; re-consider percentile for next bucket.
        }
        bucket.annotations.push(
            [percentileValue, supported_percentiles[percentileIndex], PERCENTILE]);
      }

      for (; intervalIndex < histogram.intervals.length; ++intervalIndex) {
        const interval = histogram.intervals[intervalIndex];
        const interval_upper_bound = interval.lower_bound + interval.width;
        if (interval_upper_bound > upper_bound) {
          break; // do not increment index; re-consider interval for next bucket.
        }
        bucket.annotations.push([interval.lower_bound, interval.width, interval.count, INTERVAL]);
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

  const histogramTotalWidth = maxXValue - minXValue;
  const scaledXValue = xvalue => 9999*(xvalue / histogramTotalWidth) + 1; // between 1 and 10000;
  const xvalueToPercent = xvalue => {
    const log = log10(scaledXValue(xvalue)); // Between 0 and 4
    return 25*log;
  };

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
        const bucketDelta = bucket.lower_bound - prevBucket.lower_bound;
        if (bucketDelta > 0) {
          const weight = (bucket.lower_bound - annotation[0]) / bucketDelta;
          percentileVpx = weight * prevVpx + (1 - weight) * leftVpx;
        }

        // We always put the marker proportionally between this bucket and
        // the next one.
        const span = document.createElement('span');
        span.className = (annotation[3] == PERCENTILE) ? 'histogram-percentile' :
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
        if (annotation[3] == PERCENTILE) {
          percentilePLabel.textContent = 'P' + annotation[1];
        } else {
          percentilePLabel.textContent = 'i:' + annotation[2];
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
    const upper_bound = bucket.lower_bound + bucket.width;
    if (layout_evenly) {
      bucketSpan.style.left = left;
    } else {
      const leftPercent = xvalueToPercent(bucket.lower_bound - minXValue);
      bucketSpan.style.left = leftPercent + '%';
      const rightPercent = xvalueToPercent(upper_bound - minXValue);
      bucketSpan.style.width = (rightPercent - leftPercent) + '%';
    }
    console.log('lower_bound=' + bucket.lower_bound + ' width=' + bucket.width +
                ' left=' + bucketSpan.style.left /* + ' width=' + bucketSpan.style.width */);
    const nextVpx = bucketWidthVpx + marginWidthVpx + leftVpx;

/*
    // We will determine the width of the bucket proportionally based on deltas
    // between the previous and next buckets.
    scaleWidth = (previousBucket, prevBucketVpx, nextBucket, nextBucketVpx) => {
      const bucketDistance = nextBucket - previousBucket;
      const vpxDistance = nextBucketVpx - prevBucketVpx;
      const scaledVpx = bucket.width * vpxDistance / bucketDistance;
      const percent = toPercent(scaledVpx);
      console.log('prev=' + previousBucket + ',' + prevBucketVpx +
                  ' next= ' + nextBucket + ',' + nextBucketVpx +
                  ' bucketDist=' + bucketDistance + ' vpxDist=' + vpxDistance + ' scaled=' +
                  scaledVpx + ',' + percent);
      return percent;
    };
*/

    if (histogram.totals.length == 1) {
      // Special case one bucket.
      bucketSpan.style.width = '10%';
      bucketSpan.style.left = '45%';
    }/* else if (i == 0) {
      // We can only use the next bucket; there is no previous bucket.
      bucketSpan.style.width = scaleWidth(bucket.lower_bound, leftVpx,
                                          histogram.totals[i + 1].lower_bound, nextVpx);
    } else if (i == histogram.totals.length - 1) {
      // We can only use the previous bucket; there is no next bucket.
      bucketSpan.style.width = scaleWidth(prevBucket.lower_bound, prevVpx,
                                          bucket.lower_bound, leftVpx);

    } else {
      // We can use the both previous and next buckets.
      bucketSpan.style.width = scaleWidth(prevBucket.lower_bound, prevVpx,
                                          histogram.totals[i + 1].lower_bound, bucketSpan);
    }*/

    const magLeft = formatPercent((leftVpx - widthVpx/30) / widthVpx);
    bucketSpan.addEventListener('mouseover', (event) => {
      detailPopup.style.left = magLeft;
      detailPopup.style.visibility = 'visible';
      bucketSpan.style.backgroundColor = 'yellow';

/*
      bucketSpan.style.zIndex = 3;
      bucketSpan.style.width = '10%';
      bucketSpan.style.height = '60px';
      console.log('setting left position to ' + magLeft + ' from ' + left);
      bucketSpan.style.left = magLeft;
      bucketSpan.style.backgroundColor = 'cyan';
*/
      detailPopup.textContent =
          '[' + format(bucket.lower_bound) + ', ' + format(bucket.lower_bound + bucket.width) + ') '
          /* + 'count=' + bucket.count */;
    });

    bucketSpan.addEventListener('mouseout', (event) => {
      detailPopup.style.visibility = 'hidden';
      bucketSpan.style.backgroundColor = '#e6d7ff';

/*
      bucketSpan.style.zIndex = 1;
      bucketSpan.style.width = '2%';
      bucketSpan.style.left = left;
      bucketSpan.style.height = heightPercent;
      bucketSpan.textContent = '';
*/
    });

    graphics.appendChild(bucketSpan);

    if (++textIntervalIndex == textInterval) {
      textIntervalIndex = 0;
      const lower_label = document.createElement('span');
      //lower_label.textContent = format(bucket.lower_bound) + '[' + format(bucket.width) + ']';
      lower_label.textContent = format(bucket.lower_bound); // + ',' + format(bucket.width);


      //lower_label.style.left = toPercent(leftVpx - bucketWidthVpx/2);
      lower_label.style.left = left;
      lower_label.style.width = toPercent(bucketWidthVpx);
      /*
        const upper_label = document.createElement('span');
        upper_label.textContent = format(upper_bound);
        upper_label.style.left = toPercent(leftVpx + bucketWidthVpx/2);
      */
      labels.appendChild(lower_label);
    }

    const bucketLabel = document.createElement('span');
    bucketLabel.className = 'bucket-label';
    bucketLabel.textContent = format(bucket.count);
    bucketLabel.style.left = left;
    bucketLabel.style.bottom = heightPercent;
    graphics.appendChild(bucketLabel);

    //labels.appendChild(upper_label);
    prevVpx = leftVpx;
    leftVpx = nextVpx;
    prevBucket = bucket;
  }
}
