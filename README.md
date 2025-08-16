[![Tests](https://github.com/uvlad7/json_scanner/actions/workflows/main.yml/badge.svg)](https://github.com/uvlad7/json_scanner/actions/workflows/main.yml)

# JsonScanner

Extract values from JSON without full parsing. This gem uses the `yajl` library to scan a JSON string and allows you to parse pieces of it.

## Installation

Install the gem and add to the application's Gemfile by executing:

    $ bundle add json_scanner

If bundler is not being used to manage dependencies, install the gem by executing:

    $ gem install json_scanner

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
# Or as a positional argument
JsonScanner.scan('[0, 42, 0]', [[(1..-1)]], true)
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
# => #<JsonScanner::Selector [[], ['key'], [(0..9223372036854775807)]]>
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
path :data, :search, :searchResult, :paginationV2, :maxPage; extracted values: [8, 8, 8, 8]
ruby 2.7.8p225 (2023-03-30 revision 1f4d455848) [x86_64-linux]
Warming up --------------------------------------
                json     9.000 i/100ms
                  oj    18.000 i/100ms
   json_scanner scan    70.000 i/100ms
  json_scanner parse    74.000 i/100ms
Calculating -------------------------------------
                json     93.566 (± 2.1%) i/s   (10.69 ms/i) -    468.000 in   5.004734s
                  oj    188.098 (± 5.3%) i/s    (5.32 ms/i) -    954.000 in   5.088179s
   json_scanner scan    740.777 (± 3.1%) i/s    (1.35 ms/i) -      3.710k in   5.013415s
  json_scanner parse    738.147 (± 2.2%) i/s    (1.35 ms/i) -      3.700k in   5.014863s

Comparison:
   json_scanner scan:      740.8 i/s
  json_scanner parse:      738.1 i/s - same-ish: difference falls within error
                  oj:      188.1 i/s - 3.94x  slower
                json:       93.6 i/s - 7.92x  slower

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

Comparison:
   json_scanner scan:        368 allocated
  json_scanner parse:       2240 allocated - 6.09x more
                  oj:    1379274 allocated - 3748.03x more
                json:    1987269 allocated - 5400.19x more
============================================

========= JSON string size: 463 KB =========
path :data, :search, :searchResult, :paginationV2, :maxPage; extracted values: [8, 8, 8, 8]
ruby 3.2.2 (2023-03-30 revision e51014f9c0) [x86_64-linux]
Warming up --------------------------------------
                json    15.000 i/100ms
                  oj    17.000 i/100ms
   json_scanner scan    74.000 i/100ms
  json_scanner parse    73.000 i/100ms
Calculating -------------------------------------
                json    156.015 (± 4.5%) i/s    (6.41 ms/i) -    780.000 in   5.009398s
                  oj    146.395 (±19.1%) i/s    (6.83 ms/i) -    714.000 in   5.085658s
   json_scanner scan    513.451 (±29.4%) i/s    (1.95 ms/i) -      2.368k in   5.046488s
  json_scanner parse    640.176 (±16.2%) i/s    (1.56 ms/i) -      3.139k in   5.067958s

Comparison:
  json_scanner parse:      640.2 i/s
   json_scanner scan:      513.5 i/s - same-ish: difference falls within error
                json:      156.0 i/s - 4.10x  slower
                  oj:      146.4 i/s - 4.37x  slower

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

Comparison:
   json_scanner scan:        368 allocated
  json_scanner parse:       2072 allocated - 5.63x more
                json:    1401815 allocated - 3809.28x more
                  oj:    1440774 allocated - 3915.15x more
============================================

========= JSON string size: 463 KB =========
path :data, :search, :searchResult, :paginationV2, :maxPage; extracted values: [8, 8, 8, 8]
ruby 3.4.2 (2025-02-15 revision d2930f8e7a) +PRISM [x86_64-linux]
Warming up --------------------------------------
                json    11.000 i/100ms
                  oj    12.000 i/100ms
   json_scanner scan    68.000 i/100ms
  json_scanner parse    75.000 i/100ms
Calculating -------------------------------------
                json    153.145 (± 9.1%) i/s    (6.53 ms/i) -    759.000 in   5.003997s
                  oj    161.947 (± 3.7%) i/s    (6.17 ms/i) -    816.000 in   5.045710s
   json_scanner scan    733.917 (± 1.9%) i/s    (1.36 ms/i) -      3.672k in   5.005395s
  json_scanner parse    719.953 (± 2.1%) i/s    (1.39 ms/i) -      3.600k in   5.002508s

Comparison:
   json_scanner scan:      733.9 i/s
  json_scanner parse:      720.0 i/s - same-ish: difference falls within error
                  oj:      161.9 i/s - 4.53x  slower
                json:      153.1 i/s - 4.79x  slower

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

Comparison:
   json_scanner scan:        360 allocated
  json_scanner parse:       2000 allocated - 5.56x more
                  oj:    1363382 allocated - 3787.17x more
                json:    1376519 allocated - 3823.66x more
============================================
```
