
local foo = { }

-- Called during the envoy_on_request Lua filter function.
function foo.do_something(request_handle)
  local headers = request_handle:headers()
  local token = headers:get("authorization")

  -- Check if auth header is present
  if token == nil then
    request_handle:respond({[":status"] = "401"}, "unable to find authorization header")
    return
  end

  request_handle:headers():add("lua-filter-header-example", "lua_was_here")
  request_handle:headers():remove("authorization")
end

return foo