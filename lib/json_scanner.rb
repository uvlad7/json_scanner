# frozen_string_literal: true

require_relative "json_scanner/version"
require_relative "json_scanner/json_scanner"

require "json"

# Extract values from JSON without full parsing. This gem uses the +yajl+ library
#   to scan a JSON string and allows you to parse pieces of it.
module JsonScanner
  class Error < StandardError; end

  ALLOWED_OPTS = %i[verbose_error allow_comments dont_validate_strings allow_multiple_values
                    allow_trailing_garbage allow_partial_values symbolize_path_keys].freeze
  private_constant :ALLOWED_OPTS
  STUB = :stub
  private_constant :STUB

  def self.parse(json_str, config_or_path_ary, **opts)
    # with_path and with_roots_info is set here
    unless (extra_opts = opts.keys - ALLOWED_OPTS).empty?
      raise ArgumentError, "unknown keyword#{extra_opts.size > 1 ? "s" : ""}: #{extra_opts.map(&:inspect).join(", ")}"
    end

    results, roots = scan(json_str, config_or_path_ary, **opts, with_path: true, with_roots_info: true)

    res = process_results(json_str, results, roots, opts[:symbolize_path_keys])

    opts[:allow_multiple_values] ? res : res.first
  end

  def self.process_results(json_str, results, roots, symbolize_names)
    # stubs are symbols, so they can be distinguished from real values
    res = roots.map(&:first)
    # results for different path matchers can overlap, in that case we will simply parse more than one time,
    # but there shouln't be any surprises in the behavior
    results.each do |result|
      process_result(res, result, roots, json_str, symbolize_names)
    end
    res
  end

  private_class_method :process_results

  def self.process_result(res, result, roots, json_str, symbolize_names)
    current_root_index = 0
    next_root = roots[1]
    result.each do |path, (begin_pos, end_pos, _type)|
      while next_root && begin_pos >= next_root[1]
        current_root_index += 1
        next_root = roots[current_root_index + 1]
      end

      # for 'res[index]' check inside insert_value
      res[current_root_index] = nil if res[current_root_index].is_a?(Symbol)
      insert_value(res, parse_value(json_str, begin_pos, end_pos, symbolize_names), current_root_index, path)
    end
  end

  private_class_method :process_result

  def self.parse_value(json_str, begin_pos, end_pos, symbolize_names)
    # TODO: opts for JSON.parse
    JSON.parse(
      json_str.byteslice(begin_pos...end_pos),
      quirks_mode: true, symbolize_names: symbolize_names,
    )
  end

  private_class_method :parse_value

  def self.insert_value(res, parsed_value, index, path)
    until path.empty?
      new_index = path.shift
      res[index] ||= new_index.is_a?(Integer) ? [] : {}
      res = res[index]
      index = new_index
    end
    res[index] = parsed_value
  end

  private_class_method :insert_value
end
