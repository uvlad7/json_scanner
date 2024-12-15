# JsonScanner

Extract values from JSON without full parsing. This gem uses yajl lib to scan a json string and allows you to parse pieces of it.

## Installation

Install the gem and add to the application's Gemfile by executing:

    $ bundle add json_scanner

If bundler is not being used to manage dependencies, install the gem by executing:

    $ gem install json_scanner

## Usage

```ruby
require 'json'
require 'json_scanner'

large_json = "[#{'4,' * 100_000 }42#{',2' * 100_000 }]"
where_is_42 = JsonScanner.scan(large_json, [[100_000]], false).first
# => [[200001, 200003, :number]]
where_is_42.map { |begin_pos, end_pos, _type| JSON.parse(large_json[begin_pos...end_pos], quirks_mode: true) }
# => [42]
```

## Development

After checking out the repo, run `bin/setup` to install dependencies. Then, run `rake spec` to run the tests. You can also run `bin/console` for an interactive prompt that will allow you to experiment.

To install this gem onto your local machine, run `bundle exec rake install`. To release a new version, update the version number in `version.rb`, and then run `bundle exec rake release`, which will create a git tag for the version, push git commits and the created tag, and push the `.gem` file to [rubygems.org](https://rubygems.org).

## Contributing

Bug reports and pull requests are welcome on GitHub at https://github.com/uvlad7/json_scanner.

## License

The gem is available as open source under the terms of the [MIT License](https://opensource.org/licenses/MIT).
