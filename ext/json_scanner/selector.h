#ifndef JSON_SCANNER_SELECTOR_H
#define JSON_SCANNER_SELECTOR_H 1

static void scan_ctx_init(scan_ctx *ctx, VALUE path_ary);
static void scan_ctx_reset(scan_ctx *ctx, VALUE points_list, VALUE roots_info_list, int with_path, int symbolize_path_keys);
static void scan_ctx_free(scan_ctx *ctx);
static inline void path_elem_init_key(path_elem_t *elem);

static VALUE rb_cJsonScannerSelector;

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
  res += ctx->paths_arena_size;
  if (ctx->current_path != NULL)
  {
    for (int i = 0; i < ctx->max_path_len; i++)
    {
      if (ctx->current_path[i].type == PATH_KEY && ctx->current_path[i].value.key.owned)
        res += ctx->current_path[i].value.key.len ? ctx->current_path[i].value.key.len : 1;
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
  ctx->paths_arena_size = 0;
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

static void selector_copy_arena(scan_ctx *ctx, const scan_ctx *other_ctx)
{
  void *arena;
  intptr_t arena_offset;

  if (other_ctx->paths == NULL)
    return;
  arena = ruby_xmalloc(other_ctx->paths_arena_size);
  memcpy(arena, other_ctx->paths, other_ctx->paths_arena_size);
  arena_offset = (intptr_t)arena - (intptr_t)other_ctx->paths;
  *ctx = *other_ctx;
  ctx->paths = arena;
  ctx->current_path = (path_elem_t *)((intptr_t)other_ctx->current_path + arena_offset);
  ctx->starts = (size_t *)((intptr_t)other_ctx->starts + arena_offset);
  for (int i = 0; i < ctx->paths_len; i++)
  {
    ctx->paths[i].elems = (path_matcher_elem_t *)((intptr_t)other_ctx->paths[i].elems + arena_offset);
    for (int j = 0; j < ctx->paths[i].len; j++)
    {
      if (ctx->paths[i].elems[j].type == MATCHER_KEY)
        ctx->paths[i].elems[j].value.key.val = (const char *)((intptr_t)other_ctx->paths[i].elems[j].value.key.val + arena_offset);
    }
  }
  for (int i = 0; i < ctx->max_path_len; i++)
    path_elem_init_key(&ctx->current_path[i]);
  scan_ctx_reset(ctx, Qundef, Qundef, false, false);
}

static VALUE selector_m_initialize_copy(VALUE self, VALUE other)
{
  scan_ctx *ctx, *other_ctx;
  TypedData_Get_Struct(self, scan_ctx, &selector_type, ctx);
  if (ctx->paths)
    rb_raise(rb_eRuntimeError, "selector is already initialized");
  rb_call_super(1, &other);
  TypedData_Get_Struct(other, scan_ctx, &selector_type, other_ctx);
  selector_copy_arena(ctx, other_ctx);
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

static void init_selector(VALUE json_scanner_module)
{
  rb_cJsonScannerSelector = rb_define_class_under(json_scanner_module, "Selector", rb_cObject);
  rb_define_alloc_func(rb_cJsonScannerSelector, selector_alloc);
  rb_define_method(rb_cJsonScannerSelector, "initialize", selector_m_initialize, 1);
  rb_define_method(rb_cJsonScannerSelector, "initialize_copy", selector_m_initialize_copy, 1);
  rb_define_method(rb_cJsonScannerSelector, "inspect", selector_m_inspect, 0);
  rb_define_method(rb_cJsonScannerSelector, "length", selector_m_length, 0);
  rb_define_alias(rb_cJsonScannerSelector, "size", "length");
}

#endif /* JSON_SCANNER_SELECTOR_H */
