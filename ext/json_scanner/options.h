#ifndef JSON_SCANNER_OPTIONS_H
#define JSON_SCANNER_OPTIONS_H 1

typedef struct
{
  int with_path;
  int verbose_error;
  int allow_comments;
  int dont_validate_strings;
  int allow_trailing_garbage;
  int allow_multiple_values;
  int allow_partial_values;
  int symbolize_path_keys;
  int with_roots_info;
} scan_options;
#define SCAN_OPTION_VALUE_MASK 1
#define SCAN_OPTION_SET_MASK (1 << 1)
#define SCAN_OPTION(options, field) ((options)->field & SCAN_OPTION_VALUE_MASK)
#define SCAN_OPTION_IS_SET(options, field) ((options)->field & SCAN_OPTION_SET_MASK)
#define SCAN_OPTION_SET(options, field, value) ((options)->field = ((value) & SCAN_OPTION_VALUE_MASK) | SCAN_OPTION_SET_MASK)
#define SCAN_OPTION_FALSE(options, field) \
  (!SCAN_OPTION(options, field) && ((options)->field & SCAN_OPTION_SET_MASK))

static void scan_options_init(scan_options *options, VALUE kwargs)
{
  options->with_path = 0;
  options->verbose_error = 0;
  options->allow_comments = 0;
  options->dont_validate_strings = 0;
  options->allow_trailing_garbage = 0;
  options->allow_multiple_values = 0;
  options->allow_partial_values = 0;
  options->symbolize_path_keys = 0;
  options->with_roots_info = 0;
  if (kwargs != Qnil)
  {
    VALUE kwargs_values[SCAN_KWARGS_SIZE];
    rb_get_kwargs(kwargs, scan_kwargs_table, 0, SCAN_KWARGS_SIZE, kwargs_values);
    if (kwargs_values[0] != Qundef)
      SCAN_OPTION_SET(options, with_path, RTEST(kwargs_values[0]));
    if (kwargs_values[1] != Qundef)
      SCAN_OPTION_SET(options, verbose_error, RTEST(kwargs_values[1]));
    if (kwargs_values[2] != Qundef)
      SCAN_OPTION_SET(options, allow_comments, RTEST(kwargs_values[2]));
    if (kwargs_values[3] != Qundef)
      SCAN_OPTION_SET(options, dont_validate_strings, RTEST(kwargs_values[3]));
    if (kwargs_values[4] != Qundef)
      SCAN_OPTION_SET(options, allow_trailing_garbage, RTEST(kwargs_values[4]));
    if (kwargs_values[5] != Qundef)
      SCAN_OPTION_SET(options, allow_multiple_values, RTEST(kwargs_values[5]));
    if (kwargs_values[6] != Qundef)
      SCAN_OPTION_SET(options, allow_partial_values, RTEST(kwargs_values[6]));
    if (kwargs_values[7] != Qundef)
      SCAN_OPTION_SET(options, symbolize_path_keys, RTEST(kwargs_values[7]));
    if (kwargs_values[8] != Qundef)
      SCAN_OPTION_SET(options, with_roots_info, RTEST(kwargs_values[8]));
  }
}

static size_t options_size(const void *data)
{
  return sizeof(scan_options);
}

static const rb_data_type_t options_type = {
    .wrap_struct_name = "json_scanner_options",
    .function = {
        .dfree = RUBY_DEFAULT_FREE,
        .dsize = options_size,
    },
    .flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

static VALUE options_alloc(VALUE self)
{
  scan_options *options;
  return TypedData_Make_Struct(self, scan_options, &options_type, options);
}

static VALUE options_m_initialize(int argc, VALUE *argv, VALUE self)
{
  VALUE kwargs;
  scan_options *options;
  TypedData_Get_Struct(self, scan_options, &options_type, options);
#if RUBY_API_VERSION_MAJOR > 2 || (RUBY_API_VERSION_MAJOR == 2 && RUBY_API_VERSION_MINOR >= 7)
  rb_scan_args_kw(RB_SCAN_ARGS_LAST_HASH_KEYWORDS, argc, argv, "0:", &kwargs);
#else
  rb_scan_args(argc, argv, "0:", &kwargs);
#endif
  scan_options_init(options, kwargs);
  return self;
}

static VALUE options_m_inspect(VALUE self)
{
  VALUE res;
  scan_options *options;
  TypedData_Get_Struct(self, scan_options, &options_type, options);
  res = rb_sprintf("#<%" PRIsVALUE " {", rb_class_name(CLASS_OF(self)));
#define APPEND_OPTION_INSPECT(field) \
  if (SCAN_OPTION_IS_SET(options, field)) \
    rb_str_catf(res, #field ": %s, ", SCAN_OPTION(options, field) ? "true" : "false");
  APPEND_OPTION_INSPECT(with_path)
  APPEND_OPTION_INSPECT(verbose_error)
  APPEND_OPTION_INSPECT(allow_comments)
  APPEND_OPTION_INSPECT(dont_validate_strings)
  APPEND_OPTION_INSPECT(allow_trailing_garbage)
  APPEND_OPTION_INSPECT(allow_multiple_values)
  APPEND_OPTION_INSPECT(allow_partial_values)
  APPEND_OPTION_INSPECT(symbolize_path_keys)
  APPEND_OPTION_INSPECT(with_roots_info)
#undef APPEND_OPTION_INSPECT
  if (RSTRING_END(res)[-1] == ' ')
    rb_str_resize(res, RSTRING_LEN(res) - 2);
  rb_str_buf_cat_ascii(res, "}>");
  return res;
}

static void init_options(VALUE json_scanner_module)
{
  rb_cJsonScannerOptions = rb_define_class_under(json_scanner_module, "Options", rb_cObject);
  rb_define_alloc_func(rb_cJsonScannerOptions, options_alloc);
  rb_define_method(rb_cJsonScannerOptions, "initialize", options_m_initialize, -1);
  rb_define_method(rb_cJsonScannerOptions, "inspect", options_m_inspect, 0);
}

#endif /* JSON_SCANNER_OPTIONS_H */
