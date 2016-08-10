###
Sticky Elements Shortcut for jQuery Waypoints - v2.0.5
Copyright (c) 2011-2014 Caleb Troughton
Licensed under the MIT license.
https://github.com/imakewebthings/jquery-waypoints/blob/master/licenses.txt
###
((root, factory) ->
  if typeof define is 'function' and define.amd
    define ['jquery', 'waypoints'], factory
  else
    factory root.jQuery
) window, ($) ->

  # An extension of the waypoint defaults when calling the "sticky" method.

  # - wrapper: Each sticky element gets wrapped in another element. This
  #   element acts as the actual waypoint and stays in the document flow,
  #   leaving the sticky element to gain/lost fixed positioning freely without
  #   effecting layout. "wrapper" is the HTML for this element.

  # - stuckClass: The class that is added to the sticky element when the
  #   waypoint is reached. Users should style this class to add fixed
  #   positioning and whatever other styles are necessary for their
  #   particular design.
  defaults =
    wrapper: '<div class="sticky-wrapper" />'
    stuckClass: 'stuck'
    direction: 'down right'

  # Internal: Wraps the sticky elements in the sticky wrapper and returns the
  # wrapper elements.
  wrap = ($elements, options) ->
    $elements.wrap options.wrapper
    $parent = $elements.parent()
    $parent.data 'isWaypointStickyWrapper', true

  # .waypoint('sticky', [object])

  # The sticky method is a shortcut method for a common UI pattern, sticky
  # elements. In its most common form, this pattern consists of an item that
  # is part of the normal document flow until it reaches the top of the
  # viewport, where it gains a fixed position state.

  # This shortcut does very little to actually create the sticky state. It only
  # adds a class to the element when it reaches the appropriate part of the
  # viewport. It is the job of the user to define the styles for this "stuck"
  # state in CSS. There are many different ways one could style their sticky
  # elements, and trying to implement all of them in JS is futile. Everyone's
  # design is different.

  # This shortcut does take care of the most common pitfall in previous
  # versions of Waypoints: Using the sticky element as the waypoint. Fixed
  # position elements do not work well as waypoints since their position in
  # the document is constantly changing as the user scrolls (and their
  # position relative to the viewport never does, which is the whole point of
  # Waypoints). This shortcut will create a wrapper element around the sticky
  # element that acts as the actual waypoint, as well as a placeholder for the
  # waypoint in the document flow, as fixed positioning takes an element out
  # of flow and would otherwise effect the page layout. Users are recommended
  # to define any margins on their sticky elements as margins on this
  # wrapper instead.

  $.waypoints 'extendFn', 'sticky', (opt) ->
    options = $.extend {}, $.fn.waypoint.defaults, defaults, opt
    $wrap = wrap @, options
    originalHandler = options.handler
    options.handler = (direction) ->
      $sticky = $(@).children ':first'
      shouldBeStuck = options.direction.indexOf(direction) != -1
      $sticky.toggleClass options.stuckClass, shouldBeStuck
      $wrap.height if shouldBeStuck then $sticky.outerHeight() else ''
      originalHandler.call @, direction if originalHandler?
    $wrap.waypoint options
    @data 'stuckClass', options.stuckClass

  # .waypoint('unsticky')

  # Undoes everything done within the sticky shortcut by removing the parent
  # sticky wrapper, destroying the waypoint, and removing any stuck class
  # that may be applied.

  $.waypoints 'extendFn', 'unsticky', () ->
    $parent = @parent()
    return @ unless $parent.data('isWaypointStickyWrapper')
    $parent.waypoint 'destroy'
    @unwrap()
    @removeClass @data 'stuckClass'