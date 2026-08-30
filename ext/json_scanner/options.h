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
#define OPTIONS_FIELDS(X)              \
  X(with_path)                         \
  X(verbose_error)                     \
  X(allow_comments)                    \
  X(dont_validate_strings)             \
  X(allow_trailing_garbage)            \
  X(allow_multiple_values)             \
  X(allow_partial_values)              \
  X(symbolize_path_keys)               \
  X(with_roots_info)

static VALUE rb_cJsonScannerOptions;
enum
{
#define SCAN_KWARG_ENUM(field) SCAN_KWARG_##field,
  OPTIONS_FIELDS(SCAN_KWARG_ENUM)
#undef SCAN_KWARG_ENUM
  SCAN_KWARGS_SIZE,
};
static ID scan_kwargs_table[SCAN_KWARGS_SIZE];

static void scan_options_init(scan_options *options, VALUE kwargs)
{
  memset(options, 0, sizeof(*options));
  if (kwargs != Qnil)
  {
    VALUE kwargs_values[SCAN_KWARGS_SIZE];
    rb_get_kwargs(kwargs, scan_kwargs_table, 0, SCAN_KWARGS_SIZE, kwargs_values);
#define SCAN_OPTION_SET_KWARG(field) \
  if (kwargs_values[SCAN_KWARG_##field] != Qundef) \
    SCAN_OPTION_SET(options, field, RTEST(kwargs_values[SCAN_KWARG_##field]));
    OPTIONS_FIELDS(SCAN_OPTION_SET_KWARG)
#undef SCAN_OPTION_SET_KWARG
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
    .flags = RUBY_TYPED_FREE_IMMEDIATELY
#ifdef RUBY_TYPED_FROZEN_SHAREABLE
             | RUBY_TYPED_FROZEN_SHAREABLE
#endif
    ,
};

static VALUE options_alloc(VALUE self)
{
  scan_options *options;
  // TypedData_Make_Struct zeroes options.
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

static VALUE options_m_initialize_copy(VALUE self, VALUE other)
{
  scan_options *options, *other_options;
  rb_call_super(1, &other);
  TypedData_Get_Struct(self, scan_options, &options_type, options);
  TypedData_Get_Struct(other, scan_options, &options_type, other_options);
  *options = *other_options;
  return self;
}

#define DEFINE_OPTIONS_ACCESSORS(field)                                                   \
  static VALUE options_m_##field(VALUE self)                                             \
  {                                                                                       \
    scan_options *options;                                                               \
    TypedData_Get_Struct(self, scan_options, &options_type, options);                   \
    if (!SCAN_OPTION_IS_SET(options, field))                                             \
      return Qnil;                                                                       \
    return SCAN_OPTION(options, field) ? Qtrue : Qfalse;                                \
  }                                                                                       \
  static VALUE options_m_##field##_set(VALUE self, VALUE value)                         \
  {                                                                                       \
    scan_options *options;                                                               \
    rb_check_frozen(self);                                                               \
    TypedData_Get_Struct(self, scan_options, &options_type, options);                   \
    SCAN_OPTION_SET(options, field, RTEST(value));                                       \
    return value;                                                                        \
  }
OPTIONS_FIELDS(DEFINE_OPTIONS_ACCESSORS)

static VALUE options_m_equal(VALUE self, VALUE other)
{
  scan_options *options, *other_options;
  if (!rb_obj_is_kind_of(other, rb_cJsonScannerOptions))
    return Qfalse;
  TypedData_Get_Struct(self, scan_options, &options_type, options);
  TypedData_Get_Struct(other, scan_options, &options_type, other_options);
#define OPTIONS_EQUAL(field) \
  if (options->field != other_options->field) \
    return Qfalse;
  OPTIONS_FIELDS(OPTIONS_EQUAL)
#undef OPTIONS_EQUAL
  return Qtrue;
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
  OPTIONS_FIELDS(APPEND_OPTION_INSPECT)
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
  rb_define_method(rb_cJsonScannerOptions, "initialize_copy", options_m_initialize_copy, 1);
#define DEFINE_OPTIONS_ACCESSOR_METHODS(field)                                      \
  rb_define_method(rb_cJsonScannerOptions, #field, options_m_##field, 0);           \
  rb_define_method(rb_cJsonScannerOptions, #field "=", options_m_##field##_set, 1);
  OPTIONS_FIELDS(DEFINE_OPTIONS_ACCESSOR_METHODS)
#undef DEFINE_OPTIONS_ACCESSOR_METHODS
  rb_define_method(rb_cJsonScannerOptions, "==", options_m_equal, 1);
  rb_define_method(rb_cJsonScannerOptions, "inspect", options_m_inspect, 0);
#define INIT_SCAN_KWARG(field) scan_kwargs_table[SCAN_KWARG_##field] = rb_intern(#field);
  OPTIONS_FIELDS(INIT_SCAN_KWARG)
#undef INIT_SCAN_KWARG
}

#endif /* JSON_SCANNER_OPTIONS_H */
