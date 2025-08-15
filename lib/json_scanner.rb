# frozen_string_literal: true

require_relative "json_scanner/version"
require_relative "json_scanner/json_scanner"

require "json"

module JsonScanner
  class Error < StandardError; end

  # 2.7.0 :007 > JsonScanner.parse('{"a": 1, "b": [0,1,2], "c": 42} {"b": [1,2,3,4,5,6]} {} 1 []', [["a"], ["b", 4]], allow_multiple_values: true)
  # => [{"a"=>1}, {"b"=>[nil, nil, nil, nil, 5]}, :object, :number, :array]

  def self.parse(json_str, config_or_path_ary, **opts)
    # with_path and with_roots_info is set here
    extra_opts = opts.keys - %i[verbose_error allow_comments dont_validate_strings allow_multiple_values allow_trailing_garbage allow_partial_values symbolize_path_keys]
    unless extra_opts.empty?
      raise ArgumentError, "unknown keyword#{extra_opts.size > 1 ? "s" : ""}: #{extra_opts.map(&:inspect).join(", ")}"
    end

    results, roots = self.scan(json_str, config_or_path_ary, **opts, with_path: true, with_roots_info: true)

    # stubs are symbols, so they can be distinguished from real values
    res = roots.map(&:first)
    # results for different path matchers can overlap, in that case we will simply parse more than one time,
    # but there shouln't be any surprises in the behavior
    results.each do |result|
      current_root_index = 0
      next_root = roots[1]
      result.each do |path, (begin_pos, end_pos, _type)|
        if next_root && begin_pos >= next_root[1]
          current_root_index += 1
          next_root = roots[current_root_index + 1]
        end

        parsed_value = JSON.parse(json_str.byteslice(begin_pos...end_pos), quirks_mode: true, symbolize_names: opts[:symbolize_path_keys])
        # for 'res[index]' check inside insert_value
        res[current_root_index] = nil if res[current_root_index].is_a?(Symbol)
        insert_value(res, parsed_value, current_root_index, path)
      end
    end

    opts[:allow_multiple_values] ? res : res.first
  end

  def self.insert_value(res, parsed_value, index, path)
    until path.empty?
      new_index = path.shift
      res[index] ||= new_index.is_a?(Integer) ? [] : {}
      res = res[index]
      index = new_index
    end
    res[index] = parsed_value
  end
end
