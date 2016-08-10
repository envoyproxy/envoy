$(function() {
  var chunkLevels, headerLevel, headers, jqShift, levels, nav, toToc, toc;

  headerLevel = function(header) {
    return parseInt(header.get(0).nodeName[1]);
  };
  jqShift = function(jq) {
    return [].shift.call(jq);
  };
  chunkLevels = function(headers) {
    var chunks, currentLevel, header, level, result;

    if (headers.length) {
      level = headerLevel(headers.first());
    }
    chunks = [];
    while (headers.length) {
      result = [jqShift(headers)];
      currentLevel = level + 1;
      while (headers.length) {
        header = headers.first();
        currentLevel = headerLevel(header);
        if (currentLevel > level) {
          result.push(jqShift(headers));
        } else {
          break;
        }
      }
      result = $(result);
      if (result.length > 1) {
        result = [$(jqShift(result)), chunkLevels(result)];
      } else {
        result = [result];
      }
      chunks.push(result);
    }
    return chunks;
  };
  toToc = function(levels) {
    var header, headers, li, ul, _i, _len;

    ul = $('<ul/>');
    for (_i = 0, _len = levels.length; _i < _len; _i++) {
      headers = levels[_i];
      header = headers[0];
      li = $('<li/>');
      li.append("<a href=\"#" + (header.attr('id')) + "\">" + (header.text()) + "</a>");
      if (headers.length > 1) {
        li.append(toToc(headers[1]));
      }
      ul.append(li);
    }
    return ul;
  };
  headers = $(':header[id^="toc_"]:not(h1)');
  levels = chunkLevels(headers);
  toc = toToc(levels);
  nav = $('<nav id="generated-toc"/>').append(toc);
  return $('#toc_0').after(nav);
});
