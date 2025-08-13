#include "json_scanner.h"

VALUE rb_mJsonScanner;
VALUE rb_cJsonScannerConfig;
VALUE rb_eJsonScannerParseError;
#define BYTES_CONSUMED "bytes_consumed"
ID rb_iv_bytes_consumed;
#define SCAN_KWARGS_SIZE 8
ID scan_kwargs_table[SCAN_KWARGS_SIZE];

VALUE null_sym;
VALUE boolean_sym;
VALUE number_sym;
VALUE string_sym;
VALUE object_sym;
VALUE array_sym;

VALUE any_key_sym;

enum matcher_type
{
  MATCHER_KEY,
  MATCHER_INDEX,
  MATCHER_ANY_KEY,
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
  int symbolize_path_keys;
  int paths_len;
  paths_t *paths;
  int current_path_len;
  int max_path_len;
  path_elem_t *current_path;
  // Easier to use a Ruby array for result than convert later
  // must be supplied by the caller and RB_GC_GUARD-ed if it isn't on the stack
  VALUE points_list;
  // by depth
  size_t *starts;
  // VALUE rb_err;
  yajl_handle handle;
} scan_ctx;

void scan_ctx_debug(scan_ctx *ctx)
{
  // actually might have been cleared by GC already, be careful, debug only when in valid state
  VALUE points_list_inspect = ctx->points_list == Qundef ? rb_str_new_cstr("undef") : rb_sprintf("%" PRIsVALUE, rb_inspect(ctx->points_list));
  fprintf(stderr, "\nscan_ctx {\n");
  fprintf(stderr, "  with_path: %s,\n", ctx->with_path ? "true" : "false");
  fprintf(stderr, "  symbolize_path_keys: %s,\n", ctx->symbolize_path_keys ? "true" : "false");
  fprintf(stderr, "  paths_len: %d,\n", ctx->paths_len);

  fprintf(stderr, "  paths: [\n");
  for (int i = 0; ctx->paths && i < ctx->paths_len; i++)
  {
    fprintf(stderr, "    [");
    for (int j = 0; j < ctx->paths[i].len; j++)
    {
      switch (ctx->paths[i].elems[j].type)
      {
      case MATCHER_KEY:
        fprintf(stderr, "'%.*s'", (int)ctx->paths[i].elems[j].value.key.len, ctx->paths[i].elems[j].value.key.val);
        break;
      case MATCHER_INDEX:
        fprintf(stderr, "%ld", ctx->paths[i].elems[j].value.index);
        break;
      case MATCHER_INDEX_RANGE:
        fprintf(stderr, "(%ld..%ld)", ctx->paths[i].elems[j].value.range.start, ctx->paths[i].elems[j].value.range.end);
        break;
      case MATCHER_ANY_KEY:
        fprintf(stderr, "('*'..'*')");
        break;
      }
      if (j < ctx->paths[i].len - 1)
        fprintf(stderr, ", ");
    }
    fprintf(stderr, "],\n");
  }
  fprintf(stderr, "  ],\n");

  fprintf(stderr, "  current_path_len: %d,\n", ctx->current_path_len);
  fprintf(stderr, "  max_path_len: %d,\n", ctx->max_path_len);
  fprintf(stderr, "  current_path: [");
  for (int i = 0; i < ctx->current_path_len; i++)
  {
    switch (ctx->current_path[i].type)
    {
    case PATH_KEY:
      fprintf(stderr, "'%.*s'", (int)ctx->current_path[i].value.key.len, ctx->current_path[i].value.key.val);
      break;
    case PATH_INDEX:
      fprintf(stderr, "%ld", ctx->current_path[i].value.index);
    }
    if (i < ctx->current_path_len - 1)
      fprintf(stderr, ", ");
  }
  fprintf(stderr, "],\n");

  fprintf(stderr, "  points_list: %.*s,\n", RSTRING_LENINT(points_list_inspect), RSTRING_PTR(points_list_inspect));
  fprintf(stderr, "  starts: [");
  for (int i = 0; i <= ctx->max_path_len; i++)
  {
    fprintf(stderr, "%ld", ctx->starts[i]);
    if (i < ctx->max_path_len)
      fprintf(stderr, ", ");
  }
  fprintf(stderr, "],\n");

  fprintf(stderr, "  handle: %p,\n", ctx->handle);
  fprintf(stderr, "}\n\n\n");
}

// FIXME: This will cause memory leak if ruby_xmalloc raises
// path_ary must be RB_GC_GUARD-ed by the caller
VALUE scan_ctx_init(scan_ctx *ctx, VALUE path_ary, VALUE string_keys)
{
  int path_ary_len;
  paths_t *paths;
  // TODO: Allow to_ary and sized enumerables
  rb_check_type(path_ary, T_ARRAY);
  path_ary_len = rb_long2int(rb_array_len(path_ary));
  // Check types early before any allocations, so exception is ok
  // TODO: Fix this, just handle errors
  // It's not possible that another Ruby thread changes path_ary items between these two loops, because C call holds GVL
  for (int i = 0; i < path_ary_len; i++)
  {
    int path_len;
    VALUE path = rb_ary_entry(path_ary, i);
    rb_check_type(path, T_ARRAY);
    path_len = rb_long2int(rb_array_len(path));
    for (int j = 0; j < path_len; j++)
    {
      VALUE entry = rb_ary_entry(path, j);
      switch (TYPE(entry))
      {
      case T_SYMBOL:
        entry = rb_sym2str(entry);
        /* fall through */
      case T_STRING:
#if LONG_MAX > SIZE_MAX
        RSTRING_LENINT(entry);
#endif
        break;
      case T_FIXNUM:
      case T_BIGNUM:
        RB_NUM2LONG(entry);
        break;
      default:
      {
        VALUE range_beg, range_end;
        long end_val;
        int open_ended;
        if (rb_range_values(entry, &range_beg, &range_end, &open_ended) != Qtrue)
          return rb_exc_new_cstr(rb_eArgError, "path elements must be strings, integers, or ranges");
        if (range_beg != any_key_sym || range_end != any_key_sym)
        {
          if (RB_NUM2LONG(range_beg) < 0L)
            return rb_exc_new_cstr(rb_eArgError, "range start must be positive");
          end_val = RB_NUM2LONG(range_end);
          if (end_val < -1L)
            return rb_exc_new_cstr(rb_eArgError, "range end must be positive or -1");
          if (end_val == -1L && open_ended)
            return rb_exc_new_cstr(rb_eArgError, "range with -1 end must be closed");
        }
      }
      }
    }
  }

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
      switch (TYPE(entry))
      {
      case T_SYMBOL:
        entry = rb_sym2str(entry);
        /* fall through */
      case T_STRING:
      {
        if (string_keys != Qundef)
        {
          // If string_keys is provided, we need to duplicate the string
          // to avoid use-after-free issues and to add the newly created string to the string_keys array
          entry = rb_str_dup(entry);
          rb_ary_push(string_keys, entry);
        }
        paths[i].elems[j].type = MATCHER_KEY;
        paths[i].elems[j].value.key.val = RSTRING_PTR(entry);
#if LONG_MAX > SIZE_MAX
        paths[i].elems[j].value.key.len = RSTRING_LENINT(entry);
#else
        paths[i].elems[j].value.key.len = RSTRING_LEN(entry);
#endif
      }
      break;
      case T_FIXNUM:
      case T_BIGNUM:
      {
        paths[i].elems[j].type = MATCHER_INDEX;
        paths[i].elems[j].value.index = FIX2LONG(entry);
      }
      break;
      default:
      {
        VALUE range_beg, range_end;
        int open_ended;
        rb_range_values(entry, &range_beg, &range_end, &open_ended);
        if (range_beg == any_key_sym && range_end == any_key_sym)
        {
          paths[i].elems[j].type = MATCHER_ANY_KEY;
        }
        else
        {
          paths[i].elems[j].type = MATCHER_INDEX_RANGE;
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
      }
    }
    paths[i].len = path_len;
    paths[i].matched_depth = 0;
  }

  ctx->paths = paths;
  ctx->paths_len = path_ary_len;
  ctx->current_path = ruby_xmalloc2(sizeof(path_elem_t), ctx->max_path_len);

  ctx->starts = ruby_xmalloc2(sizeof(size_t), ctx->max_path_len + 1);
  return Qundef; // no error
}

// resets temporary values in the config
void scan_ctx_reset(scan_ctx *ctx, VALUE points_list, int with_path, int symbolize_path_keys)
{
  // TODO: reset matched_depth if implemented
  ctx->current_path_len = 0;
  // ctx->rb_err = Qnil;
  ctx->handle = NULL;
  ctx->points_list = points_list;
  ctx->with_path = with_path;
  ctx->symbolize_path_keys = symbolize_path_keys;
}

void scan_ctx_free(scan_ctx *ctx)
{
  // fprintf(stderr, "scan_ctx_free\n");
  if (!ctx)
    return;
  ruby_xfree(ctx->starts);
  ruby_xfree(ctx->current_path);
  if (!ctx->paths)
    return;
  for (int i = 0; i < ctx->paths_len; i++)
  {
    ruby_xfree(ctx->paths[i].elems);
  }
  ruby_xfree(ctx->paths);
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
      if (sctx->symbolize_path_keys)
        entry = rb_id2sym(rb_intern2(sctx->current_path[i].value.key.val, sctx->current_path[i].value.key.len));
      else
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
  VALUE point = Qundef, path;
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
      case MATCHER_ANY_KEY:
        if (sctx->current_path[j].type != PATH_KEY)
          match = false;
        break;
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
          path = create_path(sctx);
          point = rb_ary_new_from_args(2, path, point);
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
  sctx->starts[sctx->current_path_len] = yajl_get_bytes_consumed(sctx->handle) - 1;
  if (sctx->current_path_len < sctx->max_path_len)
    sctx->current_path[sctx->current_path_len].type = PATH_KEY;
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
  if (sctx->current_path_len <= sctx->max_path_len)
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
  sctx->starts[sctx->current_path_len] = yajl_get_bytes_consumed(sctx->handle) - 1;
  if (sctx->current_path_len < sctx->max_path_len)
  {
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
  if (sctx->current_path_len <= sctx->max_path_len)
    save_point(sctx, array_value, 0);
  return true;
}

void config_free(void *data)
{
  scan_ctx_free((scan_ctx *)data);
  ruby_xfree(data);
}

size_t config_size(const void *data)
{
  // see ObjectSpace.memsize_of
  scan_ctx *ctx = (scan_ctx *)data;
  size_t res = sizeof(scan_ctx);
  // current_path
  if (ctx->current_path != NULL)
    res += ctx->max_path_len * sizeof(path_elem_t);
  // starts
  if (ctx->starts != NULL)
    res += ctx->max_path_len * sizeof(size_t);
  if (ctx->paths != NULL)
  {
    res += ctx->paths_len * sizeof(paths_t);
    for (int i = 0; i < ctx->paths_len; i++)
    {
      res += ctx->paths[i].len * sizeof(path_matcher_elem_t);
    }
  }
  return res;
}

static const rb_data_type_t config_type = {
    .wrap_struct_name = "json_scanner_config",
    .function = {
        .dfree = config_free,
        .dsize = config_size,
    },
    .flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

VALUE config_alloc(VALUE self)
{
  scan_ctx *ctx = ruby_xmalloc(sizeof(scan_ctx));
  ctx->paths = NULL;
  ctx->paths_len = 0;
  ctx->current_path = NULL;
  ctx->max_path_len = 0;
  ctx->starts = NULL;
  scan_ctx_reset(ctx, Qundef, false, false);
  return TypedData_Wrap_Struct(self, &config_type, ctx);
}

VALUE config_m_initialize(VALUE self, VALUE path_ary)
{
  scan_ctx *ctx;
  VALUE scan_ctx_init_err, string_keys;
  TypedData_Get_Struct(self, scan_ctx, &config_type, ctx);
  string_keys = rb_ary_new();
  scan_ctx_init_err = scan_ctx_init(ctx, path_ary, string_keys);
  if (scan_ctx_init_err != Qundef)
  {
    rb_exc_raise(scan_ctx_init_err);
  }
  rb_iv_set(self, "string_keys", string_keys);
  return self;
}

VALUE config_m_inspect(VALUE self)
{
  scan_ctx *ctx;
  VALUE res;
  TypedData_Get_Struct(self, scan_ctx, &config_type, ctx);
  res = rb_sprintf("#<%" PRIsVALUE " [", rb_class_name(CLASS_OF(self)));
  for (int i = 0; ctx->paths && i < ctx->paths_len; i++)
  {
    rb_str_cat_cstr(res, "[");
    for (int j = 0; j < ctx->paths[i].len; j++)
    {
      switch (ctx->paths[i].elems[j].type)
      {
      case MATCHER_KEY:
        rb_str_catf(res, "'%.*s'", (int)ctx->paths[i].elems[j].value.key.len, ctx->paths[i].elems[j].value.key.val);
        break;
      case MATCHER_INDEX:
        rb_str_catf(res, "%ld", ctx->paths[i].elems[j].value.index);
        break;
      case MATCHER_INDEX_RANGE:
        rb_str_catf(res, "(%ld..%ld)", ctx->paths[i].elems[j].value.range.start, ctx->paths[i].elems[j].value.range.end);
        break;
      case MATCHER_ANY_KEY:
        rb_str_cat_cstr(res, "('*'..'*')");
        break;
      }
      if (j < ctx->paths[i].len - 1)
        rb_str_cat_cstr(res, ", ");
    }
    rb_str_cat_cstr(res, "]");
    if (i < ctx->paths_len - 1)
      rb_str_cat_cstr(res, ", ");
  }
  rb_str_cat_cstr(res, "]>");
  return res;
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
  VALUE kwargs_values[SCAN_KWARGS_SIZE];

  int with_path = false, verbose_error = false, symbolize_path_keys = false;
  char *json_text;
  size_t json_text_len;
  yajl_handle handle;
  yajl_status stat;
  scan_ctx *ctx;
  int free_ctx = true;
  VALUE err_msg = Qnil, bytes_consumed, err, result;
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
    rb_get_kwargs(kwargs, scan_kwargs_table, 0, SCAN_KWARGS_SIZE, kwargs_values);
    if (kwargs_values[0] != Qundef)
      with_path = RTEST(kwargs_values[0]);
    if (kwargs_values[1] != Qundef)
      verbose_error = RTEST(kwargs_values[1]);
    if (kwargs_values[7] != Qundef)
      symbolize_path_keys = RTEST(kwargs_values[7]);
  }
  rb_check_type(json_str, T_STRING);
  json_text = RSTRING_PTR(json_str);
#if LONG_MAX > SIZE_MAX
  json_text_len = RSTRING_LENINT(json_str);
#else
  json_text_len = RSTRING_LEN(json_str);
#endif
  if (rb_obj_is_kind_of(path_ary, rb_cJsonScannerConfig))
  {
    free_ctx = false;
    TypedData_Get_Struct(path_ary, scan_ctx, &config_type, ctx);
  }
  else
  {
    VALUE scan_ctx_init_err;
    ctx = ruby_xmalloc(sizeof(scan_ctx));
    scan_ctx_init_err = scan_ctx_init(ctx, path_ary, Qundef);
    if (scan_ctx_init_err != Qundef)
    {
      ruby_xfree(ctx);
      rb_exc_raise(scan_ctx_init_err);
    }
  }
  // Need to keep a ref to result array on the stack to prevent it from being GC-ed
  result = rb_ary_new_capa(ctx->paths_len);
  for (int i = 0; i < ctx->paths_len; i++)
  {
    rb_ary_push(result, rb_ary_new());
  }
  scan_ctx_reset(ctx, result, with_path, symbolize_path_keys);
  // scan_ctx_debug(ctx);

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
    err_msg = rb_utf8_str_new_cstr(str);
    bytes_consumed = RB_ULONG2NUM(yajl_get_bytes_consumed(handle));
    yajl_free_error(handle, (unsigned char *)str);
  }
  // callback_err = ctx->rb_err;
  if (free_ctx)
  {
    // fprintf(stderr, "free_ctx\n");
    scan_ctx_free(ctx);
    ruby_xfree(ctx);
  }
  yajl_free(handle);
  if (err_msg != Qnil)
  {
    err = rb_exc_new_str(rb_eJsonScannerParseError, err_msg);
    rb_ivar_set(err, rb_iv_bytes_consumed, bytes_consumed);
    rb_exc_raise(err);
  }
  // if (callback_err != Qnil)
  //   rb_exc_raise(callback_err);
  return result;
}

RUBY_FUNC_EXPORTED void
Init_json_scanner(void)
{
  rb_mJsonScanner = rb_define_module("JsonScanner");
  rb_cJsonScannerConfig = rb_define_class_under(rb_mJsonScanner, "Config", rb_cObject);
  rb_define_alloc_func(rb_cJsonScannerConfig, config_alloc);
  rb_define_method(rb_cJsonScannerConfig, "initialize", config_m_initialize, 1);
  rb_define_method(rb_cJsonScannerConfig, "inspect", config_m_inspect, 0);
  rb_define_const(rb_mJsonScanner, "ANY_INDEX", rb_range_new(INT2FIX(0), INT2FIX(-1), false));
  any_key_sym = rb_id2sym(rb_intern("*"));
  rb_define_const(rb_mJsonScanner, "ANY_KEY", rb_range_new(any_key_sym, any_key_sym, false));
  rb_eJsonScannerParseError = rb_define_class_under(rb_mJsonScanner, "ParseError", rb_eRuntimeError);
  rb_define_attr(rb_eJsonScannerParseError, BYTES_CONSUMED, true, false);
  rb_iv_bytes_consumed = rb_intern("@" BYTES_CONSUMED);
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
  scan_kwargs_table[7] = rb_intern("symbolize_path_keys");
}
