# frozen_string_literal: true

source "https://rubygems.org"

# Specify your gem's dependencies in json_scanner.gemspec
gemspec

gem "rake", "~> 13.0"

gem "rake-compiler", "~> 1.2"

gem "rspec", "~> 3.0"

gem "rubocop", RUBY_VERSION >= "2.7" ? "~> 1.69" : "~> 0.81.0"

gem "rubocop-rspec", RUBY_VERSION >= "2.7" ? "~> 3.3" : "~> 1.38", require: false

# No requirement, but uses syntax from 2.6

gem "ruby_memcheck", "~> 2.3" if RUBY_VERSION >= "2.7"

pry_version, pry_byebug_version = if RUBY_VERSION < "2.7"
                                    ["~> 0.12.2", "~> 3.6.0"]
                                  elsif RUBY_VERSION >= "3.4"
                                    ["~> 0.15.2", "~> 3.11"]
                                  else
                                    ["~> 0.14.2", "~> 3.10.1"]
                                  end
gem "pry", pry_version
gem "pry-byebug", pry_byebug_version

# benchmarks
if RUBY_VERSION >= "2.7"
  gem "benchmark", "~> 0.4.1"
  gem "benchmark-ips", "~> 2.14"
  gem "benchmark-memory", "~> 0.2.0"
  gem "json", "~> 2.13"
  gem "oj", "~> 3.16"
  # for ':stats => :bootstrap'
  # gem "kalibera", "~> 0.1.2"
  gem "activesupport", "~> 7.1"
  # gem "json-next", "~> 1.2"
  gem "rainbow", "~> 3.1"
  # gem "rb_json5", "~> 0.3.0" - extremely slow
  gem "ffi-yajl", "~> 3.0"
  gem "simdjson", "~> 1.0"
  gem "yaji", "~> 0.3.6"
  gem "yajl-ffi", "~> 1.0"
  gem "yajl-ruby", "~> 1.4"
end

gem "libyajl2", "~> 2.1"
