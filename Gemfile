# frozen_string_literal: true

source "https://rubygems.org"

# Specify your gem's dependencies in json_scanner.gemspec
gemspec

gem "rake", "~> 13.0"

gem "rake-compiler", "~> 1.2"

gem "rspec", "~> 3.0"

gem "rubocop", RUBY_VERSION >= "2.7" ? "~> 1.69" : "~> 0.81.0"

gem "rubocop-rspec", RUBY_VERSION >= "2.7" ? "~> 3.3" : "~> 1.38", require: false

# No requirement, but uses syntax from 2.5

gem "ruby_memcheck", "~> 2.3" if RUBY_VERSION >= "2.5"
