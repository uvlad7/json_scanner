#include "json_scanner.h"

VALUE rb_mJsonScanner;

RUBY_FUNC_EXPORTED void
Init_json_scanner(void)
{
  rb_mJsonScanner = rb_define_module("JsonScanner");
}
