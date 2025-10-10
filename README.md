[![Tests](https://github.com/uvlad7/json_scanner/actions/workflows/main.yml/badge.svg)](https://github.com/uvlad7/json_scanner/actions/workflows/main.yml)

# JsonScanner

Extract values from JSON without full parsing. This gem uses the `yajl` library to scan a JSON string and allows you to parse pieces of it.

## Installation

Install the gem and add to the application's Gemfile by executing:

    $ bundle add json_scanner

If bundler is not being used to manage dependencies, install the gem by executing:

    $ gem install json_scanner

This gem relies on the [yajl](https://github.com/lloyd/yajl) library and needs its development headers being installed. On Defian and Ubuntu you can install thel with the following command:

    $ sudo apt install libyajl2 libyajl-dev

You can also use `libyajl2` gem to obtain the library. Install it before installing `json_scanner`:

    $ gem install libyajl2 -v '2.1.0'

and then install the gem by executing:

    $ gem install json_scanner --with-libyajl2-gem

If bundler is being used, you need to ensure the `libyajl2` gem is installed before it installs `json_scanner`:

- add the following into your Gemfile:
```ruby
group :libyajl do
  gem "libyajl2", "~> 2.1"
end
```
- configure `with-libyajl2-gem` flag:
```
$ bundle config --local build.json_scanner --with-libyajl2-gem
```
- install the group:
```
# if you placed json_scanner into a group, replace 'default' with its name
$ BUNDLE_WITHOUT='default' BUNDLE_WITH='libyajl2' bundle install
```
- now you can install the rest as usual:
```
$ bundle install
```
## Usage

Basic usage

```ruby
require "json"
require "json_scanner"

large_json = "[#{"4," * 100_000}42#{",2" * 100_000}]"
where_is_42 = JsonScanner.scan(large_json, [[100_000]]).first
# => [[200001, 200003, :number]]
where_is_42.map do |begin_pos, end_pos, _type|
  JSON.parse(large_json.byteslice(begin_pos...end_pos), quirks_mode: true)
end
# => [42]

# You can supply multiple paths to retrieve different values; each path can match multiple results,
# values may overlap if needed
question_path = ["data", "question"]
answers_path = ["data", "answers", (0..-1)]
json_str = '{"data": {"question": "the Ultimate Question of Life, the Universe, and Everything", "answers": [42, 0, 420]}}'
(question_pos, ), answers_pos = JsonScanner.scan(json_str, [question_path, answers_path])
question = JSON.parse(json_str.byteslice(question_pos[0]...question_pos[1]), quirks_mode: true)
# => "the Ultimate Question of Life, the Universe, and Everything"
answers = answers_pos.map do |begin_pos, end_pos, _type|
  JSON.parse(json_str.byteslice(begin_pos...end_pos), quirks_mode: true)
end
# => [42, 0, 420]

# Result contains byte offsets, you need to be careful when working with non-binary strings
emoji_json = '{"grin": "😁", "heart": "😍", "rofl": "🤣"}'
begin_pos, end_pos, = JsonScanner.scan(emoji_json, [["heart"]]).first.first
emoji_json.byteslice(begin_pos...end_pos)
# => "\"😍\""
# Note: You most likely don't need the `quirks_mode` option unless you are using
#  an older version of Ruby with the stdlib - or just also old - version of the json gem.
# In newer versions, `quirks_mode` is enabled by default.
JSON.parse(emoji_json.byteslice(begin_pos...end_pos), quirks_mode: true)
# => "😍"
# You can also do this
# emoji_json.force_encoding(Encoding::BINARY)[begin_pos...end_pos].force_encoding(Encoding::UTF_8)
# => "\"😍\""

# Ranges are supported as matchers for indexes with the following restrictions:
# - the start of a range must be positive
# - the end of a range must be positive or -1
# - a range with -1 end must be closed, e.g. (0..-1) works, but (0...-1) is forbidden
JsonScanner.scan('[0, 42, 0]', [[(1..-1)]])
# => [[[4, 6, :number], [8, 9, :number]]]
JsonScanner.scan('[0, 42, 0]', [[JsonScanner::ANY_INDEX]])
# => [[[1, 2, :number], [4, 6, :number], [8, 9, :number]]]

# Special matcher JsonScanner::ANY_KEY is supported for object keys
JsonScanner.scan('{"a": 1, "b": 2}', [[JsonScanner::ANY_KEY]], with_path: true)
# => [[[["a"], [6, 7, :number]], [["b"], [14, 15, :number]]]]
# Regex mathers aren't supported yet, but you can simulate it using `with_path` option
JsonScanner.scan(
  '{"question1": 1, "answer": 42, "question2": 2}',
  [[JsonScanner::ANY_KEY]], with_path: true,
).map do |res|
  res.map do |path, (begin_pos, end_pos, type)|
    [begin_pos, end_pos, type] if path[0] =~ /\Aquestion/
  end.compact
end
# => [[[14, 15, :number], [44, 45, :number]]]
```

## Options

`JsonScanner` supports multiple options

```ruby
JsonScanner.scan('[0, 42, 0]', [[(1..-1)]], with_path: true)
# => [[[[1], [4, 6, :number]], [[2], [8, 9, :number]]]]
JsonScanner.scan('[0, 42],', [[(1..-1)]], verbose_error: true)
# JsonScanner::ParseError (parse error: trailing garbage)
#                                 [0, 42],
#                      (right here) ------^
# Note: the 'right here' pointer is wrong in case of a premature EOF error, it's a bug of the libyajl
JsonScanner.scan('[0, 42,', [[(1..-1)]], verbose_error: true)
# JsonScanner::ParseError (parse error: premature EOF)
#                                        [0, 42,
#                      (right here) ------^
JsonScanner.scan('[0, /* answer */ 42, 0]', [[(1..-1)]], allow_comments: true)
# => [[[17, 19, :number], [21, 22, :number]]]
JsonScanner.scan("\"\x81\x83\"", [[]], dont_validate_strings: true)
# => [[[0, 4, :string]]]
JsonScanner.scan(
  "{\"\x81\x83\": 42}", [[JsonScanner::ANY_KEY]],
  dont_validate_strings: true, with_path: true.
)
# => [[[["\x81\x83"], [7, 9, :number]]]]
JsonScanner.scan('[0, 42, 0]garbage', [[(1..-1)]], allow_trailing_garbage: true)
# => [[[4, 6, :number], [8, 9, :number]]]
JsonScanner.scan('[0, 42, 0]  [0, 34]', [[(1..-1)]], allow_multiple_values: true)
# => [[[4, 6, :number], [8, 9, :number], [16, 18, :number]]]
JsonScanner.scan('[0, 42, 0', [[(1..-1)]], allow_partial_values: true)
# => [[[4, 6, :number], [8, 9, :number]]]
JsonScanner.scan(
  '{"a": 1}', [[JsonScanner::ANY_KEY]],
  with_path: true, symbolize_path_keys: true,
)
# => [[[[:a], [6, 7, :number]]]]
JsonScanner.scan('[42]42{"a":42} true', [], allow_multiple_values: true, with_roots_info: true).last
# => [[:array, 0], [:number, 4], [:object, 6], [:boolean, 15]]
```

### Comments in the JSON

Note that the standard `JSON` library supports comments, so you may want to enable it in the `JsonScanner` as well
```ruby
json_str = '{"answer": {"value": 42 /* the Ultimate Question of Life, the Universe, and Everything */ }}'
JsonScanner.scan(
  json_str, [["answer"]], allow_comments: true.
).first.map do |begin_pos, end_pos, _type|
  JSON.parse(json_str.byteslice(begin_pos...end_pos), quirks_mode: true)
end
# => [{"value"=>42}]
```

### Find the end of a JSON string

`allow_trailing_garbage` option may come in handy if you want to extract a JSON string from a JS text
```ruby
script_text = <<~'JS'
      <script>window.__APOLLO_STATE__={"ContentItem:0":{"__typename":"ContentItem","id":0, "configurationType":"NO_CONFIGURATION","replacementPartsUrl":null,"relatedCategories":[{"__ref":"Category:109450"},{"__ref":"Category:82044355"},{"__ref":"Category:109441"},{"__ref":"Category:109442"},{"__ref":"Category:109449"},{"__ref":"Category:109444"},{"__ref":"Category:82043730"}],"recommendedOptions":[]}};window.__APPVERSION__=7018;window.__CONFIG_ENV__={value: 'PRODUCTION'};</script>
JS
json_with_trailing_garbage = script_text[/__APOLLO_STATE__\s*=\s*({.+)/, 1]
json_end_pos = JsonScanner.scan(
  json_with_trailing_garbage, [[]], allow_trailing_garbage: true,
).first.first[1]
apollo_state = JSON.parse(json_with_trailing_garbage[0...json_end_pos])
```

## Reuse configuration

You can create a `JsonScanner::Selector` instance and reuse it between `JsonScanner.scan` calls

```ruby
require "json_scanner"

selector = JsonScanner::Selector.new([[], ["key"], [(0..-1)]])
# => #<JsonScanner::Selector [[], ['key'], [(0..-1)]]>
JsonScanner.scan('{"key": "42"}', selector)
# => [[[0, 13, :object]], [[8, 12, :string]], []]
JsonScanner.scan('{"key": "42"}', selector, with_path: true)
# => [[[[], [0, 13, :object]]], [[["key"], [8, 12, :string]]], []]
JsonScanner.scan('[0, 42]', selector)
# => [[[0, 7, :array]], [], [[1, 2, :number], [4, 6, :number]]]
JsonScanner.scan('[0, 42]', selector, with_path: true)
# => [[[[], [0, 7, :array]]], [], [[[0], [1, 2, :number]], [[1], [4, 6, :number]]]]
```

Configuration options can be passed as a hash, even on Ruby 3
```ruby
options = { allow_trailing_garbage: true, allow_partial_values: true }
JsonScanner.scan('[0, 42', [[1]], options) == JsonScanner.scan('[0, 42]_', [[1]], options)
# => true
```

Alternatively, you can use `JsonScanner::Options` not to parse a hash every time
```ruby
require 'benchmark'

options = { allow_trailing_garbage: true, allow_partial_values: true }
scanner_options = JsonScanner::Options.new(options)
# => #<JsonScanner::Options {allow_trailing_garbage: true, allow_partial_values: true}>
json_str = '{"question1": 1, "answer": 42, "question2": 2}'
selector = JsonScanner::Selector.new([['answer']])
Benchmark.bmbm do |x|
  x.report("options") { 1_000_000.times { JsonScanner.scan(json_str, selector, options) } }
  x.report("scanner_options") { 1_000_000.times { JsonScanner.scan(json_str, selector, scanner_options) } }
end
# produces the following on Ruby 3.2.2
# Rehearsal ---------------------------------------------------
# options           0.957705   0.002293   0.959998 (  0.960228)
# scanner_options   0.903800   0.003121   0.906921 (  0.906956)
# ------------------------------------------ total: 1.866919sec

#                       user     system      total        real
# options           0.929433   0.001102   0.930535 (  0.931687)
# scanner_options   0.907289   0.005055   0.912344 (  0.912379)
```

## Streaming mode

Streaming mode isn't supported yet, as it's harder to implement and to use. I plan to add it in the future, its API is a subject to discussion. If you have suggestions, use cases, or preferences for how it should behave, I’d love to hear from you!

## Development

After checking out the repo, run `bin/setup` to install dependencies. Then, run `rake spec` to run the tests. You can also run `bin/console` for an interactive prompt that will allow you to experiment.

To install this gem onto your local machine, run `bundle exec rake install`. To release a new version, update the version number in `version.rb`, and then run `bundle exec rake release`, which will create a git tag for the version, push git commits and the created tag, and push the `.gem` file to [rubygems.org](https://rubygems.org).

## Contributing

Bug reports and pull requests are welcome on GitHub at [github](https://github.com/uvlad7/json_scanner).

## License

The gem is available as open source under the terms of the [MIT License](https://opensource.org/licenses/MIT).

## Benchmarks

```shell
========= JSON string size: 463 KB =========
path :data, :search, :searchResult, :paginationV2, :maxPage; extracted values: [8, 8, 8, 8, 8, 8, 8, 8]
ruby 2.7.8p225 (2023-03-30 revision 1f4d455848) [x86_64-linux]
Warming up --------------------------------------
                json     8.000 i/100ms
                  oj    16.000 i/100ms
   json_scanner scan    66.000 i/100ms
  json_scanner parse    44.000 i/100ms
            simdjson    12.000 i/100ms
           yajl-ruby    12.000 i/100ms
                yaji     1.000 i/100ms
            ffi-yajl     9.000 i/100ms
     yajl-ffi (stub)     1.000 i/100ms
Calculating -------------------------------------
                json     89.542 (±20.1%) i/s   (11.17 ms/i) -    432.000 in   5.083579s
                  oj    155.916 (±29.5%) i/s    (6.41 ms/i) -    688.000 in   5.033501s
   json_scanner scan    661.426 (±10.7%) i/s    (1.51 ms/i) -      3.300k in   5.059478s
  json_scanner parse    649.443 (±12.0%) i/s    (1.54 ms/i) -      3.212k in   5.045073s
            simdjson    156.793 (±11.5%) i/s    (6.38 ms/i) -    780.000 in   5.053348s
           yajl-ruby    120.428 (± 5.0%) i/s    (8.30 ms/i) -    612.000 in   5.096770s
                yaji     18.486 (± 0.0%) i/s   (54.09 ms/i) -     93.000 in   5.034553s
            ffi-yajl    126.109 (± 9.5%) i/s    (7.93 ms/i) -    630.000 in   5.059638s
     yajl-ffi (stub)     13.726 (±21.9%) i/s   (72.85 ms/i) -     66.000 in   5.028068s

Comparison:
   json_scanner scan:      661.4 i/s
  json_scanner parse:      649.4 i/s - same-ish: difference falls within error
            simdjson:      156.8 i/s - 4.22x  slower
                  oj:      155.9 i/s - 4.24x  slower
            ffi-yajl:      126.1 i/s - 5.24x  slower
           yajl-ruby:      120.4 i/s - 5.49x  slower
                json:       89.5 i/s - 7.39x  slower
                yaji:       18.5 i/s - 35.78x  slower
     yajl-ffi (stub):       13.7 i/s - 48.19x  slower

Calculating -------------------------------------
                json     1.987M memsize (     0.000  retained)
                        26.302k objects (     0.000  retained)
                        50.000  strings (     0.000  retained)
                  oj     1.379M memsize (     0.000  retained)
                         9.665k objects (     0.000  retained)
                        50.000  strings (     0.000  retained)
   json_scanner scan   368.000  memsize (     0.000  retained)
                         6.000  objects (     0.000  retained)
                         1.000  strings (     0.000  retained)
  json_scanner parse     2.240k memsize (   528.000  retained)
                        23.000  objects (     9.000  retained)
                         1.000  strings (     0.000  retained)
            simdjson     2.203M memsize (     0.000  retained)
                        30.038k objects (     0.000  retained)
                        50.000  strings (     0.000  retained)
           yajl-ruby     1.379M memsize (     0.000  retained)
                         9.666k objects (     0.000  retained)
                        50.000  strings (     0.000  retained)
                yaji     8.593M memsize (     0.000  retained)
                       138.847k objects (     0.000  retained)
                        50.000  strings (     0.000  retained)
            ffi-yajl     1.380M memsize (   168.000  retained)
                         9.671k objects (     1.000  retained)
                        50.000  strings (     0.000  retained)
     yajl-ffi (stub)   233.913M memsize (   208.000  retained)
                        98.406k objects (     2.000  retained)
                        50.000  strings (     1.000  retained)

Comparison:
   json_scanner scan:        368 allocated
  json_scanner parse:       2240 allocated - 6.09x more
                  oj:    1379274 allocated - 3748.03x more
           yajl-ruby:    1379442 allocated - 3748.48x more
            ffi-yajl:    1380058 allocated - 3750.16x more
                json:    1987269 allocated - 5400.19x more
            simdjson:    2202607 allocated - 5985.35x more
                yaji:    8592765 allocated - 23349.90x more
     yajl-ffi (stub):  233913419 allocated - 635634.29x more
============================================


========= JSON string size: 463 KB =========
path :data, :search, :searchResult, :paginationV2, :maxPage; extracted values: [8, 8, 8, 8, 8, 8, 8, 8]
ruby 3.2.2 (2023-03-30 revision e51014f9c0) [x86_64-linux]
Warming up --------------------------------------
                json    15.000 i/100ms
                  oj    17.000 i/100ms
   json_scanner scan    66.000 i/100ms
  json_scanner parse    74.000 i/100ms
            simdjson     4.000 i/100ms
           yajl-ruby    11.000 i/100ms
                yaji     2.000 i/100ms
            ffi-yajl     9.000 i/100ms
     yajl-ffi (stub)     1.000 i/100ms
Calculating -------------------------------------
                json    157.797 (± 4.4%) i/s    (6.34 ms/i) -    795.000 in   5.050469s
                  oj    163.243 (± 7.4%) i/s    (6.13 ms/i) -    816.000 in   5.027023s
   json_scanner scan    630.410 (± 7.6%) i/s    (1.59 ms/i) -      3.168k in   5.060172s
  json_scanner parse    684.963 (± 3.1%) i/s    (1.46 ms/i) -      3.478k in   5.082918s
            simdjson     44.316 (± 0.0%) i/s   (22.57 ms/i) -    224.000 in   5.055158s
           yajl-ruby    109.968 (± 1.8%) i/s    (9.09 ms/i) -    550.000 in   5.002486s
                yaji     27.216 (± 3.7%) i/s   (36.74 ms/i) -    138.000 in   5.073578s
            ffi-yajl    115.785 (± 4.3%) i/s    (8.64 ms/i) -    585.000 in   5.064551s
     yajl-ffi (stub)     14.820 (±13.5%) i/s   (67.48 ms/i) -     73.000 in   5.007391s

Comparison:
  json_scanner parse:      685.0 i/s
   json_scanner scan:      630.4 i/s - same-ish: difference falls within error
                  oj:      163.2 i/s - 4.20x  slower
                json:      157.8 i/s - 4.34x  slower
            ffi-yajl:      115.8 i/s - 5.92x  slower
           yajl-ruby:      110.0 i/s - 6.23x  slower
            simdjson:       44.3 i/s - 15.46x  slower
                yaji:       27.2 i/s - 25.17x  slower
     yajl-ffi (stub):       14.8 i/s - 46.22x  slower

Calculating -------------------------------------
                json     1.402M memsize (     0.000  retained)
                        10.184k objects (     0.000  retained)
                        50.000  strings (     0.000  retained)
                  oj     1.441M memsize (   168.000  retained)
                         9.665k objects (     1.000  retained)
                        50.000  strings (     0.000  retained)
   json_scanner scan   368.000  memsize (     0.000  retained)
                         6.000  objects (     0.000  retained)
                         1.000  strings (     0.000  retained)
  json_scanner parse     2.072k memsize (   360.000  retained)
                        22.000  objects (     8.000  retained)
                         1.000  strings (     0.000  retained)
            simdjson     2.379M memsize (     0.000  retained)
                        30.037k objects (     0.000  retained)
                        50.000  strings (     0.000  retained)
           yajl-ruby     1.441M memsize (     0.000  retained)
                         9.666k objects (     0.000  retained)
                        50.000  strings (     0.000  retained)
                yaji    10.230M memsize (     0.000  retained)
                       138.847k objects (     0.000  retained)
                        50.000  strings (     0.000  retained)
            ffi-yajl     1.441M memsize (     0.000  retained)
                         9.671k objects (     0.000  retained)
                        50.000  strings (     0.000  retained)
     yajl-ffi (stub)   234.105M memsize (   208.000  retained)
                        98.509k objects (     2.000  retained)
                        50.000  strings (     1.000  retained)

Comparison:
   json_scanner scan:        368 allocated
  json_scanner parse:       2072 allocated - 5.63x more
                json:    1401815 allocated - 3809.28x more
                  oj:    1440774 allocated - 3915.15x more
           yajl-ruby:    1440814 allocated - 3915.26x more
            ffi-yajl:    1441310 allocated - 3916.60x more
            simdjson:    2378766 allocated - 6464.04x more
                yaji:   10230271 allocated - 27799.65x more
     yajl-ffi (stub):  234104932 allocated - 636154.71x more
============================================


========= JSON string size: 463 KB =========
path :data, :search, :searchResult, :paginationV2, :maxPage; extracted values: [8, 8, 8, 8, 8, 8, 8, 8]
ruby 3.4.2 (2025-02-15 revision d2930f8e7a) +PRISM [x86_64-linux]
Warming up --------------------------------------
                json    11.000 i/100ms
                  oj    14.000 i/100ms
   json_scanner scan    70.000 i/100ms
  json_scanner parse    69.000 i/100ms
            simdjson     4.000 i/100ms
           yajl-ruby     9.000 i/100ms
                yaji     2.000 i/100ms
            ffi-yajl     9.000 i/100ms
     yajl-ffi (stub)     1.000 i/100ms
Calculating -------------------------------------
                json    130.231 (± 3.8%) i/s    (7.68 ms/i) -    660.000 in   5.076796s
                  oj    145.239 (± 6.9%) i/s    (6.89 ms/i) -    728.000 in   5.047231s
   json_scanner scan    626.381 (± 9.3%) i/s    (1.60 ms/i) -      3.150k in   5.079929s
  json_scanner parse    667.710 (± 4.5%) i/s    (1.50 ms/i) -      3.381k in   5.076284s
            simdjson     37.137 (±10.8%) i/s   (26.93 ms/i) -    184.000 in   5.025192s
           yajl-ruby     98.301 (± 6.1%) i/s   (10.17 ms/i) -    495.000 in   5.054221s
                yaji     25.146 (± 4.0%) i/s   (39.77 ms/i) -    126.000 in   5.026175s
            ffi-yajl    105.936 (± 3.8%) i/s    (9.44 ms/i) -    531.000 in   5.022619s
     yajl-ffi (stub)     13.615 (±14.7%) i/s   (73.45 ms/i) -     67.000 in   5.007215s

Comparison:
  json_scanner parse:      667.7 i/s
   json_scanner scan:      626.4 i/s - same-ish: difference falls within error
                  oj:      145.2 i/s - 4.60x  slower
                json:      130.2 i/s - 5.13x  slower
            ffi-yajl:      105.9 i/s - 6.30x  slower
           yajl-ruby:       98.3 i/s - 6.79x  slower
            simdjson:       37.1 i/s - 17.98x  slower
                yaji:       25.1 i/s - 26.55x  slower
     yajl-ffi (stub):       13.6 i/s - 49.04x  slower

Calculating -------------------------------------
                json     1.377M memsize (     0.000  retained)
                        10.184k objects (     0.000  retained)
                        50.000  strings (     0.000  retained)
                  oj     1.363M memsize (     0.000  retained)
                         9.665k objects (     0.000  retained)
                        50.000  strings (     0.000  retained)
   json_scanner scan   360.000  memsize (     0.000  retained)
                         6.000  objects (     0.000  retained)
                         1.000  strings (     0.000  retained)
  json_scanner parse     2.000k memsize (   360.000  retained)
                        22.000  objects (     8.000  retained)
                         1.000  strings (     0.000  retained)
            simdjson     2.301M memsize (     0.000  retained)
                        30.037k objects (     0.000  retained)
                        50.000  strings (     0.000  retained)
           yajl-ruby     1.364M memsize (     0.000  retained)
                         9.666k objects (     0.000  retained)
                        50.000  strings (     0.000  retained)
                yaji    10.230M memsize (     0.000  retained)
                       138.847k objects (     0.000  retained)
                        50.000  strings (     0.000  retained)
            ffi-yajl     1.364M memsize (    40.000  retained)
                         9.671k objects (     1.000  retained)
                        50.000  strings (     1.000  retained)
     yajl-ffi (stub)   234.105M memsize (   208.000  retained)
                        98.509k objects (     2.000  retained)
                        50.000  strings (     1.000  retained)

Comparison:
   json_scanner scan:        360 allocated
  json_scanner parse:       2000 allocated - 5.56x more
                  oj:    1363382 allocated - 3787.17x more
           yajl-ruby:    1363550 allocated - 3787.64x more
            ffi-yajl:    1364158 allocated - 3789.33x more
                json:    1376519 allocated - 3823.66x more
            simdjson:    2301382 allocated - 6392.73x more
                yaji:   10230263 allocated - 28417.40x more
     yajl-ffi (stub):  234104932 allocated - 650291.48x more
============================================
```
