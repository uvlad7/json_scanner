fprintf(stderr, "save_point\n");
fprintf(stderr, "current_path_len: %d\n", sctx->current_path_len);
fprintf(stderr, "max_path_len: %d\n", sctx->max_path_len);

fprintf(stderr, "current_path: ");
for (int i = 0; i < sctx->current_path_len; i++)
{
  if (sctx->current_path[i].type == PATH_KEY)
    fprintf(stderr, "%.*s, ", (int)sctx->current_path[i].value.key.len, sctx->current_path[i].value.key.val);
  else
    fprintf(stderr, "%ld, ", sctx->current_path[i].value.index);
}
fprintf(stderr, "\n");

for (int i = 0; i < sctx->paths_len; i++)
{
  fprintf(stderr, "paths[%d]: ", i);
  for (int j = 0; j < sctx->paths[i].len; j++)
  {
    if (sctx->paths[i].elems[j].type == MATCHER_KEY)
      fprintf(stderr, "%.*s, ", (int)sctx->paths[i].elems[j].value.key.len, sctx->paths[i].elems[j].value.key.val);
    else if (sctx->paths[i].elems[j].type == MATCHER_INDEX)
      fprintf(stderr, "%ld, ", sctx->paths[i].elems[j].value.index);
    else
      fprintf(stderr, "%ld..%ld, ", sctx->paths[i].elems[j].value.range.start, sctx->paths[i].elems[j].value.range.end);
  }
  fprintf(stderr, "\n\n");
}
