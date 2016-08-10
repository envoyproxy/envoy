$(function() {
  var $stickyEl = $("[data-waypoint='sidebar']");
  var $stopEl   = $("[data-waypoint='footer']");
  var $verticalGutter = $stopEl.outerHeight();
  var $stickyElHeight = $stickyEl.outerHeight();

  $stickyEl.waypoint("sticky", {
    wrapper: "<aside class='sidebar-container' />"
  },

  function() {
    $("[data-waypoint='main']").toggleClass("stuck");
  });

  $stopEl.waypoint(function(direction) {
    if (direction == "down") {
      var footerOffset = $stopEl.offset();

      $stickyEl.css({
        position: "absolute",
        top: footerOffset.top - $stickyElHeight - $verticalGutter
      });
    } else if (direction == "up") {
      $stickyEl.attr("style", "");
    }
  }, {
    offset: function () {
      return $stickyElHeight;
    }
  });
});
