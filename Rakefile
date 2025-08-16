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
    require "yajl"
    require "yaji"
    require "yajl/ffi"
    require "ffi_yajl"

    json_str = File.read("spec/graphql_response.json")
    json_path = %i[data search searchResult paginationV2 maxPage]
    json_path_str = json_path.map { |p| p.is_a?(Symbol) ? p.to_s : p }
    json_selector = JsonScanner::Selector.new([json_path])
    yaji_path = "/#{json_path.map(&:to_s).join("/")}"

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
    page_size_with_yajl_ruby = lambda do
      Yajl::Parser.parse(json_str, symbolize_names: true).dig(*json_path)
    end
    page_size_with_yaji = lambda do
      YAJI::Parser.new(json_str).each(yaji_path).first
    end
    page_size_with_ffi_yajl = lambda do
      FFI_Yajl::Parser.parse(json_str, symbolize_names: true).dig(*json_path)
    end

    yajl_ffi_parser = Yajl::FFI::Parser.new
    # rubocop:disable Lint/EmptyBlock
    yajl_ffi_parser.start_document {}
    yajl_ffi_parser.end_document {}
    yajl_ffi_parser.start_object {}
    yajl_ffi_parser.end_object {}
    yajl_ffi_parser.start_array {}
    yajl_ffi_parser.end_array {}
    yajl_ffi_parser.key { |_k| }
    yajl_ffi_parser.value { |_v| }
    # rubocop:enable Lint/EmptyBlock
    Yajl::FFI.config(yajl_ffi_parser.instance_variable_get(:@handle), :allow_multiple_values, :int, 1)
    # Clone of `<<`, but won't call complete_parse
    def yajl_ffi_parser.push(data)
      result = Yajl::FFI.parse(@handle, data, data.bytesize)
      error(data) if result == :error
    end
    stub_page_size_with_yajl_ffi = lambda do
      # I don't want to reinplement the logic, so callbacks here are empty, it's still
      # slower and consumes more memory.
      # And I even allow mulltiple values not to recreate the parser multiple times.
      yajl_ffi_parser.push(json_str)
    end

    results = [
      page_size_with_json.call,
      page_size_with_oj.call,
      page_size_with_json_scanner_scan.call,
      page_size_with_json_scanner_parse.call,
      page_size_with_simdjson.call,
      page_size_with_yajl_ruby.call,
      page_size_with_yaji.call,
      page_size_with_ffi_yajl.call,
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
      x.report("yajl-ruby", &page_size_with_yajl_ruby)
      x.report("yaji", &page_size_with_yaji)
      x.report("ffi-yajl", &page_size_with_ffi_yajl)
      x.report("yajl-ffi (stub)", &stub_page_size_with_yajl_ffi)
      type == :memory ? x.compare!(memory: :allocated) : x.compare!
    end.curry

    Benchmark.ips(&benchmark[:ips])
    Benchmark.memory(&benchmark[:memory])
    puts Rainbow("=" * title.size).color(136, 17, 2)
    puts "\n\n\n"
  end
  # rubocop:enable Metrics/BlockLength
end
