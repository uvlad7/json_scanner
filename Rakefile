# frozen_string_literal: true

require "bundler/gem_tasks"
require "rspec/core/rake_task"

RSpec::Core::RakeTask.new(spec: :compile)

require "rubocop/rake_task"

RuboCop::RakeTask.new

require "rake/extensiontask"
require_relative "spec/extensiontesttask"

task build: :compile
task package: :build
task pkg: :build

GEMSPEC = Gem::Specification.load("json_scanner.gemspec")

Rake::ExtensionTestTask.new("json_scanner", GEMSPEC) do |ext|
  ext.lib_dir = "lib/json_scanner"
  # https://karottenreibe.github.io/2009/10/30/ruby-c-extension-7/
  ext.c_spec_files = FileList["spec/**{,/*/**}/*_spec.c"]
end

task default: %i[clobber compile spec rubocop]

if RUBY_VERSION >= "2.6"
  require "ruby_memcheck"
  require "ruby_memcheck/rspec/rake_task"

  RubyMemcheck.config(skipped_ruby_functions: %w[rb_utf8_str_new_cstr rb_exc_new_str rb_exc_raise])
  namespace :spec do
    RubyMemcheck::RSpec::RakeTask.new(valgrind: :compile)
  end
end
