#include "json_scanner.h"

VALUE rb_mJsonScanner;
VALUE rb_mJsonScannerOptions;

RUBY_FUNC_EXPORTED void
Init_json_scanner(void)
{
  rb_mJsonScanner = rb_define_module("JsonScanner");
  rb_mJsonScannerOptions = rb_define_module_under(rb_mJsonScanner, "Options");
  rb_define_const(rb_mJsonScannerOptions, "ALLOW_COMMENTS", INT2FIX(yajl_allow_comments));
  rb_define_const(rb_mJsonScannerOptions, "DONT_VALIDATE_STRINGS", INT2FIX(yajl_dont_validate_strings));
  rb_define_const(rb_mJsonScannerOptions, "ALLOW_TRAILING_GARBAGE", INT2FIX(yajl_allow_trailing_garbage));
  rb_define_const(rb_mJsonScannerOptions, "ALLOW_MULTIPLE_VALUES", INT2FIX(yajl_allow_multiple_values));
  rb_define_const(rb_mJsonScannerOptions, "ALLOW_PARTIAL_VALUES", INT2FIX(yajl_allow_partial_values));
}
