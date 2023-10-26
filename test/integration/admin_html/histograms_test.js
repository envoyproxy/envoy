/**
 * Makes a histogram with the specified values so we can test that all the
 * features of this are rendered graphically.
 *
 * @param {!Array<!Object>} totals pre-populated json 'totals' field.
 * @return {!Object} json stats object containing one histogram.
 */
function makeHistogramJson(totals) {
  return {'stats': [{
    'histograms': {
      'supported_percentiles': [0, 25, 50, 75, 90, 95, 99, 99.5, 99.9, 100],
      'details': [{
        'name': 'h1',
        'percentiles': [
          {'cumulative': 200, 'interval': 200},
          {'cumulative': 207.5, 'interval': 207.5},
          {'cumulative': 302.5, 'interval': 302.5},
          {'cumulative': 306.25, 'interval': 306.25},
          {'cumulative': 308.5, 'interval': 308.5},
          {'cumulative': 309.25, 'interval': 309.25},
          {'cumulative': 309.85, 'interval': 309.85},
          {'cumulative': 309.925, 'interval': 309.925},
          {'cumulative': 309.985, 'interval': 309.985},
          {'cumulative': 310, 'interval': 310},
        ],
        'totals': totals,
        'intervals': [
          {'lower_bound': 200, 'width': 10, 'count': 1},
          {'lower_bound': 300, 'width': 10, 'count': 2}]}]}}]};
}

/**
 * Tests the rendering of histograms.
 *
 * @param {!Element} iframe the iframe we can use for rendering.
 */
async function testRenderHistogram(iframe) {
  const idoc = iframe.contentWindow.document;
  renderHistograms(idoc.body, makeHistogramJson([
    {'lower_bound': 200, 'width': 10, 'count': 1},
    {'lower_bound': 300, 'width': 10, 'count': 2}]));
  const buckets = idoc.getElementsByClassName('histogram-bucket');
  assertEq(2, buckets.length);

  // The first bucket is to the left of the second bucket;
  assertLt(parseFloat(buckets[0].style.left), parseFloat(buckets[1].style.left));

  // The first bucket has a height between 25% and 75%.
  assertLt(25, parseFloat(buckets[0].style.height));
  assertLt(parseFloat(buckets[0].style.height), 75);

  // The second bucket as a 100% height.
  assertEq('100%', buckets[1].style.height);

  // There is one popup div and it is not visible initially.
  const popups = idoc.getElementsByClassName('histogram-popup');
  assertEq(1, popups.length);
  const popup = popups[0];
  assertEq('hidden', getComputedStyle(popup).visibility);

  // When the mouse enters the first bucket it renders a visible popup with
  // associated annotations.
  buckets[0].dispatchEvent(new Event('mouseenter'));
  assertEq('visible', getComputedStyle(popup).visibility);
  assertEq(5, popup.children.length);
  assertEq('[200, 210)', popup.children[0].textContent);
  assertEq('count=1', popup.children[1].textContent);
  assertEq('P0: 200', popup.children[2].textContent);
  assertEq('Interval [200, 210): 1', popup.children[3].textContent);
  assertEq('P25: 207.5', popup.children[4].textContent);

  // 2 seconds after the mouse leaves, that area, the popup will be made invisible.
  buckets[0].dispatchEvent(new Event('mouseleave'));
  await asyncTimeout(3000);
  assertEq('hidden', getComputedStyle(popup).visibility);

  // Now enter the other bucket. Just check the 1st 2 of 10 buckets.
  buckets[1].dispatchEvent(new Event('mouseenter'));
  assertEq('visible', getComputedStyle(popup).visibility);
  assertEq(11, popup.children.length);
  assertEq('[300, 310)', popup.children[0].textContent);
  assertEq('count=2', popup.children[1].textContent);
  assertEq('Interval [300, 310): 2', popup.children[2].textContent);
  buckets[1].dispatchEvent(new Event('mouseleave'));

  // Re-enter the first bucket. The popup will immediately move to that with no delay.
  buckets[0].dispatchEvent(new Event('mouseenter'));
  assertEq('visible', getComputedStyle(popup).visibility);
  assertEq(5, popup.children.length);
  assertEq('[200, 210)', popup.children[0].textContent);
  buckets[1].dispatchEvent(new Event('mouseleave'));

  // There's exactly one annotations bucket.
  assertEq(1, idoc.getElementsByClassName('histogram-annotations').length);

  // There are 10 percentiles rendered each one to the right of the previous one.
  const percentiles = idoc.getElementsByClassName('histogram-percentile');
  assertEq(10, percentiles.length);
  let prevPercent = 0;
  for (percentile of percentiles) {
    const left = parseFloat(percentile.style.left);
    assertLt(prevPercent, left);
    prevPercent = left;
  }

  // There are 2 intervals rendered each one to the right of the previous one.
  const intervals = idoc.getElementsByClassName('histogram-interval');
  assertEq(2, intervals.length);
  assertLt(parseFloat(intervals[0].style.left), parseFloat(intervals[1].style.left));
}


/**
 * Renders a histogram, returning the number of text entries.
 *
 * @param {!Element} iframe the iframe we can use for rendering.
 * @param {number} numBuckets the number of histogram buckets to render.
 * @return {number} the number of text entries found.
 */
async function renderManyBucketsCountingLabels(iframe, numBuckets) {
  const idoc = iframe.contentWindow.document;
  idoc.body.replaceChildren();

  let totals = [];
  for (let i = 0; i < numBuckets; ++i) {
    totals.push({'lower_bound': i*100, 'width': 10, 'count': 1});
  }

  renderHistograms(idoc.body, makeHistogramJson(totals));
  await asyncTimeout(200);
  const labels = idoc.getElementsByClassName('histogram-labels')[0];
  return labels.children.length;
}


/**
 * Tests the rendering of histograms with a large number of buckets.
 *
 * @param {!Element} iframe the iframe we can use for rendering.
 */
async function testManyBuckets(iframe) {
  assertEq(15, await renderManyBucketsCountingLabels(iframe, 15));
  assertEq(16, await renderManyBucketsCountingLabels(iframe, 16));
  assertEq(17, await renderManyBucketsCountingLabels(iframe, 17));
  assertEq(18, await renderManyBucketsCountingLabels(iframe, 18));
  assertEq(19, await renderManyBucketsCountingLabels(iframe, 19));
  assertEq(20, await renderManyBucketsCountingLabels(iframe, 100));
  assertEq(20, await renderManyBucketsCountingLabels(iframe, 200));
  assertEq(19, await renderManyBucketsCountingLabels(iframe, 250));
  assertEq(20, await renderManyBucketsCountingLabels(iframe, 400));
  assertEq(20, await renderManyBucketsCountingLabels(iframe, 500));
  assertEq(20, await renderManyBucketsCountingLabels(iframe, 1000));
}

async function testDense(iframe) {
  const stats = {"stats":[{"histograms":{
    "details":[
      {"totals":[
        {"lower_bound":0,"count":472398,"width":0},
        {"width":0.1,"lower_bound":1,"count":557096},
        {"count":278689,"lower_bound":2,"width":0.1},
        {"count":47606,"lower_bound":3,"width":0.1},
        {"width":0.1,"lower_bound":4,"count":100633},
        {"lower_bound":5,"count":1573048,"width":0.1},
        {"lower_bound":6,"width":0.1,"count":8730442},
        {"width":0.1,"count":12997002,"lower_bound":7},
        {"count":10621890,"lower_bound":8,"width":0.1},
        {"count":8376248,"width":0.1,"lower_bound":9},
        {"lower_bound":10,"width":1,"count":4849572},
        {"count":2762795,"lower_bound":11,"width":1},
        {"width":1,"lower_bound":12,"count":1950010},
        {"lower_bound":13,"count":1627025,"width":1},
        {"count":1300345,"lower_bound":14,"width":1},
        {"width":1,"lower_bound":15,"count":1004985},
        {"width":1,"lower_bound":16,"count":775198},
        {"count":618252,"lower_bound":17,"width":1},
        {"width":1,"count":511487,"lower_bound":18},
        {"count":444584,"lower_bound":19,"width":1},
        {"count":404819,"lower_bound":20,"width":1},
        {"lower_bound":21,"width":1,"count":374287},
        {"count":338304,"lower_bound":22,"width":1},
        {"lower_bound":23,"width":1,"count":305750},
        {"lower_bound":24,"width":1,"count":299297},
        {"width":1,"count":334025,"lower_bound":25},
        {"width":1,"lower_bound":26,"count":415369},
        {"lower_bound":27,"count":541668,"width":1},
        {"width":1,"count":703182,"lower_bound":28},
        {"lower_bound":29,"width":1,"count":891888},
        {"lower_bound":30,"count":1106515,"width":1},
        {"count":1321004,"width":1,"lower_bound":31},
        {"count":1508785,"lower_bound":32,"width":1},
        {"lower_bound":33,"width":1,"count":1634176},
        {"lower_bound":34,"width":1,"count":1684453},
        {"lower_bound":35,"width":1,"count":1656202},
        {"width":1,"lower_bound":36,"count":1569391},
        {"width":1,"count":1448427,"lower_bound":37},
        {"count":1319784,"lower_bound":38,"width":1},
        {"count":1197179,"width":1,"lower_bound":39},
        {"lower_bound":40,"count":1086568,"width":1},
        {"lower_bound":41,"count":989413,"width":1},
        {"lower_bound":42,"width":1,"count":902388},
        {"lower_bound":43,"width":1,"count":823711},
        {"lower_bound":44,"width":1,"count":756634},
        {"lower_bound":45,"count":698663,"width":1},
        {"count":646086,"lower_bound":46,"width":1},
        {"width":1,"count":599957,"lower_bound":47},
        {"count":558024,"lower_bound":48,"width":1},
        {"count":519213,"width":1,"lower_bound":49},
        {"width":1,"lower_bound":50,"count":485635},
        {"width":1,"lower_bound":51,"count":464436},
        {"count":456691,"width":1,"lower_bound":52},
        {"count":467842,"lower_bound":53,"width":1},
        {"lower_bound":54,"width":1,"count":502266},
        {"count":557909,"width":1,"lower_bound":55},
        {"lower_bound":56,"width":1,"count":626889},
        {"count":707540,"width":1,"lower_bound":57},
        {"lower_bound":58,"width":1,"count":795978},
        {"lower_bound":59,"count":886280,"width":1},
        {"count":975926,"lower_bound":60,"width":1},
        {"count":1064726,"width":1,"lower_bound":61},
        {"lower_bound":62,"width":1,"count":1145957},
        {"lower_bound":63,"width":1,"count":1225469},
        {"count":1301871,"lower_bound":64,"width":1},
        {"lower_bound":65,"width":1,"count":1368685},
        {"width":1,"count":1429951,"lower_bound":66},
        {"width":1,"lower_bound":67,"count":1481437},
        {"width":1,"count":1531808,"lower_bound":68},
        {"width":1,"lower_bound":69,"count":1570730},
        {"width":1,"count":1612173,"lower_bound":70},
        {"width":1,"count":1644396,"lower_bound":71},
        {"width":1,"lower_bound":72,"count":1674847},
        {"count":1696110,"lower_bound":73,"width":1},
        {"count":1711967,"width":1,"lower_bound":74},
        {"count":1717062,"width":1,"lower_bound":75},
        {"count":1714020,"width":1,"lower_bound":76},
        {"width":1,"lower_bound":77,"count":1703362},
        {"lower_bound":78,"width":1,"count":1688992},
        {"count":1662752,"lower_bound":79,"width":1},
        {"count":1628202,"width":1,"lower_bound":80},
        {"count":1586145,"width":1,"lower_bound":81},
        {"lower_bound":82,"count":1539446,"width":1},
        {"width":1,"lower_bound":83,"count":1487187},
        {"lower_bound":84,"count":1436273,"width":1},
        {"width":1,"lower_bound":85,"count":1385643},
        {"lower_bound":86,"width":1,"count":1335497},
        {"count":1282595,"lower_bound":87,"width":1},
        {"width":1,"count":1235372,"lower_bound":88},
        {"width":1,"count":1189702,"lower_bound":89},
        {"lower_bound":90,"count":1143775,"width":1},
        {"count":1102952,"lower_bound":91,"width":1},
        {"count":1062247,"lower_bound":92,"width":1},
        {"count":1024297,"lower_bound":93,"width":1},
        {"lower_bound":94,"width":1,"count":992991},
        {"lower_bound":95,"width":1,"count":962374},
        {"count":936389,"width":1,"lower_bound":96},
        {"lower_bound":97,"count":913206,"width":1},
        {"count":893620,"lower_bound":98,"width":1},
        {"lower_bound":99,"width":1,"count":873168},
        {"lower_bound":100,"width":10,"count":7445829},
        {"count":5120274,"lower_bound":110.00000000000001,"width":10},
        {"lower_bound":120,"count":3689227,"width":10},
        {"width":10,"count":2942282,"lower_bound":130},
        {"lower_bound":140,"count":2459659,"width":10},
        {"width":10,"count":2063279,"lower_bound":150},
        {"width":10,"count":1863891,"lower_bound":160},
        {"width":10,"lower_bound":170,"count":1700979},
        {"count":1511092,"width":10,"lower_bound":180},
        {"lower_bound":190,"width":10,"count":1316476},
        {"count":1125849,"lower_bound":200,"width":10},
        {"width":10,"count":947157,"lower_bound":210},
        {"count":813968,"lower_bound":220.00000000000003,"width":10},
        {"lower_bound":229.99999999999997,"count":720115,"width":10},
        {"lower_bound":240,"count":637523,"width":10},
        {"width":10,"lower_bound":250,"count":564598},
        {"lower_bound":260,"count":494081,"width":10},
        {"width":10,"count":431764,"lower_bound":270},
        {"count":375778,"width":10,"lower_bound":280},
        {"count":325478,"width":10,"lower_bound":290},
        {"width":10,"lower_bound":300,"count":283433},
        {"count":245364,"lower_bound":310,"width":10},
        {"width":10,"count":215046,"lower_bound":320},
        {"lower_bound":330,"count":189997,"width":10},
        {"count":169817,"width":10,"lower_bound":340},
        {"lower_bound":350,"width":10,"count":152299},
        {"width":10,"count":135490,"lower_bound":360},
        {"count":121187,"width":10,"lower_bound":370},
        {"count":108488,"lower_bound":380,"width":10},
        {"lower_bound":390,"width":10,"count":97233},
        {"count":86670,"width":10,"lower_bound":400},
        {"width":10,"count":76717,"lower_bound":409.99999999999994},
        {"lower_bound":420,"width":10,"count":68278},
        {"count":60168,"lower_bound":430,"width":10},
        {"lower_bound":440.00000000000006,"width":10,"count":54734},
        {"width":10,"lower_bound":450,"count":49024},
        {"width":10,"lower_bound":459.99999999999994,"count":43996},
        {"width":10,"count":39318,"lower_bound":470},
        {"width":10,"lower_bound":480,"count":35407},
        {"width":10,"lower_bound":490.00000000000006,"count":32452},
        {"width":10,"lower_bound":500,"count":29409},
        {"width":10,"lower_bound":509.99999999999994,"count":26340},
        {"width":10,"lower_bound":520,"count":23798},
        {"width":10,"lower_bound":530,"count":21378},
        {"width":10,"count":19753,"lower_bound":540},
        {"count":17707,"width":10,"lower_bound":550},
        {"count":16130,"width":10,"lower_bound":560},
        {"lower_bound":570,"width":10,"count":14384},
        {"width":10,"lower_bound":580,"count":13241},
        {"width":10,"lower_bound":590,"count":11986},
        {"count":11013,"width":10,"lower_bound":600},
        {"width":10,"count":10058,"lower_bound":610},
        {"count":9116,"lower_bound":620,"width":10},
        {"width":10,"count":8389,"lower_bound":630},
        {"width":10,"count":7663,"lower_bound":640},
        {"lower_bound":650,"width":10,"count":7016},
        {"lower_bound":660,"count":6395,"width":10},
        {"lower_bound":670,"count":5871,"width":10},
        {"count":5394,"width":10,"lower_bound":680},
        {"count":4991,"lower_bound":690,"width":10},
        {"lower_bound":700,"width":10,"count":4446},
        {"lower_bound":710,"width":10,"count":4240},
        {"width":10,"lower_bound":720,"count":3740},
        {"count":3514,"lower_bound":730,"width":10},
        {"count":3308,"lower_bound":740,"width":10},
        {"count":3003,"width":10,"lower_bound":750},
        {"count":2715,"lower_bound":760,"width":10},
        {"width":10,"lower_bound":770,"count":2510},
        {"width":10,"lower_bound":780,"count":2354},
        {"lower_bound":790,"width":10,"count":2229},
        {"width":10,"count":1901,"lower_bound":800},
        {"width":10,"lower_bound":810,"count":1799},
        {"width":10,"count":1662,"lower_bound":819.99999999999989},
        {"width":10,"count":1543,"lower_bound":830.00000000000011},
        {"width":10,"lower_bound":840,"count":1503},
        {"width":10,"lower_bound":850,"count":1292},
        {"width":10,"lower_bound":860,"count":1221},
        {"width":10,"count":1205,"lower_bound":869.99999999999989},
        {"lower_bound":880.00000000000011,"width":10,"count":1034},
        {"count":944,"width":10,"lower_bound":890},
        {"lower_bound":900,"count":898,"width":10},
        {"lower_bound":910,"count":800,"width":10},
        {"count":776,"width":10,"lower_bound":919.99999999999989},
        {"width":10,"lower_bound":930.00000000000011,"count":705},
        {"width":10,"lower_bound":940,"count":692},
        {"lower_bound":950,"count":616,"width":10},
        {"lower_bound":960,"count":559,"width":10},
        {"width":10,"lower_bound":969.99999999999989,"count":531},
        {"lower_bound":980.00000000000011,"width":10,"count":505},
        {"lower_bound":990,"count":432,"width":10},
        {"count":3141,"width":100,"lower_bound":1000},
        {"width":100,"count":1510,"lower_bound":1100},
        {"lower_bound":1200,"width":100,"count":829},
        {"lower_bound":1300,"width":100,"count":424},
        {"count":234,"width":100,"lower_bound":1400},
        {"width":100,"lower_bound":1500,"count":137},
        {"count":86,"lower_bound":1600,"width":100},
        {"lower_bound":1700,"count":63,"width":100},
        {"lower_bound":1800,"width":100,"count":41},
        {"lower_bound":1900,"width":100,"count":19},
        {"count":20,"lower_bound":2000,"width":100},
        {"count":18,"lower_bound":2100,"width":100},
        {"lower_bound":2200,"width":100,"count":13},
        {"count":13,"lower_bound":2300,"width":100},
        {"lower_bound":2400,"count":10,"width":100},
        {"count":8,"lower_bound":2500,"width":100},
        {"count":5,"width":100,"lower_bound":2600},
        {"width":100,"lower_bound":2700,"count":4},
        {"width":100,"lower_bound":2900,"count":5},
        {"width":100,"count":6,"lower_bound":3000},
        {"count":2,"width":100,"lower_bound":3100},
        {"width":100,"count":1,"lower_bound":3200},
        {"lower_bound":3300,"count":2,"width":100},
        {"count":2,"width":100,"lower_bound":3400},
        {"count":1,"width":100,"lower_bound":3500},
        {"lower_bound":3600,"width":100,"count":2},
        {"width":100,"count":2,"lower_bound":3800},
        {"count":1,"width":100,"lower_bound":3900},
        {"count":2,"width":100,"lower_bound":4000},
        {"width":100,"lower_bound":4300,"count":1},
        {"count":1,"width":100,"lower_bound":4400},
        {"count":1,"lower_bound":4700,"width":100},
        {"width":100,"lower_bound":5300,"count":1},
        {"width":1000,"lower_bound":12000,"count":1},
        {"width":1000,"lower_bound":15000,"count":1}
      ],
       "percentiles":[
         {"cumulative":0,"interval":0},
         {"cumulative":10.52802494735618,"interval":11.74527027027027},
         {"cumulative":60.456552033658291,"interval":66.074074074074076},
         {"cumulative":92.45778241783691,"interval":100.59340074507716},
         {"cumulative":145.77243512210438,"interval":159.47899159663874},
         {"cumulative":198.23464765024195,"interval":216.33467741935479},
         {"interval":353.465384615384,"cumulative":329.90518865731076},
         {"interval":409.505000000001,"cumulative":394.83355033784852},
         {"interval":574.57428571428215,"cumulative":563.5139566026005},
         {"cumulative":16000,"interval":1200}],
       "intervals":[
         {"lower_bound":0,"count":105,"width":0},
         {"count":134,"width":0.1,"lower_bound":1},
         {"lower_bound":2,"width":0.1,"count":60},
         {"width":0.1,"count":19,"lower_bound":3},
         {"count":13,"width":0.1,"lower_bound":4},
         {"count":227,"width":0.1,"lower_bound":5},
         {"width":0.1,"count":1352,"lower_bound":6},
         {"lower_bound":7,"width":0.1,"count":2304},
         {"lower_bound":8,"width":0.1,"count":1977},
         {"lower_bound":9,"width":0.1,"count":1745},
         {"count":1212,"lower_bound":10,"width":1},
         {"lower_bound":11,"count":740,"width":1},
         {"width":1,"lower_bound":12,"count":538},
         {"count":403,"width":1,"lower_bound":13},
         {"width":1,"lower_bound":14,"count":278},
         {"width":1,"lower_bound":15,"count":232},
         {"width":1,"lower_bound":16,"count":149},
         {"lower_bound":17,"width":1,"count":153},
         {"count":121,"width":1,"lower_bound":18},
         {"count":129,"lower_bound":19,"width":1},
         {"lower_bound":20,"count":88,"width":1},
         {"width":1,"lower_bound":21,"count":91},
         {"count":68,"lower_bound":22,"width":1},
         {"count":71,"lower_bound":23,"width":1},
         {"width":1,"lower_bound":24,"count":68},
         {"width":1,"lower_bound":25,"count":59},
         {"count":70,"width":1,"lower_bound":26},
         {"lower_bound":27,"width":1,"count":93},
         {"count":106,"lower_bound":28,"width":1},
         {"count":144,"lower_bound":29,"width":1},
         {"lower_bound":30,"count":176,"width":1},
         {"lower_bound":31,"count":208,"width":1},
         {"width":1,"lower_bound":32,"count":231},
         {"width":1,"count":311,"lower_bound":33},
         {"lower_bound":34,"count":295,"width":1},
         {"lower_bound":35,"width":1,"count":347},
         {"count":313,"lower_bound":36,"width":1},
         {"lower_bound":37,"count":306,"width":1},
         {"width":1,"lower_bound":38,"count":254},
         {"width":1,"count":268,"lower_bound":39},
         {"count":265,"lower_bound":40,"width":1},
         {"lower_bound":41,"width":1,"count":221},
         {"width":1,"count":208,"lower_bound":42},
         {"lower_bound":43,"width":1,"count":182},
         {"lower_bound":44,"width":1,"count":180},
         {"width":1,"lower_bound":45,"count":185},
         {"width":1,"count":157,"lower_bound":46},
         {"width":1,"count":133,"lower_bound":47},
         {"width":1,"count":159,"lower_bound":48},
         {"width":1,"count":139,"lower_bound":49},
         {"lower_bound":50,"width":1,"count":121},
         {"count":99,"lower_bound":51,"width":1},
         {"lower_bound":52,"count":97,"width":1},
         {"width":1,"count":87,"lower_bound":53},
         {"lower_bound":54,"count":93,"width":1},
         {"width":1,"lower_bound":55,"count":92},
         {"count":98,"width":1,"lower_bound":56},
         {"lower_bound":57,"count":119,"width":1},
         {"width":1,"lower_bound":58,"count":128},
         {"lower_bound":59,"width":1,"count":118},
         {"lower_bound":60,"count":157,"width":1},
         {"lower_bound":61,"count":153,"width":1},
         {"lower_bound":62,"count":157,"width":1},
         {"width":1,"lower_bound":63,"count":171},
         {"count":193,"width":1,"lower_bound":64},
         {"width":1,"lower_bound":65,"count":213},
         {"lower_bound":66,"count":216,"width":1},
         {"count":253,"width":1,"lower_bound":67},
         {"width":1,"count":250,"lower_bound":68},
         {"count":241,"lower_bound":69,"width":1},
         {"lower_bound":70,"count":262,"width":1},
         {"width":1,"lower_bound":71,"count":280},
         {"count":305,"width":1,"lower_bound":72},
         {"count":297,"width":1,"lower_bound":73},
         {"width":1,"count":292,"lower_bound":74},
         {"count":331,"lower_bound":75,"width":1},
         {"count":330,"lower_bound":76,"width":1},
         {"lower_bound":77,"width":1,"count":316},
         {"width":1,"count":318,"lower_bound":78},
         {"lower_bound":79,"count":330,"width":1},
         {"lower_bound":80,"width":1,"count":300},
         {"lower_bound":81,"width":1,"count":329},
         {"width":1,"lower_bound":82,"count":331},
         {"width":1,"lower_bound":83,"count":331},
         {"lower_bound":84,"count":312,"width":1},
         {"width":1,"count":350,"lower_bound":85},
         {"count":308,"width":1,"lower_bound":86},
         {"count":287,"lower_bound":87,"width":1},
         {"lower_bound":88,"count":283,"width":1},
         {"lower_bound":89,"width":1,"count":275},
         {"width":1,"count":286,"lower_bound":90},
         {"width":1,"lower_bound":91,"count":296},
         {"lower_bound":92,"width":1,"count":280},
         {"lower_bound":93,"width":1,"count":235},
         {"count":259,"lower_bound":94,"width":1},
         {"width":1,"lower_bound":95,"count":216},
         {"lower_bound":96,"count":253,"width":1},
         {"count":213,"lower_bound":97,"width":1},
         {"count":232,"width":1,"lower_bound":98},
         {"lower_bound":99,"count":207,"width":1},
         {"count":1879,"width":10,"lower_bound":100},
         {"count":1400,"width":10,"lower_bound":110.00000000000001},
         {"width":10,"count":961,"lower_bound":120},
         {"lower_bound":130,"count":667,"width":10},
         {"width":10,"lower_bound":140,"count":573},
         {"width":10,"lower_bound":150,"count":476},
         {"lower_bound":160,"count":432,"width":10},
         {"count":377,"width":10,"lower_bound":170},
         {"count":349,"lower_bound":180,"width":10},
         {"lower_bound":190,"count":303,"width":10},
         {"lower_bound":200,"width":10,"count":297},
         {"count":248,"width":10,"lower_bound":210},
         {"width":10,"count":201,"lower_bound":220.00000000000003},
         {"width":10,"lower_bound":229.99999999999997,"count":189},
         {"count":174,"width":10,"lower_bound":240},
         {"lower_bound":250,"count":157,"width":10},
         {"width":10,"count":132,"lower_bound":260},
         {"width":10,"lower_bound":270,"count":112},
         {"lower_bound":280,"width":10,"count":97},
         {"lower_bound":290,"width":10,"count":80},
         {"width":10,"count":71,"lower_bound":300},
         {"lower_bound":310,"width":10,"count":56},
         {"lower_bound":320,"count":68,"width":10},
         {"width":10,"lower_bound":330,"count":55},
         {"lower_bound":340,"width":10,"count":51},
         {"width":10,"count":52,"lower_bound":350},
         {"width":10,"lower_bound":360,"count":47},
         {"width":10,"count":42,"lower_bound":370},
         {"lower_bound":380,"width":10,"count":33},
         {"width":10,"count":19,"lower_bound":390},
         {"lower_bound":400,"count":20,"width":10},
         {"width":10,"count":24,"lower_bound":409.99999999999994},
         {"lower_bound":420,"count":18,"width":10},
         {"width":10,"count":19,"lower_bound":430},
         {"lower_bound":440.00000000000006,"width":10,"count":12},
         {"width":10,"count":14,"lower_bound":450},
         {"lower_bound":459.99999999999994,"count":12,"width":10},
         {"count":12,"lower_bound":470,"width":10},
         {"lower_bound":480,"count":7,"width":10},
         {"lower_bound":490.00000000000006,"count":7,"width":10},
         {"width":10,"count":6,"lower_bound":500},
         {"count":4,"lower_bound":509.99999999999994,"width":10},
         {"width":10,"lower_bound":520,"count":2},
         {"width":10,"lower_bound":530,"count":4},
         {"lower_bound":540,"width":10,"count":4},
         {"width":10,"lower_bound":550,"count":3},
         {"lower_bound":560,"count":3,"width":10},
         {"lower_bound":570,"count":7,"width":10},
         {"lower_bound":580,"count":3,"width":10},
         {"count":1,"lower_bound":590,"width":10},
         {"width":10,"lower_bound":600,"count":1},
         {"width":10,"count":1,"lower_bound":610},
         {"count":2,"lower_bound":620,"width":10},
         {"width":10,"lower_bound":630,"count":4},
         {"width":10,"count":2,"lower_bound":640},
         {"lower_bound":650,"count":1,"width":10},
         {"width":10,"lower_bound":660,"count":1},
         {"width":10,"count":1,"lower_bound":670},
         {"width":10,"count":1,"lower_bound":680},
         {"width":10,"lower_bound":730,"count":2},
         {"width":10,"lower_bound":740,"count":1},
         {"lower_bound":750,"count":1,"width":10},
         {"count":1,"lower_bound":780,"width":10},
         {"count":1,"lower_bound":819.99999999999989,"width":10},
         {"width":10,"lower_bound":830.00000000000011,"count":2},
         {"lower_bound":840,"width":10,"count":1},
         {"lower_bound":880.00000000000011,"width":10,"count":2},
         {"count":1,"lower_bound":910,"width":10},
         {"count":1,"width":10,"lower_bound":930.00000000000011},
         {"width":10,"lower_bound":950,"count":1},
         {"width":10,"lower_bound":990,"count":1},
         {"lower_bound":1000,"count":1,"width":100},
         {"width":100,"count":1,"lower_bound":1100}],
       "name":"listener_manager.worker_0.dispatcher.loop_duration_us"}],
    "supported_percentiles":[0,25,50,75,90,95,99,99.5,99.9,100]}}]};

  const idoc = iframe.contentWindow.document;
  idoc.body.replaceChildren();

  renderHistograms(idoc.body, stats);
  await asyncTimeout(200);
  const labels = idoc.getElementsByClassName('histogram-labels')[0];
  assertEq(18, labels.children.length);
  const percentile_labels = idoc.getElementsByClassName('percentile-label');
  assertEq(34, percentile_labels.length);
}

addTest('?file=histograms_test.html', 'renderHistogram', testRenderHistogram);
addTest('?file=histograms_test.html', 'manyBuckets', testManyBuckets);
addTest('?file=histograms_test.html', 'testDense', testDense);
