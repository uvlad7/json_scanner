# frozen_string_literal: true

require "mkmf"

# Makes all symbols private by default to avoid unintended conflict
# with other gems. To explicitly export symbols you can use RUBY_FUNC_EXPORTED
# selectively, or entirely remove this flag.
append_cflags("-fvisibility=hidden")

dir_config("yajl", "", "")

unless have_library("yajl") && have_header("yajl/yajl_parse.h") && have_header("yajl/yajl_gen.h")
  abort "yajl library not found"
end

create_makefile("json_scanner/json_scanner")
