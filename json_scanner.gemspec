# frozen_string_literal: true

require_relative "lib/json_scanner/version"

Gem::Specification.new do |spec|
  spec.name = "json_scanner"
  spec.version = JsonScanner::VERSION
  spec.authors = ["uvlad7"]
  spec.email = ["uvlad7@gmail.com"]

  spec.summary = "Extract values from JSON without full parsing"
  spec.description = "This gem uses the yajl lib to scan a JSON string and allows you to parse pieces of it"
  spec.homepage = "https://github.com/uvlad7/json_scanner"
  spec.license = "MIT"
  spec.required_ruby_version = ">= 2.3.8"
  spec.requirements = ["libyajl2, v2.1", "libyajl-dev, v2.1"]

  spec.metadata["homepage_uri"] = spec.homepage
  spec.metadata["source_code_uri"] = "https://github.com/uvlad7/json_scanner"
  spec.metadata["changelog_uri"] = "https://github.com/uvlad7/json_scanner/CHANGELOG.md"

  # Specify which files should be added to the gem when it is released.
  # The `git ls-files -z` loads the files in the RubyGem that have been added into git.
  File.basename(__FILE__)
  spec.files = [
    *(Dir["{lib,sig,spec}/**/*"] - Dir["lib/**/*.{so,dylib,dll}"]),
    *Dir["ext/json_scanner/{extconf.rb,json_scanner.c,json_scanner.h}"], "README.md",
  ].reject { |f| File.directory?(f) }
  spec.require_paths = ["lib"]
  spec.extensions = ["ext/json_scanner/extconf.rb"]
  spec.metadata["rubygems_mfa_required"] = "true"
end
