mergeInto(LibraryManager.library, {
    proxy_get_configuration: function () {},
    proxy_log: function () {},
    proxy_get_current_time_nanoseconds: function() {},
    proxy_define_metric: function () {},
    proxy_increment_metric: function () {},
    proxy_record_metric: function () {},
    proxy_get_metric: function () {},
    proxy_done: function () {},
});
