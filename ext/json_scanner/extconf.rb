# frozen_string_literal: true

require "mkmf"

# Makes all symbols private by default to avoid unintended conflict
# with other gems. To explicitly export symbols you can use RUBY_FUNC_EXPORTED
# selectively, or entirely remove this flag.
append_cflags("-fvisibility=hidden")

idefault, ldefault = if with_config("libyajl2-gem")
                       require "libyajl2"
                       [Libyajl2.include_path, Libyajl2.opt_path]
                     else
                       ["", ""]
                     end
dir_config("yajl", idefault, ldefault)

unless have_library("yajl") && have_header("yajl/yajl_parse.h") && have_header("yajl/yajl_gen.h")
  abort "yajl library not found"
end

create_makefile("json_scanner/json_scanner")
