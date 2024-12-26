#include "json_scanner.h"

VALUE rb_mJsonScanner;
VALUE rb_eJsonScannerParseError;
ID scan_kwargs_table[7];

VALUE null_sym;
VALUE boolean_sym;
VALUE number_sym;
VALUE string_sym;
VALUE object_sym;
VALUE array_sym;

enum matcher_type
{
  MATCHER_KEY,
  MATCHER_INDEX,
  // MATCHER_ANY_KEY,
  MATCHER_INDEX_RANGE,
  // MATCHER_KEYS_LIST,
  // MATCHER_KEY_REGEX,
};

enum path_type
{
  PATH_KEY,
  PATH_INDEX,
};

typedef struct
{
  const char *val;
  size_t len;
} hashkey_t;

typedef struct
{
  long start;
  long end;
} range_t;

typedef struct
{
  enum matcher_type type;
  union
  {
    hashkey_t key;
    long index;
    range_t range;
  } value;
} path_matcher_elem_t;

typedef struct
{
  enum path_type type;
  union
  {
    hashkey_t key;
    long index;
  } value;
} path_elem_t;

typedef struct
{
  path_matcher_elem_t *elems;
  int len;
  int matched_depth;
} paths_t;

typedef struct
{
  int with_path;
  paths_t *paths;
  int paths_len;
  path_elem_t *current_path;
  int current_path_len;
  int max_path_len;
  // Easier to use a Ruby array for result than convert later
  VALUE points_list;
  // by depth
  size_t *starts;
  // VALUE rb_err;
  yajl_handle handle;
} scan_ctx;

// FIXME: This will cause memory leak if ruby_xmalloc raises
scan_ctx *scan_ctx_init(VALUE path_ary, int with_path)
{
  int path_ary_len;
  scan_ctx *ctx;
  paths_t *paths;
  // TODO: Allow to_ary and sized enumerables
  rb_check_type(path_ary, T_ARRAY);
  path_ary_len = rb_long2int(rb_array_len(path_ary));
  // Check types early before any allocations, so exception is ok
  // TODO: Fix this, just handle errors
  for (int i = 0; i < path_ary_len; i++)
  {
    int path_len;
    VALUE path = rb_ary_entry(path_ary, i);
    rb_check_type(path, T_ARRAY);
    path_len = rb_long2int(rb_array_len(path));
    for (int j = 0; j < path_len; j++)
    {
      VALUE entry = rb_ary_entry(path, j);
      int type = TYPE(entry);
      if (type == T_STRING)
      {
#if LONG_MAX > SIZE_MAX
        RSTRING_LENINT(entry);
#endif
      }
      else if (type == T_FIXNUM || type == T_BIGNUM)
      {
        RB_NUM2LONG(entry);
      }
      else
      {
        VALUE range_beg, range_end;
        long end_val;
        int open_ended;
        if (rb_range_values(entry, &range_beg, &range_end, &open_ended) != Qtrue)
          rb_raise(rb_eArgError, "path elements must be strings, integers, or ranges");
        if (RB_NUM2LONG(range_beg) < 0L)
          rb_raise(rb_eArgError, "range start must be positive");
        end_val = RB_NUM2LONG(range_end);
        if (end_val < -1L)
          rb_raise(rb_eArgError, "range end must be positive or -1");
        if (end_val == -1L && open_ended)
          rb_raise(rb_eArgError, "range with -1 end must be closed");
      }
    }
  }

  ctx = ruby_xmalloc(sizeof(scan_ctx));

  ctx->with_path = with_path;
  ctx->max_path_len = 0;

  paths = ruby_xmalloc(sizeof(paths_t) * path_ary_len);
  for (int i = 0; i < path_ary_len; i++)
  {
    int path_len;
    VALUE path = rb_ary_entry(path_ary, i);
    path_len = rb_long2int(rb_array_len(path));
    if (path_len > ctx->max_path_len)
      ctx->max_path_len = path_len;
    paths[i].elems = ruby_xmalloc2(sizeof(path_matcher_elem_t), path_len);
    for (int j = 0; j < path_len; j++)
    {
      VALUE entry = rb_ary_entry(path, j);
      int type = TYPE(entry);
      if (type == T_STRING)
      {
        paths[i].elems[j].type = MATCHER_KEY;
        paths[i].elems[j].value.key.val = RSTRING_PTR(entry);
#if LONG_MAX > SIZE_MAX
        paths[i].elems[j].value.key.len = RSTRING_LENINT(entry);
#else
        paths[i].elems[j].value.key.len = RSTRING_LEN(entry);
#endif
      }
      else if (type == T_FIXNUM || type == T_BIGNUM)
      {
        paths[i].elems[j].type = MATCHER_INDEX;
        paths[i].elems[j].value.index = FIX2LONG(entry);
      }
      else
      {
        VALUE range_beg, range_end;
        int open_ended;
        paths[i].elems[j].type = MATCHER_INDEX_RANGE;
        rb_range_values(entry, &range_beg, &range_end, &open_ended);
        paths[i].elems[j].value.range.start = RB_NUM2LONG(range_beg);
        paths[i].elems[j].value.range.end = RB_NUM2LONG(range_end);
        // (value..-1) works as expected, (value...-1) is forbidden above
        if (paths[i].elems[j].value.range.end == -1L)
          paths[i].elems[j].value.range.end = LONG_MAX;
        // -1 here is fine, so, (0...0) works just as expected - doesn't match anything
        if (open_ended)
          paths[i].elems[j].value.range.end--;
      }
    }
    paths[i].len = path_len;
    paths[i].matched_depth = 0;
  }

  ctx->paths = paths;
  ctx->paths_len = path_ary_len;
  ctx->current_path = ruby_xmalloc2(sizeof(path_elem_t), ctx->max_path_len);

  ctx->current_path_len = 0;
  ctx->points_list = rb_ary_new_capa(path_ary_len);
  for (int i = 0; i < path_ary_len; i++)
  {
    rb_ary_push(ctx->points_list, rb_ary_new());
  }

  ctx->starts = ruby_xmalloc2(sizeof(size_t), ctx->max_path_len);
  // ctx->rb_err = Qnil;
  ctx->handle = NULL;

  return ctx;
}

void scan_ctx_free(scan_ctx *ctx)
{
  if (!ctx)
    return;
  ruby_xfree(ctx->starts);
  ruby_xfree(ctx->current_path);
  for (int i = 0; i < ctx->paths_len; i++)
  {
    ruby_xfree(ctx->paths[i].elems);
  }
  ruby_xfree(ctx->paths);
  ruby_xfree(ctx);
}

// noexcept
inline void increment_arr_index(scan_ctx *sctx)
{
  // remember - any value can be root
  // TODO: Maybe make current_path_len 1 shorter and get rid of -1; need to change all compares
  if (sctx->current_path_len && sctx->current_path[sctx->current_path_len - 1].type == PATH_INDEX)
  {
    sctx->current_path[sctx->current_path_len - 1].value.index++;
  }
}

typedef enum
{
  null_value,
  boolean_value,
  number_value,
  string_value,
  object_value,
  array_value,
} value_type;

// noexcept
VALUE create_point(scan_ctx *sctx, value_type type, size_t length, size_t curr_pos)
{
  VALUE values[3];
  VALUE point = rb_ary_new_capa(3);
  // noexcept
  values[1] = RB_ULONG2NUM(curr_pos);
  switch (type)
  {
    // FIXME: size_t can be longer than ulong
  case null_value:
    values[0] = RB_ULONG2NUM(curr_pos - length);
    values[2] = null_sym;
    break;
  case boolean_value:
    values[0] = RB_ULONG2NUM(curr_pos - length);
    values[2] = boolean_sym;
    break;
  case number_value:
    values[0] = RB_ULONG2NUM(curr_pos - length);
    values[2] = number_sym;
    break;
  case string_value:
    values[0] = RB_ULONG2NUM(curr_pos - length);
    values[2] = string_sym;
    break;
  case object_value:
    values[0] = RB_ULONG2NUM(sctx->starts[sctx->current_path_len]);
    values[2] = object_sym;
    break;
  case array_value:
    values[0] = RB_ULONG2NUM(sctx->starts[sctx->current_path_len]);
    values[2] = array_sym;
    break;
  }
  // rb_ary_cat raise only in case of a frozen array or if len is too long
  rb_ary_cat(point, values, 3);
  return point;
}

// noexcept
VALUE create_path(scan_ctx *sctx)
{
  VALUE path = rb_ary_new_capa(sctx->current_path_len);
  for (int i = 0; i < sctx->current_path_len; i++)
  {
    VALUE entry;
    switch (sctx->current_path[i].type)
    {
    case PATH_KEY:
      entry = rb_str_new(sctx->current_path[i].value.key.val, sctx->current_path[i].value.key.len);
      break;
    case PATH_INDEX:
      entry = RB_ULONG2NUM(sctx->current_path[i].value.index);
      break;
    default:
      entry = Qnil;
    }
    rb_ary_push(path, entry);
  }
  return path;
}

// noexcept
void save_point(scan_ctx *sctx, value_type type, size_t length)
{
  // TODO: Abort parsing if all paths are matched and no more mathces are possible: only trivial key/index matchers at the current level
  // TODO: Don't re-compare already matched prefixes; hard to invalidate, though
  // TODO: Might fail in case of no memory
  VALUE point = Qundef;
  int match;
  for (int i = 0; i < sctx->paths_len; i++)
  {
    if (sctx->paths[i].len != sctx->current_path_len)
      continue;

    match = true;
    for (int j = 0; j < sctx->current_path_len; j++)
    {
      switch (sctx->paths[i].elems[j].type)
      {
      case MATCHER_KEY:
        if (sctx->current_path[j].type != PATH_KEY ||
            sctx->current_path[j].value.key.len != sctx->paths[i].elems[j].value.key.len ||
            strncmp(sctx->current_path[j].value.key.val, sctx->paths[i].elems[j].value.key.val, sctx->current_path[j].value.key.len))
          match = false;
        break;
      case MATCHER_INDEX:
        if (sctx->current_path[j].type != PATH_INDEX ||
            sctx->current_path[j].value.index != sctx->paths[i].elems[j].value.index)
          match = false;
        break;
      case MATCHER_INDEX_RANGE:
        if (sctx->current_path[j].type != PATH_INDEX ||
            sctx->current_path[j].value.index < sctx->paths[i].elems[j].value.range.start ||
            sctx->current_path[j].value.index > sctx->paths[i].elems[j].value.range.end)
          match = false;
        break;
      }
      if (!match)
        break;
    }
    if (match)
    {
      if (point == Qundef)
      {
        point = create_point(sctx, type, length, yajl_get_bytes_consumed(sctx->handle));
        if (sctx->with_path)
        {
          point = rb_ary_new_from_args(2, create_path(sctx), point);
        }
      }
      // rb_ary_push raises only in case of a frozen array, which is not the case
      // rb_ary_entry is safe
      rb_ary_push(rb_ary_entry(sctx->points_list, i), point);
    }
  }
}

// noexcept
int scan_on_null(void *ctx)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  if (sctx->current_path_len > sctx->max_path_len)
    return true;
  increment_arr_index(sctx);
  save_point(sctx, null_value, 4);
  return true;
}

// noexcept
int scan_on_boolean(void *ctx, int bool_val)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  if (sctx->current_path_len > sctx->max_path_len)
    return true;
  increment_arr_index(sctx);
  save_point(sctx, boolean_value, bool_val ? 4 : 5);
  return true;
}

// noexcept
int scan_on_number(void *ctx, const char *val, size_t len)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  if (sctx->current_path_len > sctx->max_path_len)
    return true;
  increment_arr_index(sctx);
  save_point(sctx, number_value, len);
  return true;
}

// noexcept
int scan_on_string(void *ctx, const unsigned char *val, size_t len)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  if (sctx->current_path_len > sctx->max_path_len)
    return true;
  increment_arr_index(sctx);
  save_point(sctx, string_value, len + 2);
  return true;
}

// noexcept
int scan_on_start_object(void *ctx)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  if (sctx->current_path_len > sctx->max_path_len)
  {
    sctx->current_path_len++;
    return true;
  }
  increment_arr_index(sctx);
  if (sctx->current_path_len < sctx->max_path_len)
  {
    sctx->starts[sctx->current_path_len] = yajl_get_bytes_consumed(sctx->handle) - 1;
    sctx->current_path[sctx->current_path_len].type = PATH_KEY;
  }
  sctx->current_path_len++;
  return true;
}

// noexcept
int scan_on_key(void *ctx, const unsigned char *key, size_t len)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  if (sctx->current_path_len > sctx->max_path_len)
    return true;
  // Can't be called without scan_on_start_object being called before
  // So current_path_len at least 1 and key.type is set to PATH_KEY;
  sctx->current_path[sctx->current_path_len - 1].value.key.val = (char *)key;
  sctx->current_path[sctx->current_path_len - 1].value.key.len = len;
  return true;
}

// noexcept
int scan_on_end_object(void *ctx)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  sctx->current_path_len--;
  if (sctx->current_path_len >= sctx->max_path_len)
    return true;
  save_point(sctx, object_value, 0);
  return true;
}

// noexcept
int scan_on_start_array(void *ctx)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  if (sctx->current_path_len > sctx->max_path_len)
  {
    sctx->current_path_len++;
    return true;
  }
  increment_arr_index(sctx);
  if (sctx->current_path_len < sctx->max_path_len)
  {
    sctx->starts[sctx->current_path_len] = yajl_get_bytes_consumed(sctx->handle) - 1;
    sctx->current_path[sctx->current_path_len].type = PATH_INDEX;
    sctx->current_path[sctx->current_path_len].value.index = -1;
  }
  sctx->current_path_len++;
  return true;
}

// noexcept
int scan_on_end_array(void *ctx)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  sctx->current_path_len--;
  if (sctx->current_path_len >= sctx->max_path_len)
    return true;
  save_point(sctx, array_value, 0);
  return true;
}

static yajl_callbacks scan_callbacks = {
    scan_on_null,
    scan_on_boolean,
    NULL,
    NULL,
    scan_on_number,
    scan_on_string,
    scan_on_start_object,
    scan_on_key,
    scan_on_end_object,
    scan_on_start_array,
    scan_on_end_array};

// def scan(json_str, path_arr, opts)
// opts
// with_path: false, verbose_error: false,
// the following opts converted to bool and passed to yajl_config if provided, ignored if not provided
// allow_comments, dont_validate_strings, allow_trailing_garbage, allow_multiple_values, allow_partial_values
VALUE scan(int argc, VALUE *argv, VALUE self)
{
  VALUE json_str, path_ary, with_path_flag, kwargs;
  VALUE kwargs_values[7];

  int with_path = false, verbose_error = false;
  char *json_text;
  size_t json_text_len;
  yajl_handle handle;
  yajl_status stat;
  scan_ctx *ctx;
  VALUE err = Qnil, result;
  // Turned out callbacks can't raise exceptions
  // VALUE callback_err;
#if RUBY_API_VERSION_MAJOR > 2 || (RUBY_API_VERSION_MAJOR == 2 && RUBY_API_VERSION_MINOR >= 7)
  rb_scan_args_kw(RB_SCAN_ARGS_LAST_HASH_KEYWORDS, argc, argv, "21:", &json_str, &path_ary, &with_path_flag, &kwargs);
#else
  rb_scan_args(argc, argv, "21:", &json_str, &path_ary, &with_path_flag, &kwargs);
#endif
  // rb_io_write(rb_stderr, rb_sprintf("with_path_flag: %" PRIsVALUE " \n", with_path_flag));
  with_path = RTEST(with_path_flag);
  if (kwargs != Qnil)
  {
    rb_get_kwargs(kwargs, scan_kwargs_table, 0, 7, kwargs_values);
    if (kwargs_values[0] != Qundef)
      with_path = RTEST(kwargs_values[0]);
    if (kwargs_values[1] != Qundef)
      verbose_error = RTEST(kwargs_values[1]);
  }
  rb_check_type(json_str, T_STRING);
  json_text = RSTRING_PTR(json_str);
#if LONG_MAX > SIZE_MAX
  json_text_len = RSTRING_LENINT(json_str);
#else
  json_text_len = RSTRING_LEN(json_str);
#endif
  ctx = scan_ctx_init(path_ary, with_path);

  handle = yajl_alloc(&scan_callbacks, NULL, (void *)ctx);
  if (kwargs != Qnil) // it's safe to read kwargs_values only if rb_get_kwargs was called
  {
    if (kwargs_values[2] != Qundef)
      yajl_config(handle, yajl_allow_comments, RTEST(kwargs_values[2]));
    if (kwargs_values[3] != Qundef)
      yajl_config(handle, yajl_dont_validate_strings, RTEST(kwargs_values[3]));
    if (kwargs_values[4] != Qundef)
      yajl_config(handle, yajl_allow_trailing_garbage, RTEST(kwargs_values[4]));
    if (kwargs_values[5] != Qundef)
      yajl_config(handle, yajl_allow_multiple_values, RTEST(kwargs_values[5]));
    if (kwargs_values[6] != Qundef)
      yajl_config(handle, yajl_allow_partial_values, RTEST(kwargs_values[6]));
  }
  ctx->handle = handle;
  stat = yajl_parse(handle, (unsigned char *)json_text, json_text_len);
  if (stat == yajl_status_ok)
    stat = yajl_complete_parse(handle);

  if (stat != yajl_status_ok)
  {
    char *str = (char *)yajl_get_error(handle, verbose_error, (unsigned char *)json_text, json_text_len);
    err = rb_str_new_cstr(str);
    yajl_free_error(handle, (unsigned char *)str);
  }
  // callback_err = ctx->rb_err;
  result = ctx->points_list;
  scan_ctx_free(ctx);
  yajl_free(handle);
  if (err != Qnil)
    rb_exc_raise(rb_exc_new_str(rb_eJsonScannerParseError, err));
  // if (callback_err != Qnil)
  //   rb_exc_raise(callback_err);
  // TODO: report yajl_get_bytes_consumed(handle)
  return result;
}

RUBY_FUNC_EXPORTED void
Init_json_scanner(void)
{
  rb_mJsonScanner = rb_define_module("JsonScanner");
  rb_define_const(rb_mJsonScanner, "ANY_INDEX", rb_range_new(INT2FIX(0), INT2FIX(-1), false));
  rb_eJsonScannerParseError = rb_define_class_under(rb_mJsonScanner, "ParseError", rb_eRuntimeError);
  rb_define_module_function(rb_mJsonScanner, "scan", scan, -1);
  null_sym = rb_id2sym(rb_intern("null"));
  boolean_sym = rb_id2sym(rb_intern("boolean"));
  number_sym = rb_id2sym(rb_intern("number"));
  string_sym = rb_id2sym(rb_intern("string"));
  object_sym = rb_id2sym(rb_intern("object"));
  array_sym = rb_id2sym(rb_intern("array"));
  scan_kwargs_table[0] = rb_intern("with_path");
  scan_kwargs_table[1] = rb_intern("verbose_error");
  scan_kwargs_table[2] = rb_intern("allow_comments");
  scan_kwargs_table[3] = rb_intern("dont_validate_strings");
  scan_kwargs_table[4] = rb_intern("allow_trailing_garbage");
  scan_kwargs_table[5] = rb_intern("allow_multiple_values");
  scan_kwargs_table[6] = rb_intern("allow_partial_values");
}
