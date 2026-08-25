#include "json_scanner.h"

VALUE rb_mJsonScanner;
VALUE rb_cJsonScannerSelector;
VALUE rb_cJsonScannerOptions;
VALUE rb_eJsonScannerParseError;
#define BYTES_CONSUMED "bytes_consumed"
ID rb_iv_bytes_consumed;
#define SCAN_KWARGS_SIZE 9
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
  VALUE roots_info_list;
  // by depth
  size_t *starts;
  yajl_handle handle;
  size_t yajl_bytes_consumed;
  const unsigned char *json_text;
} scan_ctx;

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

static inline size_t scan_ctx_get_bytes_consumed(scan_ctx *ctx)
{
  return ctx->yajl_bytes_consumed + yajl_get_bytes_consumed(ctx->handle);
}

static inline void scan_ctx_save_bytes_consumed(scan_ctx *ctx)
{
  ctx->yajl_bytes_consumed += yajl_get_bytes_consumed(ctx->handle);
}

static size_t scan_ctx_get_string_length(scan_ctx *ctx)
{
  size_t end = scan_ctx_get_bytes_consumed(ctx);
  size_t pos;

  if (end < 2)
    return end;
  pos = end - 1;

  while (pos > 0)
  {
    size_t backslashes = 0;
    pos--;
    if (ctx->json_text[pos] != '"')
      continue;
    for (size_t i = pos; i > 0 && ctx->json_text[i - 1] == '\\'; i--)
      backslashes++;
    if (backslashes % 2 == 0)
      return end - pos;
  }

  return end;
}

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
      break;
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
  fprintf(stderr, "  yajl_bytes_consumed: %ld,\n", ctx->yajl_bytes_consumed);
  fprintf(stderr, "}\n\n\n");
}

// path_ary must be RB_GC_GUARD-ed by the caller
static void scan_ctx_init(scan_ctx *ctx, VALUE path_ary)
{
  int path_ary_len;
  paths_t *paths;
  size_t arena_size, arena_off, current_path_off, starts_off, key_arena_off, key_bytes = 0;
  void *arena;
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
        key_bytes = checked_size_add(key_bytes, (size_t)RSTRING_LENINT(entry));
#else
        key_bytes = checked_size_add(key_bytes, (size_t)RSTRING_LEN(entry));
#endif
        break;
      case T_FIXNUM:
      case T_BIGNUM:
        NUM2LONG(entry);
        break;
      default:
      {
        VALUE range_beg, range_end;
        long end_val;
        int open_ended;
        if (rb_range_values(entry, &range_beg, &range_end, &open_ended) != Qtrue)
          rb_raise(rb_eArgError, "path elements must be strings, integers, or ranges");
        if (range_beg != any_key_sym || range_end != any_key_sym)
        {
          if (NUM2LONG(range_beg) < 0L)
            rb_raise(rb_eArgError, "range start must be positive");
          end_val = NUM2LONG(range_end);
          if (end_val < -1L)
            rb_raise(rb_eArgError, "range end must be positive or -1");
          if (end_val == -1L && open_ended)
            rb_raise(rb_eArgError, "range with -1 end must be closed");
        }
      }
      }
    }
  }

  ctx->max_path_len = 0;

  arena_size = checked_size_mul(path_ary_len, sizeof(paths_t));
  for (int i = 0; i < path_ary_len; i++)
  {
    int path_len = rb_long2int(rb_array_len(rb_ary_entry(path_ary, i)));
    if (path_len > ctx->max_path_len)
      ctx->max_path_len = path_len;
    arena_size = checked_size_add(arena_size, checked_size_mul(path_len, sizeof(path_matcher_elem_t)));
  }
  current_path_off = arena_size;
  arena_size = checked_size_add(arena_size, checked_size_mul(ctx->max_path_len, sizeof(path_elem_t)));
  starts_off = arena_size;
  arena_size = checked_size_add(arena_size, checked_size_mul((size_t)ctx->max_path_len + 1, sizeof(size_t)));
  key_arena_off = arena_size;
  arena_size = checked_size_add(arena_size, key_bytes);
  arena = ruby_xmalloc(arena_size);
  arena_off = 0;

  paths = (paths_t *)arena;
  arena_off = checked_size_mul(path_ary_len, sizeof(paths_t));
  // Assign ctx->paths early so ruby_xfree(ctx->paths) will free the arena
  // if a Ruby exception happens during the population loop below
  ctx->paths = paths;
  ctx->paths_len = 0;
  ctx->current_path = NULL;
  ctx->starts = NULL;
  for (int i = 0; i < path_ary_len; i++)
  {
    int path_len;
    VALUE path = rb_ary_entry(path_ary, i);
    path_len = rb_long2int(rb_array_len(path));
    paths[i].elems = (path_matcher_elem_t *)((unsigned char *)arena + arena_off);
    arena_off = checked_size_add(arena_off, checked_size_mul(path_len, sizeof(path_matcher_elem_t)));
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
        char *key_dst = (char *)arena + key_arena_off;
        paths[i].elems[j].type = MATCHER_KEY;
#if LONG_MAX > SIZE_MAX
        paths[i].elems[j].value.key.len = RSTRING_LENINT(entry);
#else
        paths[i].elems[j].value.key.len = RSTRING_LEN(entry);
#endif
        paths[i].elems[j].value.key.val = key_dst;
        memcpy(key_dst, RSTRING_PTR(entry), paths[i].elems[j].value.key.len);
        key_arena_off = checked_size_add(key_arena_off, paths[i].elems[j].value.key.len);
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
          paths[i].elems[j].value.range.start = NUM2LONG(range_beg);
          paths[i].elems[j].value.range.end = NUM2LONG(range_end);
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
  ctx->current_path = (path_elem_t *)((unsigned char *)arena + current_path_off);
  ctx->starts = (size_t *)((unsigned char *)arena + starts_off);
}

typedef struct
{
  scan_ctx *ctx;
  VALUE path_ary;
} scan_ctx_init_args;

static VALUE scan_ctx_init_protected(VALUE arg)
{
  scan_ctx_init_args *args = (scan_ctx_init_args *)arg;
  scan_ctx_init(args->ctx, args->path_ary);
  return Qnil;
}

// resets temporary values in the selector
static void scan_ctx_reset(scan_ctx *ctx, VALUE points_list, VALUE roots_info_list, int with_path, int symbolize_path_keys)
{
  // TODO: reset matched_depth if implemented
  ctx->current_path_len = 0;
  ctx->handle = NULL;
  ctx->yajl_bytes_consumed = 0;
  ctx->points_list = points_list;
  ctx->roots_info_list = roots_info_list;
  ctx->with_path = with_path;
  ctx->symbolize_path_keys = symbolize_path_keys;
}

static void scan_ctx_free(scan_ctx *ctx)
{
  // fprintf(stderr, "scan_ctx_free\n");
  if (!ctx)
    return;
  if (!ctx->paths)
    return;
  ruby_xfree(ctx->paths);
}

// noexcept
static inline void increment_arr_index(scan_ctx *sctx)
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

static VALUE create_point(scan_ctx *sctx, value_type type, size_t length)
{
  VALUE values[3], point;
  size_t curr_pos = scan_ctx_get_bytes_consumed(sctx);
  point = rb_ary_new_capa(3);
  values[1] = ULL2NUM(curr_pos);
  switch (type)
  {
    /* FIXME: size_t can be longer than ulong */
  case null_value:
    values[0] = ULL2NUM(curr_pos - length);
    values[2] = null_sym;
    break;
  case boolean_value:
    values[0] = ULL2NUM(curr_pos - length);
    values[2] = boolean_sym;
    break;
  case number_value:
    values[0] = ULL2NUM(curr_pos - length);
    values[2] = number_sym;
    break;
  case string_value:
    values[0] = ULL2NUM(curr_pos - length);
    values[2] = string_sym;
    break;
  case object_value:
    values[0] = ULL2NUM(sctx->starts[sctx->current_path_len]);
    values[2] = object_sym;
    break;
  case array_value:
    values[0] = ULL2NUM(sctx->starts[sctx->current_path_len]);
    values[2] = array_sym;
    break;
  }
  rb_ary_cat(point, values, 3);
  return point;
}

static VALUE create_path(scan_ctx *sctx)
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
      entry = LONG2NUM(sctx->current_path[i].value.index);
      break;
    default:
      entry = Qnil;
    }
    rb_ary_push(path, entry);
  }
  return path;
}

// noexcept
static inline void save_root_info(scan_ctx *sctx, VALUE type, size_t len)
{
  if (sctx->roots_info_list != Qundef && sctx->current_path_len == 0)
  {
    rb_ary_push(sctx->roots_info_list, rb_ary_new_from_args(2, type, ULL2NUM(scan_ctx_get_bytes_consumed(sctx) - len)));
  }
}

// noexcept
static void save_point(scan_ctx *sctx, value_type type, size_t length)
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
            memcmp(sctx->current_path[j].value.key.val, sctx->paths[i].elems[j].value.key.val, sctx->current_path[j].value.key.len))
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
        point = create_point(sctx, type, length);
        if (sctx->with_path)
        {
          path = create_path(sctx);
          point = rb_ary_new_from_args(2, path, point);
        }
      }
      rb_ary_push(rb_ary_entry(sctx->points_list, i), point);
    }
  }
}

// noexcept
static int scan_on_null(void *ctx)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  save_root_info(sctx, null_sym, 4);
  if (sctx->current_path_len > sctx->max_path_len)
    return true;
  increment_arr_index(sctx);
  save_point(sctx, null_value, 4);
  return true;
}

// noexcept
static int scan_on_boolean(void *ctx, int bool_val)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  save_root_info(sctx, boolean_sym, bool_val ? 4 : 5);
  if (sctx->current_path_len > sctx->max_path_len)
    return true;
  increment_arr_index(sctx);
  save_point(sctx, boolean_value, bool_val ? 4 : 5);
  return true;
}

// noexcept
static int scan_on_number(void *ctx, const char *val, size_t len)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  save_root_info(sctx, number_sym, len);
  if (sctx->current_path_len > sctx->max_path_len)
    return true;
  increment_arr_index(sctx);
  save_point(sctx, number_value, len);
  return true;
}

// noexcept
static int scan_on_string(void *ctx, const unsigned char *val, size_t len)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  size_t source_len = scan_ctx_get_string_length(sctx);
  (void)val;
  (void)len;
  save_root_info(sctx, string_sym, source_len);
  if (sctx->current_path_len > sctx->max_path_len)
    return true;
  increment_arr_index(sctx);
  save_point(sctx, string_value, source_len);
  return true;
}

// noexcept
static int scan_on_start_object(void *ctx)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  // Save in the beginning in case of a partial value
  save_root_info(sctx, object_sym, 1);
  if (sctx->current_path_len > sctx->max_path_len)
  {
    sctx->current_path_len++;
    return true;
  }
  increment_arr_index(sctx);
  sctx->starts[sctx->current_path_len] = scan_ctx_get_bytes_consumed(sctx) - 1;
  if (sctx->current_path_len < sctx->max_path_len)
    sctx->current_path[sctx->current_path_len].type = PATH_KEY;
  sctx->current_path_len++;
  return true;
}

// noexcept
static int scan_on_key(void *ctx, const unsigned char *key, size_t len)
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
static int scan_on_end_object(void *ctx)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  sctx->current_path_len--;
  if (sctx->current_path_len <= sctx->max_path_len)
    save_point(sctx, object_value, 0);
  return true;
}

// noexcept
static int scan_on_start_array(void *ctx)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  // Save in the beginning in case of a partial value
  save_root_info(sctx, array_sym, 1);
  if (sctx->current_path_len > sctx->max_path_len)
  {
    sctx->current_path_len++;
    return true;
  }
  increment_arr_index(sctx);
  sctx->starts[sctx->current_path_len] = scan_ctx_get_bytes_consumed(sctx) - 1;
  if (sctx->current_path_len < sctx->max_path_len)
  {
    sctx->current_path[sctx->current_path_len].type = PATH_INDEX;
    sctx->current_path[sctx->current_path_len].value.index = -1;
  }
  sctx->current_path_len++;
  return true;
}

// noexcept
static int scan_on_end_array(void *ctx)
{
  scan_ctx *sctx = (scan_ctx *)ctx;
  sctx->current_path_len--;
  if (sctx->current_path_len <= sctx->max_path_len)
    save_point(sctx, array_value, 0);
  return true;
}

static void selector_free(void *data)
{
  scan_ctx_free((scan_ctx *)data);
  ruby_xfree(data);
}

static size_t selector_size(const void *data)
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
      for (int j = 0; j < ctx->paths[i].len; j++)
      {
        if (ctx->paths[i].elems[j].type == MATCHER_KEY)
          res += ctx->paths[i].elems[j].value.key.len;
      }
    }
  }
  return res;
}

static const rb_data_type_t selector_type = {
    .wrap_struct_name = "json_scanner_selector",
    .function = {
        .dfree = selector_free,
        .dsize = selector_size,
    },
    .flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

static VALUE selector_alloc(VALUE self)
{
  scan_ctx *ctx = ruby_xmalloc(sizeof(scan_ctx));
  ctx->paths = NULL;
  ctx->paths_len = 0;
  ctx->current_path = NULL;
  ctx->max_path_len = 0;
  ctx->starts = NULL;
  scan_ctx_reset(ctx, Qundef, Qundef, false, false);
  return TypedData_Wrap_Struct(self, &selector_type, ctx);
}

static VALUE selector_m_initialize(VALUE self, VALUE path_ary)
{
  scan_ctx *ctx;
  TypedData_Get_Struct(self, scan_ctx, &selector_type, ctx);
  if (ctx->paths)
    rb_raise(rb_eRuntimeError, "selector is already initialized");
  scan_ctx_init(ctx, path_ary);
  return self;
}

static VALUE selector_m_inspect(VALUE self)
{
  scan_ctx *ctx;
  VALUE res;
  TypedData_Get_Struct(self, scan_ctx, &selector_type, ctx);
  res = rb_sprintf("#<%" PRIsVALUE " [", rb_class_name(CLASS_OF(self)));
  for (int i = 0; ctx->paths && i < ctx->paths_len; i++)
  {
    rb_str_buf_cat_ascii(res, "[");
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
        rb_str_catf(res, "(%ld..%ld)", ctx->paths[i].elems[j].value.range.start, ctx->paths[i].elems[j].value.range.end == LONG_MAX ? -1L : ctx->paths[i].elems[j].value.range.end);
        break;
      case MATCHER_ANY_KEY:
        rb_str_buf_cat_ascii(res, "('*'..'*')");
        break;
      }
      if (j < ctx->paths[i].len - 1)
        rb_str_buf_cat_ascii(res, ", ");
    }
    rb_str_buf_cat_ascii(res, "]");
    if (i < ctx->paths_len - 1)
      rb_str_buf_cat_ascii(res, ", ");
  }
  rb_str_buf_cat_ascii(res, "]>");
  return res;
}

static VALUE selector_m_length(VALUE self)
{
  scan_ctx *ctx;
  TypedData_Get_Struct(self, scan_ctx, &selector_type, ctx);
  return INT2FIX(ctx->paths_len);
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
  // NOT INITIALIZED
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
  if (SCAN_OPTION_IS_SET(options, with_path))
    rb_str_catf(res, "with_path: %s, ", SCAN_OPTION(options, with_path) ? "true" : "false");
  if (SCAN_OPTION_IS_SET(options, verbose_error))
    rb_str_catf(res, "verbose_error: %s, ", SCAN_OPTION(options, verbose_error) ? "true" : "false");
  if (SCAN_OPTION_IS_SET(options, allow_comments))
    rb_str_catf(res, "allow_comments: %s, ", SCAN_OPTION(options, allow_comments) ? "true" : "false");
  if (SCAN_OPTION_IS_SET(options, dont_validate_strings))
    rb_str_catf(res, "dont_validate_strings: %s, ", SCAN_OPTION(options, dont_validate_strings) ? "true" : "false");
  if (SCAN_OPTION_IS_SET(options, allow_trailing_garbage))
    rb_str_catf(res, "allow_trailing_garbage: %s, ", SCAN_OPTION(options, allow_trailing_garbage) ? "true" : "false");
  if (SCAN_OPTION_IS_SET(options, allow_multiple_values))
    rb_str_catf(res, "allow_multiple_values: %s, ", SCAN_OPTION(options, allow_multiple_values) ? "true" : "false");
  if (SCAN_OPTION_IS_SET(options, allow_partial_values))
    rb_str_catf(res, "allow_partial_values: %s, ", SCAN_OPTION(options, allow_partial_values) ? "true" : "false");
  if (SCAN_OPTION_IS_SET(options, symbolize_path_keys))
    rb_str_catf(res, "symbolize_path_keys: %s, ", SCAN_OPTION(options, symbolize_path_keys) ? "true" : "false");
  if (SCAN_OPTION_IS_SET(options, with_roots_info))
    rb_str_catf(res, "with_roots_info: %s, ", SCAN_OPTION(options, with_roots_info) ? "true" : "false");
  if (RSTRING_END(res)[-1] == ' ')
    rb_str_resize(res, RSTRING_LEN(res) - 2);
  rb_str_buf_cat_ascii(res, "}>");
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

typedef struct
{
  yajl_handle handle;
  const unsigned char *json_text;
  size_t json_text_len;
  scan_ctx *ctx;
  int verbose;
} parse_args;

typedef struct
{
  yajl_handle handle;
  unsigned char *message;
} error_message_args;

static VALUE build_error_message(VALUE arg)
{
  error_message_args *args = (error_message_args *)arg;
  return rb_utf8_str_new_cstr(args->message ? (char *)args->message : "unknown yajl error");
}

static VALUE free_error_message(VALUE arg)
{
  error_message_args *args = (error_message_args *)arg;
  if (args->message)
    yajl_free_error(args->handle, args->message);
  return Qnil;
}

static VALUE parse_and_check(VALUE arg)
{
  parse_args *args = (parse_args *)arg;
  yajl_status status = yajl_parse(args->handle, args->json_text, args->json_text_len);
  if (status == yajl_status_ok)
  {
    scan_ctx_save_bytes_consumed(args->ctx);
    status = yajl_complete_parse(args->handle);
  }
  if (status != yajl_status_ok)
  {
    error_message_args error_args = {
        args->handle,
        yajl_get_error(args->handle, args->verbose, args->json_text, args->json_text_len)};
    VALUE message = rb_ensure(build_error_message, (VALUE)&error_args, free_error_message, (VALUE)&error_args);
    VALUE exception = rb_exc_new_str(rb_eJsonScannerParseError, message);
    rb_ivar_set(exception, rb_iv_bytes_consumed, ULL2NUM(scan_ctx_get_bytes_consumed(args->ctx)));
    rb_exc_raise(exception);
  }
  return Qnil;
}

// def scan(json_str, path_arr, opts)
// opts
// with_path: false, verbose_error: false, symbolize_path_keys: false, with_roots_info: false
// the following opts converted to bool and passed to yajl_config if provided, ignored if not provided
// allow_comments, dont_validate_strings, allow_trailing_garbage, allow_multiple_values, allow_partial_values
static VALUE scan(int argc, VALUE *argv, VALUE self)
{
  VALUE json_str, path_ary, rb_options;
  scan_options options;

  char *json_text;
  size_t json_text_len;
  yajl_handle handle;
  scan_ctx *ctx;
  int free_ctx = true;
  VALUE result, roots_info_result = Qundef;
  rb_scan_args(argc, argv, "21", &json_str, &path_ary, &rb_options);
  rb_check_type(json_str, T_STRING);
  // rb_io_write(rb_stderr, rb_sprintf("with_path_flag: %" PRIsVALUE " \n", with_path_flag));
  switch (TYPE(rb_options))
  {
  case T_HASH:
  case T_NIL:
    scan_options_init(&options, rb_options);
    break;
  case T_DATA:
    if (rb_obj_is_kind_of(rb_options, rb_cJsonScannerOptions))
    {
      scan_options *ptr;
      TypedData_Get_Struct(rb_options, scan_options, &options_type, ptr);
      options = *ptr;
    }
    else
    {
      rb_raise(rb_eTypeError, "Expected a Hash or %" PRIsVALUE ", got %" PRIsVALUE, rb_cJsonScannerOptions, rb_obj_class(rb_options));
    }
    break;
  default:
    rb_raise(rb_eTypeError, "Expected a Hash or %" PRIsVALUE ", got %" PRIsVALUE, rb_cJsonScannerOptions, rb_obj_class(rb_options));
    break;
  }
  if (SCAN_OPTION(&options, with_roots_info))
    roots_info_result = rb_ary_new();
  json_text = RSTRING_PTR(json_str);
#if LONG_MAX > SIZE_MAX
  json_text_len = RSTRING_LENINT(json_str);
#else
  json_text_len = RSTRING_LEN(json_str);
#endif
  if (rb_obj_is_kind_of(path_ary, rb_cJsonScannerSelector))
  {
    free_ctx = false;
    TypedData_Get_Struct(path_ary, scan_ctx, &selector_type, ctx);
  }
  else
  {
    scan_ctx_init_args init_args;
    int init_state;
    ctx = ruby_xmalloc(sizeof(scan_ctx));
    ctx->paths = NULL;
    init_args.ctx = ctx;
    init_args.path_ary = path_ary;
    rb_protect(scan_ctx_init_protected, (VALUE)&init_args, &init_state);
    if (init_state)
    {
      scan_ctx_free(ctx);
      ruby_xfree(ctx);
      rb_jump_tag(init_state);
    }
  }
  // Need to keep a ref to result array on the stack to prevent it from being GC-ed
  result = rb_ary_new_capa(ctx->paths_len);
  for (int i = 0; i < ctx->paths_len; i++)
  {
    rb_ary_push(result, rb_ary_new());
  }
  scan_ctx_reset(ctx, result, roots_info_result, SCAN_OPTION(&options, with_path), SCAN_OPTION(&options, symbolize_path_keys));
  ctx->json_text = (const unsigned char *)json_text;
  // scan_ctx_debug(ctx);

  handle = yajl_alloc(&scan_callbacks, NULL, (void *)ctx);
  if (!handle)
  {
    if (free_ctx)
    {
      scan_ctx_free(ctx);
      ruby_xfree(ctx);
    }
    rb_raise(rb_eNoMemError, "failed to allocate yajl handle");
  }
  if (SCAN_OPTION_IS_SET(&options, allow_comments))
    yajl_config(handle, yajl_allow_comments, SCAN_OPTION(&options, allow_comments));
  if (SCAN_OPTION_IS_SET(&options, dont_validate_strings))
    yajl_config(handle, yajl_dont_validate_strings, SCAN_OPTION(&options, dont_validate_strings));
  if (SCAN_OPTION_IS_SET(&options, allow_trailing_garbage))
    yajl_config(handle, yajl_allow_trailing_garbage, SCAN_OPTION(&options, allow_trailing_garbage));
  if (SCAN_OPTION_IS_SET(&options, allow_multiple_values))
    yajl_config(handle, yajl_allow_multiple_values, SCAN_OPTION(&options, allow_multiple_values));
  if (SCAN_OPTION_IS_SET(&options, allow_partial_values))
    yajl_config(handle, yajl_allow_partial_values, SCAN_OPTION(&options, allow_partial_values));
  ctx->handle = handle;
  {
    parse_args args = {
        handle,
        (const unsigned char *)json_text,
        json_text_len,
        ctx,
        SCAN_OPTION(&options, verbose_error)};
    int parse_state;
    rb_protect(parse_and_check, (VALUE)&args, &parse_state);
    if (parse_state)
    {
      /* Ruby exception from callbacks or parse error;
       * clean up yajl + ctx before re-raising */
      if (free_ctx)
      {
        scan_ctx_free(ctx);
        ruby_xfree(ctx);
      }
      yajl_free(handle);
      rb_jump_tag(parse_state);
    }
  }
  if (free_ctx)
  {
    scan_ctx_free(ctx);
    ruby_xfree(ctx);
  }
  yajl_free(handle);
  if (roots_info_result != Qundef)
  {
    result = rb_ary_new_from_args(2, result, roots_info_result);
  }
  return result;
}

RUBY_FUNC_EXPORTED void
Init_json_scanner(void)
{
  rb_mJsonScanner = rb_define_module("JsonScanner");
  rb_cJsonScannerSelector = rb_define_class_under(rb_mJsonScanner, "Selector", rb_cObject);
  rb_define_alloc_func(rb_cJsonScannerSelector, selector_alloc);
  rb_define_method(rb_cJsonScannerSelector, "initialize", selector_m_initialize, 1);
  rb_define_method(rb_cJsonScannerSelector, "inspect", selector_m_inspect, 0);
  rb_define_method(rb_cJsonScannerSelector, "length", selector_m_length, 0);
  rb_define_alias(rb_cJsonScannerSelector, "size", "length");
  rb_cJsonScannerOptions = rb_define_class_under(rb_mJsonScanner, "Options", rb_cObject);
  rb_define_alloc_func(rb_cJsonScannerOptions, options_alloc);
  rb_define_method(rb_cJsonScannerOptions, "initialize", options_m_initialize, -1);
  rb_define_method(rb_cJsonScannerOptions, "inspect", options_m_inspect, 0);
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
  scan_kwargs_table[8] = rb_intern("with_roots_info");
}
