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

if RUBY_VERSION >= "2.7"
  require "ruby_memcheck"
  require "ruby_memcheck/rspec/rake_task"

  RubyMemcheck.config(skipped_ruby_functions: %w[rb_utf8_str_new_cstr rb_exc_new_str rb_exc_raise])
  namespace :spec do
    RubyMemcheck::RSpec::RakeTask.new(valgrind: :compile)
  end

  # rubocop:disable Metrics/BlockLength
  task bench: :compile do
    require "benchmark"
    require "benchmark/ips"
    require "benchmark/memory"

    require "active_support"
    require "active_support/number_helper"
    require "rainbow"
    require "json"
    require "oj"
    require "json_scanner"
    require "simdjson"

    json_str = File.read("spec/graphql_response.json")
    json_path = %i[data search searchResult paginationV2 maxPage]
    json_path_str = json_path.map { |p| p.is_a?(Symbol) ? p.to_s : p }
    json_selector = JsonScanner::Selector.new([json_path])

    # TODO: better title display
    puts "\n\n\n"
    json_str_size = ActiveSupport::NumberHelper.number_to_human_size(json_str.bytesize)
    title = "========= JSON string size: #{json_str_size} ========="
    puts Rainbow(title).color(136, 17, 2)
    page_size_with_json = lambda do
      JSON.parse(json_str, symbolize_names: true).dig(*json_path)
    end
    page_size_with_oj = lambda do
      Oj.load(json_str, symbolize_names: true, mode: :object).dig(*json_path)
    end
    page_size_with_json_scanner_scan = lambda do
      JsonScanner.scan(json_str, json_selector).first.first.then do |begin_pos, end_pos, _type|
        JSON.parse(json_str.byteslice(begin_pos...end_pos), quirks_mode: true)
      end
    end
    page_size_with_json_scanner_parse = lambda do
      JsonScanner.parse(json_str, json_selector, symbolize_path_keys: true).dig(*json_path)
    end
    # ondemand parser should be the fastest, but it's not supported by the wrapper
    page_size_with_simdjson = lambda do
      Simdjson.parse(json_str).dig(*json_path_str)
    end

    results = [
      page_size_with_json.call,
      page_size_with_oj.call,
      page_size_with_json_scanner_scan.call,
      page_size_with_json_scanner_parse.call,
      page_size_with_simdjson.call,
    ]
    results_report = "path #{json_path.map(&:inspect).join(", ")}; extracted values: #{results}"
    puts Rainbow(results_report).send(results.uniq.size == 1 ? :green : :red)

    benchmark = lambda do |type, x|
      # not supported by 'memory'
      # x.config(:stats => :bootstrap, :confidence => 95)

      x.report("json", &page_size_with_json)
      x.report("oj", &page_size_with_oj)
      x.report("json_scanner scan", &page_size_with_json_scanner_scan)
      x.report("json_scanner parse", &page_size_with_json_scanner_parse)
      x.report("simdjson", &page_size_with_simdjson)
      type == :memory ? x.compare!(memory: :allocated) : x.compare!
    end.curry

    Benchmark.ips(&benchmark[:ips])
    Benchmark.memory(&benchmark[:memory])
    puts Rainbow("=" * title.size).color(136, 17, 2)
    puts "\n\n\n"
  end
  # rubocop:enable Metrics/BlockLength
end
