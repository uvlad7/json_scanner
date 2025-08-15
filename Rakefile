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

    json_str = File.read("spec/graphql_response.json")
    json_path = %i[data search searchResult paginationV2 maxPage]
    json_selector = JsonScanner::Config.new([json_path])

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

    results = [
      page_size_with_json.call,
      page_size_with_oj.call,
      page_size_with_json_scanner_scan.call,
      page_size_with_json_scanner_parse.call,
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
      type == :memory ? x.compare!(memory: :allocated) : x.compare!
    end.curry

    Benchmark.ips(&benchmark[:ips])
    Benchmark.memory(&benchmark[:memory])
    puts Rainbow("=" * title.size).color(136, 17, 2)
    puts "\n\n\n"
  end
  # rubocop:enable Metrics/BlockLength
end

# ========= JSON string size: 463 KB =========
# path :data, :search, :searchResult, :paginationV2, :maxPage; extracted values: [8, 8, 8, 8]
# ruby 2.7.8p225 (2023-03-30 revision 1f4d455848) [x86_64-linux]
# Warming up --------------------------------------
#                 json     9.000 i/100ms
#                   oj    18.000 i/100ms
#    json_scanner scan    70.000 i/100ms
#   json_scanner parse    74.000 i/100ms
# Calculating -------------------------------------
#                 json     93.566 (± 2.1%) i/s   (10.69 ms/i) -    468.000 in   5.004734s
#                   oj    188.098 (± 5.3%) i/s    (5.32 ms/i) -    954.000 in   5.088179s
#    json_scanner scan    740.777 (± 3.1%) i/s    (1.35 ms/i) -      3.710k in   5.013415s
#   json_scanner parse    738.147 (± 2.2%) i/s    (1.35 ms/i) -      3.700k in   5.014863s

# Comparison:
#    json_scanner scan:      740.8 i/s
#   json_scanner parse:      738.1 i/s - same-ish: difference falls within error
#                   oj:      188.1 i/s - 3.94x  slower
#                 json:       93.6 i/s - 7.92x  slower

# Calculating -------------------------------------
#                 json     1.987M memsize (     0.000  retained)
#                         26.302k objects (     0.000  retained)
#                         50.000  strings (     0.000  retained)
#                   oj     1.379M memsize (     0.000  retained)
#                          9.665k objects (     0.000  retained)
#                         50.000  strings (     0.000  retained)
#    json_scanner scan   368.000  memsize (     0.000  retained)
#                          6.000  objects (     0.000  retained)
#                          1.000  strings (     0.000  retained)
#   json_scanner parse     2.240k memsize (   528.000  retained)
#                         23.000  objects (     9.000  retained)
#                          1.000  strings (     0.000  retained)

# Comparison:
#    json_scanner scan:        368 allocated
#   json_scanner parse:       2240 allocated - 6.09x more
#                   oj:    1379274 allocated - 3748.03x more
#                 json:    1987269 allocated - 5400.19x more
# ============================================

# ========= JSON string size: 463 KB =========
# path :data, :search, :searchResult, :paginationV2, :maxPage; extracted values: [8, 8, 8, 8]
# ruby 3.2.2 (2023-03-30 revision e51014f9c0) [x86_64-linux]
# Warming up --------------------------------------
#                 json    15.000 i/100ms
#                   oj    17.000 i/100ms
#    json_scanner scan    74.000 i/100ms
#   json_scanner parse    73.000 i/100ms
# Calculating -------------------------------------
#                 json    156.015 (± 4.5%) i/s    (6.41 ms/i) -    780.000 in   5.009398s
#                   oj    146.395 (±19.1%) i/s    (6.83 ms/i) -    714.000 in   5.085658s
#    json_scanner scan    513.451 (±29.4%) i/s    (1.95 ms/i) -      2.368k in   5.046488s
#   json_scanner parse    640.176 (±16.2%) i/s    (1.56 ms/i) -      3.139k in   5.067958s

# Comparison:
#   json_scanner parse:      640.2 i/s
#    json_scanner scan:      513.5 i/s - same-ish: difference falls within error
#                 json:      156.0 i/s - 4.10x  slower
#                   oj:      146.4 i/s - 4.37x  slower

# Calculating -------------------------------------
#                 json     1.402M memsize (     0.000  retained)
#                         10.184k objects (     0.000  retained)
#                         50.000  strings (     0.000  retained)
#                   oj     1.441M memsize (   168.000  retained)
#                          9.665k objects (     1.000  retained)
#                         50.000  strings (     0.000  retained)
#    json_scanner scan   368.000  memsize (     0.000  retained)
#                          6.000  objects (     0.000  retained)
#                          1.000  strings (     0.000  retained)
#   json_scanner parse     2.072k memsize (   360.000  retained)
#                         22.000  objects (     8.000  retained)
#                          1.000  strings (     0.000  retained)

# Comparison:
#    json_scanner scan:        368 allocated
#   json_scanner parse:       2072 allocated - 5.63x more
#                 json:    1401815 allocated - 3809.28x more
#                   oj:    1440774 allocated - 3915.15x more
# ============================================

# ========= JSON string size: 463 KB =========
# path :data, :search, :searchResult, :paginationV2, :maxPage; extracted values: [8, 8, 8, 8]
# ruby 3.4.2 (2025-02-15 revision d2930f8e7a) +PRISM [x86_64-linux]
# Warming up --------------------------------------
#                 json    11.000 i/100ms
#                   oj    12.000 i/100ms
#    json_scanner scan    68.000 i/100ms
#   json_scanner parse    75.000 i/100ms
# Calculating -------------------------------------
#                 json    153.145 (± 9.1%) i/s    (6.53 ms/i) -    759.000 in   5.003997s
#                   oj    161.947 (± 3.7%) i/s    (6.17 ms/i) -    816.000 in   5.045710s
#    json_scanner scan    733.917 (± 1.9%) i/s    (1.36 ms/i) -      3.672k in   5.005395s
#   json_scanner parse    719.953 (± 2.1%) i/s    (1.39 ms/i) -      3.600k in   5.002508s

# Comparison:
#    json_scanner scan:      733.9 i/s
#   json_scanner parse:      720.0 i/s - same-ish: difference falls within error
#                   oj:      161.9 i/s - 4.53x  slower
#                 json:      153.1 i/s - 4.79x  slower

# Calculating -------------------------------------
#                 json     1.377M memsize (     0.000  retained)
#                         10.184k objects (     0.000  retained)
#                         50.000  strings (     0.000  retained)
#                   oj     1.363M memsize (     0.000  retained)
#                          9.665k objects (     0.000  retained)
#                         50.000  strings (     0.000  retained)
#    json_scanner scan   360.000  memsize (     0.000  retained)
#                          6.000  objects (     0.000  retained)
#                          1.000  strings (     0.000  retained)
#   json_scanner parse     2.000k memsize (   360.000  retained)
#                         22.000  objects (     8.000  retained)
#                          1.000  strings (     0.000  retained)

# Comparison:
#    json_scanner scan:        360 allocated
#   json_scanner parse:       2000 allocated - 5.56x more
#                   oj:    1363382 allocated - 3787.17x more
#                 json:    1376519 allocated - 3823.66x more
# ============================================
