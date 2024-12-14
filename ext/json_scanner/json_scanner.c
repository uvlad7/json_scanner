#include "json_scanner.h"

VALUE rb_mJsonScanner;
VALUE rb_mJsonScannerOptions;
VALUE rb_eJsonScannerParseError;

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
  // MATCHER_ANY_INDEX,
  MATCHER_INDEX_RANGE,
  // MATCHER_KEYS_LIST,
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
  int end;
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
  int current_depth;
  int max_depth;
  // Easier to use a Ruby array for result than convert later
  VALUE points_list;
  // by depth
  size_t *starts;
  // VALUE rb_err;
  yajl_handle handle;
} scan_ctx;

scan_ctx *scan_ctx_init(VALUE path_ary, VALUE with_path)
{
  // TODO: Allow to_ary and sized enumerables
  rb_check_type(path_ary, T_ARRAY);
  int path_ary_len = rb_long2int(rb_array_len(path_ary));
  // Check types early before any allocations, so exception is ok
  for (int i = 0; i < path_ary_len; i++)
  {
    VALUE path = rb_ary_entry(path_ary, i);
    rb_check_type(path, T_ARRAY);
    int path_len = rb_long2int(rb_array_len(path));
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
        int open_ended;
        if (rb_range_values(entry, &range_beg, &range_end, &open_ended) != Qtrue)
          rb_raise(rb_eArgError, "path elements must be strings, integers, or close-ended ranges");
        RB_NUM2LONG(range_beg);
        RB_NUM2LONG(range_end);
      }
    }
  }

  scan_ctx *ctx = ruby_xmalloc(sizeof(scan_ctx));

  ctx->with_path = RB_TEST(with_path);
  ctx->max_depth = 0;

  paths_t *paths = ruby_xmalloc(sizeof(paths_t) * path_ary_len);
  for (int i = 0; i < path_ary_len; i++)
  {
    VALUE path = rb_ary_entry(path_ary, i);
    int path_len = rb_long2int(rb_array_len(path));
    if (path_len > ctx->max_depth)
      ctx->max_depth = path_len;
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
        paths[i].elems[j].type = MATCHER_INDEX_RANGE;
        VALUE range_beg, range_end;
        int open_ended;
        rb_range_values(entry, &range_beg, &range_end, &open_ended);
        paths[i].elems[j].value.range.start = RB_NUM2LONG(range_beg);
        paths[i].elems[j].value.range.end = RB_NUM2LONG(range_end);
        if (open_ended)
          paths[i].elems[j].value.range.end = -1;
      }
    }
    paths[i].len = path_len;
    paths[i].matched_depth = 0;
  }

  ctx->paths = paths;
  ctx->paths_len = path_ary_len;
  ctx->current_path = xmalloc2(sizeof(path_elem_t), ctx->max_depth);

  ctx->current_depth = 0;
  ctx->points_list = rb_ary_new_capa(path_ary_len);
  for (int i = 0; i < path_ary_len; i++)
  {
    rb_ary_push(ctx->points_list, rb_ary_new());
  }

  ctx->starts = xmalloc2(sizeof(size_t), ctx->max_depth);
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
void inline increment_arr_index(scan_ctx *sctx)
{
  if (sctx->current_path[sctx->current_depth].type == PATH_INDEX)
  {
    sctx->current_path[sctx->current_depth].value.index++;
  }
}

// noexcept
void inline save_start_pos(scan_ctx *sctx)
{
  sctx->starts[sctx->current_depth] = yajl_get_bytes_consumed(sctx->handle) - 1;
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
void create_point(VALUE *point, scan_ctx *sctx, value_type type, size_t length, size_t curr_pos)
{
  *point = rb_ary_new_capa(3);
  VALUE values[3];
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
    values[2] = null_sym;
    break;
  case object_value:
    values[0] = RB_ULONG2NUM(sctx->starts[sctx->current_depth]);
    values[2] = object_sym;
    break;
  case array_value:
    values[0] = RB_ULONG2NUM(sctx->starts[sctx->current_depth]);
    values[2] = array_sym;
    break;
  }
  // rb_ary_cat raise only in case of a frozen array or if len is too long
  rb_ary_cat(*point, values, 3);
}

// noexcept
void save_point(scan_ctx *sctx, value_type type, size_t length)
{
  // TODO: Abort parsing if all paths are matched
  VALUE point = Qundef;
  for (int i = 0; i < sctx->paths_len; i++)
  {
    if (sctx->paths[i].len != sctx->current_depth)
      continue;

    int match = true;
    for (int j = 0; j < sctx->current_depth; j++)
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
      if (point = Qundef)
      {
        create_point(&point, sctx, type, length, yajl_get_bytes_consumed(sctx->handle));
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
  if (sctx->current_depth >= sctx->max_depth)
    return true;
  increment_arr_index(sctx);
  save_point(sctx, null_value, 4);
  return true;
}

// noexcept
int scan_on_boolean(void *ctx, int bool_val)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  if (sctx->current_depth >= sctx->max_depth)
    return true;
  increment_arr_index(sctx);
  save_point(sctx, boolean_value, bool_val ? 4 : 5);
  return true;
}

// noexcept
int scan_on_number(void *ctx, const char *val, size_t len)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  if (sctx->current_depth >= sctx->max_depth)
    return true;
  increment_arr_index(sctx);
  save_point(sctx, number_value, len);
  return true;
}

// noexcept
int scan_on_string(void *ctx, const unsigned char *val, size_t len)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  if (sctx->current_depth >= sctx->max_depth)
    return true;
  increment_arr_index(sctx);
  save_point(sctx, string_value, len + 2);
  return true;
}

// noexcept
int scan_on_start_object(void *ctx)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  if (sctx->current_depth >= sctx->max_depth)
    return true;
  increment_arr_index(sctx);
  save_start_pos(sctx);
  sctx->current_depth++;
  if (sctx->current_depth >= sctx->max_depth)
    return true;
  sctx->current_path[sctx->current_depth].type = PATH_KEY;
  return true;
}

// noexcept
int scan_on_key(void *ctx, const unsigned char *key, size_t len)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  if (sctx->current_depth >= sctx->max_depth)
    return true;
  // sctx->current_path[sctx->current_depth].type = PATH_KEY;
  sctx->current_path[sctx->current_depth].value.key.val = key;
  sctx->current_path[sctx->current_depth].value.key.len = len;
  return true;
}

// noexcept
int scan_on_end_object(void *ctx)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  sctx->current_depth--;
  if (sctx->current_depth >= sctx->max_depth)
    return true;

  save_point(sctx, object_value, 0);
  return true;
}

// noexcept
int scan_on_start_array(void *ctx)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  if (sctx->current_depth >= sctx->max_depth)
    return true;
  increment_arr_index(sctx);
  save_start_pos(sctx);
  sctx->current_depth++;
  if (sctx->current_depth >= sctx->max_depth)
    return true;
  sctx->current_path[sctx->current_depth].type = PATH_INDEX;
  sctx->current_path[sctx->current_depth].value.index = -1;
  return true;
}

// noexcept
int scan_on_end_array(void *ctx)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  sctx->current_depth--;
  if (sctx->current_depth >= sctx->max_depth)
    return true;
  save_point(sctx, object_value, 0);
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

VALUE scan(VALUE self, VALUE json_str, VALUE path_ary, VALUE with_path)
{
  rb_check_type(json_str, T_STRING);
  char *json_text = RSTRING_PTR(json_str);
#if LONG_MAX > SIZE_MAX
  size_t json_text_len = RSTRING_LENINT(json_str);
#else
  size_t json_text_len = RSTRING_LEN(json_str);
#endif
  yajl_handle handle;
  // TODO
  int opt_verbose_error = 0;
  yajl_status stat;
  scan_ctx *ctx = scan_ctx_init(path_ary, with_path);
  VALUE err = Qnil;
  VALUE result;
  // Turned out callbacks can't raise exceptions
  // VALUE callback_err;

  handle = yajl_alloc(&scan_callbacks, NULL, (void *)ctx);
  ctx->handle = handle;
  yajl_config(handle, yajl_allow_comments, true);
  yajl_config(handle, yajl_allow_trailing_garbage, true);
  stat = yajl_parse(handle, json_text, json_text_len);
  if (stat == yajl_status_ok)
    stat = yajl_complete_parse(handle);

  if (stat != yajl_status_ok)
  {
    unsigned char *str = yajl_get_error(handle, opt_verbose_error, json_text, json_text_len);
    err = rb_str_new_cstr(str);
    yajl_free_error(handle, str);
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
  rb_define_const(rb_mJsonScanner, "ALL", rb_range_new(INT2FIX(0), INT2FIX(-1), false));
  rb_mJsonScannerOptions = rb_define_module_under(rb_mJsonScanner, "Options");
  rb_eJsonScannerParseError = rb_define_class_under(rb_mJsonScanner, "ParseError", rb_eRuntimeError);
  rb_define_const(rb_mJsonScannerOptions, "ALLOW_COMMENTS", INT2FIX(yajl_allow_comments));
  rb_define_const(rb_mJsonScannerOptions, "DONT_VALIDATE_STRINGS", INT2FIX(yajl_dont_validate_strings));
  rb_define_const(rb_mJsonScannerOptions, "ALLOW_TRAILING_GARBAGE", INT2FIX(yajl_allow_trailing_garbage));
  rb_define_const(rb_mJsonScannerOptions, "ALLOW_MULTIPLE_VALUES", INT2FIX(yajl_allow_multiple_values));
  rb_define_const(rb_mJsonScannerOptions, "ALLOW_PARTIAL_VALUES", INT2FIX(yajl_allow_partial_values));
  rb_define_module_function(rb_mJsonScanner, "scan", scan, 3);
  null_sym = rb_intern("null");
  boolean_sym = rb_intern("boolean");
  number_sym = rb_intern("number");
  string_sym = rb_intern("string");
  object_sym = rb_intern("object");
  array_sym = rb_intern("array");
}
