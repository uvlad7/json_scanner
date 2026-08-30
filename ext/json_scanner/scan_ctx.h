#ifndef JSON_SCANNER_SCAN_CTX_H
#define JSON_SCANNER_SCAN_CTX_H 1

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
  const char *val;
  size_t len;
  int owned;
} path_key_t;

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
    path_key_t key;
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
  size_t paths_arena_size;
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
  size_t json_text_len;
} scan_ctx;

#endif /* JSON_SCANNER_SCAN_CTX_H */

