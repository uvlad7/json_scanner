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
where_is_42 = JsonScanner.scan(large_json, [[100_000]], false).first
# => [[200001, 200003, :number]]
where_is_42.map do |begin_pos, end_pos, _type|
  JSON.parse(large_json.byteslice(begin_pos...end_pos), quirks_mode: true)
end
# => [42]

emoji_json = '{"grin": "😁", "heart": "😍", "rofl": "🤣"}'
begin_pos, end_pos, = JsonScanner.scan(emoji_json, [["heart"]], false).first.first
emoji_json.byteslice(begin_pos...end_pos)
# => "\"😍\""
# Note: You most likely don't need the `quirks_mode` option unless you are using an older version
# of Ruby with the stdlib - or just also old - version of the json gem. In newer versions, `quirks_mode` is enabled by default.
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
JsonScanner.scan("{\"\x81\x83\": 42}", [[JsonScanner::ANY_KEY]], dont_validate_strings: true, with_path: true)
# => [[[["\x81\x83"], [7, 9, :number]]]]
JsonScanner.scan('[0, 42, 0]garbage', [[(1..-1)]], allow_trailing_garbage: true)
# => [[[4, 6, :number], [8, 9, :number]]]
JsonScanner.scan('[0, 42, 0]  [0, 34]', [[(1..-1)]], allow_multiple_values: true)
# => [[[4, 6, :number], [8, 9, :number], [16, 18, :number]]]
JsonScanner.scan('[0, 42, 0', [[(1..-1)]], allow_partial_values: true)
# => [[[4, 6, :number], [8, 9, :number]]]
JsonScanner.scan('{"a": 1}', [[JsonScanner::ANY_KEY]], with_path: true, symbolize_path_keys: true)
# => [[[[:a], [6, 7, :number]]]]
```

### Comments in the JSON

Note that the standard `JSON` library supports comments, so you may want to enable it in the `JsonScanner` as well
```ruby
json_str = '{"answer": {"value": 42 /* the Ultimate Question of Life, the Universe, and Everything */ }}'
JsonScanner.scan(json_str, [["answer"]], allow_comments: true).first.map do |begin_pos, end_pos, _type|
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
json_end_pos = JsonScanner.scan(json_with_trailing_garbage, [[]], allow_trailing_garbage: true).first.first[1]
apollo_state = JSON.parse(json_with_trailing_garbage[0...json_end_pos])
```

## Reuse configuration

You can create a `JsonScanner::Config` instance and reuse it between `JsonScanner.scan` calls

```ruby
require "json_scanner"

config = JsonScanner::Config.new([[], ["key"], [(0..-1)]])
# => #<JsonScanner::Config [[], ['key'], [(0..9223372036854775807)]]>
JsonScanner.scan('{"key": "42"}', config)
# => [[[0, 13, :object]], [[8, 12, :string]], []]
JsonScanner.scan('{"key": "42"}', config, with_path: true)
# => [[[[], [0, 13, :object]]], [[["key"], [8, 12, :string]]], []]
JsonScanner.scan('[0, 42]', config)
# => [[[0, 7, :array]], [], [[1, 2, :number], [4, 6, :number]]]
JsonScanner.scan('[0, 42]', config, with_path: true)
# => [[[[], [0, 7, :array]]], [], [[[0], [1, 2, :number]], [[1], [4, 6, :number]]]]
```

Options can be passed as a hash, even on Ruby 3
```ruby
options = { allow_trailing_garbage: true, allow_partial_values: true }
JsonScanner.scan('[0, 42', [[1]], options) == JsonScanner.scan('[0, 42]_', [[1]], options)
# => true
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
