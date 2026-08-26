#ifndef JSON_SCANNER_H
#define JSON_SCANNER_H 1

#include "ruby.h"
#include "ruby/intern.h"
#include "ruby/version.h"
#include <stdint.h>
#include <yajl/yajl_parse.h>
#include <yajl/yajl_gen.h>

#define true 1
#define false 0

static inline size_t checked_size_mul(size_t left, size_t right)
{
  if (left > 0 && right > SIZE_MAX / left)
    rb_memerror();
  return left * right;
}

static inline size_t checked_size_add(size_t left, size_t right)
{
  if (right > SIZE_MAX - left)
    rb_memerror();
  return left + right;
}

#endif /* JSON_SCANNER_H */
