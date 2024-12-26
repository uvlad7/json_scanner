# frozen_string_literal: true

require "bundler/gem_tasks"
require "rspec/core/rake_task"

RSpec::Core::RakeTask.new(spec: :compile)

require "rubocop/rake_task"

RuboCop::RakeTask.new

require "rake/extensiontask"

task build: :compile
task package: :build
task pkg: :build

GEMSPEC = Gem::Specification.load("json_scanner.gemspec")

Rake::ExtensionTask.new("json_scanner", GEMSPEC) do |ext|
  ext.lib_dir = "lib/json_scanner"
  # https://karottenreibe.github.io/2009/10/30/ruby-c-extension-7/
end

task default: %i[clobber compile spec rubocop]

if RUBY_VERSION >= "2.5"
  require "ruby_memcheck"
  require "ruby_memcheck/rspec/rake_task"

  RubyMemcheck.config(skipped_ruby_functions: ["objspace_malloc_gc_stress"])
  namespace :spec do
    RubyMemcheck::RSpec::RakeTask.new(valgrind: :compile)
  end
end
