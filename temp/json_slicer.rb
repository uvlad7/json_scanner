require 'ffi'

module YajlFFI
  extend ::FFI::Library

  ffi_lib ['yajl', 'libyajl.so.2']

  enum :status, [
         :ok,
         :client_canceled,
         :error,
       ]

  enum :options, [
         :allow_comments, 0x01,
         :allow_invalid_utf8, 0x02,
         :allow_trailing_garbage, 0x04,
         :allow_multiple_values, 0x08,
         :allow_partial_values, 0x10,
       ]

  class Callbacks < ::FFI::Struct
    layout \
      :on_null, :pointer,
      :on_boolean, :pointer,
      :on_integer, :pointer,
      :on_double, :pointer,
      :on_number, :pointer,
      :on_string, :pointer,
      :on_start_object, :pointer,
      :on_key, :pointer,
      :on_end_object, :pointer,
      :on_start_array, :pointer,
      :on_end_array, :pointer
  end

  typedef :pointer, :handle

  attach_function :alloc, :yajl_alloc, [Callbacks.ptr, :pointer, :pointer], :handle
  attach_function :free, :yajl_free, [:handle], :void
  attach_function :config, :yajl_config, [:handle, :options, :varargs], :int
  attach_function :parse, :yajl_parse, [:handle, :pointer, :size_t], :status
  attach_function :complete_parse, :yajl_complete_parse, [:handle], :status
  attach_function :get_error, :yajl_get_error, [:handle, :int, :pointer, :size_t], :pointer
  attach_function :free_error, :yajl_free_error, [:handle, :pointer], :void
  attach_function :get_bytes_consumed, :yajl_get_bytes_consumed, [:handle], :size_t
  # Not required
  # attach_function :status_to_string, :yajl_status_to_string, [:status], :string
  # Not available in the latest yajl 2.1.0
  # https://github.com/lloyd/yajl/commit/646b8b82ce5441db3d11b98a1049e1fcb50fe776
  # So it's not possible to reset the parser and reuse it.
  # attach_function :reset, :yajl_reset, [:handle], :void

  CONTINUE_PARSE = 1
  STOP_PARSE = 0
end

class JsonSlicer
  # Raised on any invalid JSON text.
  ParserError = Class.new(RuntimeError)
  # Constant to select all elements in an array
  ALL = (0..Float::INFINITY).freeze

  # As we cannot reset the parser, we need to ensure that it's not reused
  # Still more convenient to use an instance than class methods

  def self.slice(data, path, **options)
    raise ArgumentError, 'Only binary strings are supported' unless data.is_a?(String) && data.encoding == Encoding::ASCII_8BIT

    self.new(path, options).parse(data)
  end

  def parse(data)
    @data = data
    # See example https://lloyd.github.io/yajl/
    stat = YajlFFI.parse(@handle, @data, @data.bytesize)
    stat = YajlFFI.complete_parse(@handle) if stat == :ok
    if stat == :error # client_canceled is ok
      pointer = YajlFFI.get_error(@handle, 1, @data, @data.bytesize)
      message = pointer.read_string
      YajlFFI.free_error(@handle, pointer)
      raise ParserError, message
    end

    @points
  end

  private

  def initialize(path, options)
    @path = path
    @target_depth = path.size - 1
    @indices = Array.new(@target_depth + 1)
    @current_depth = -1
    @matched_depth = -1
    @points = []
    initialize_native(options)
  end

  def save_point?
    # We are at the last level of the path and it matches
    @current_depth == @target_depth && @matched_depth == @target_depth
  end

  # Called on the end of the current value processing
  def update_index
    # nil indicates that we are inside an object or too deep and don't care about indices
    # or @matched_depth was less that @target_depth when we started an array
    if @indices[@current_depth]
      @indices[@current_depth] += 1
      # @matched_depth is at least @current_depth - 1, we only check if @indices[@current_depth] match
      @matched_depth = @path[@current_depth] === @indices[@current_depth] ? @current_depth : @current_depth - 1
    end
  end

  # Build a native Callbacks struct and allocate a yajl parser handle.
  #
  # The functions registered in the struct are invoked by the native yajl
  # parser.
  #
  # The struct instance must be stored in an
  # instance variable. This prevents the FFI::Function objects from being
  # garbage collected while the parser is still in use. The native function
  # bindings need to be collected at the same time as the Parser instance.
  def initialize_native(options)
    @callbacks = YajlFFI::Callbacks.new

    @callbacks[:on_null] = ::FFI::Function.new(:int, [:pointer]) do |ctx|
      @points << [YajlFFI::get_bytes_consumed(@handle) - 4, 4, :null] if save_point?
      update_index
      YajlFFI::CONTINUE_PARSE
    end
    @callbacks[:on_boolean] = ::FFI::Function.new(:int, [:pointer, :int]) do |ctx, value|
      if save_point?
        bool_value = value == 1
        value_len = bool_value ? 4 : 5
        @points << [YajlFFI::get_bytes_consumed(@handle) - value_len, value_len, :boolean, bool_value]
      end
      update_index
      YajlFFI::CONTINUE_PARSE
    end
    @callbacks[:on_integer] = nil
    @callbacks[:on_double] = nil
    # https://github.com/lloyd/yajl/blob/master/src/api/yajl_parse.h#L81
    @callbacks[:on_number] = ::FFI::Function.new(:int, [:pointer, :pointer, :size_t]) do |ctx, value, length|
      @points << [YajlFFI::get_bytes_consumed(@handle) - length, length, :number] if save_point?
      update_index
      YajlFFI::CONTINUE_PARSE
    end
    @callbacks[:on_string] = ::FFI::Function.new(:int, [:pointer, :pointer, :size_t]) do |ctx, value, length|
      @points << [YajlFFI::get_bytes_consumed(@handle) - 2 - length, length + 2, :string] if save_point?
      update_index
      YajlFFI::CONTINUE_PARSE
    end

    # These callbacks modify current path of the parser.

    @callbacks[:on_start_object] = ::FFI::Function.new(:int, [:pointer]) do |ctx|
      # Saved boundaries cannot intersect, so it's safe to use only one variable
      # But we need to check if we are going to save that, to not overwrite the value already saved
      # and to avoid unnecessary function calls
      @start = YajlFFI::get_bytes_consumed(@handle) - 1 if save_point?
      @current_depth += 1
      YajlFFI::CONTINUE_PARSE
    end

    @callbacks[:on_key] = ::FFI::Function.new(:int, [:pointer, :pointer, :size_t]) do |ctx, key, length|
      if @current_depth - 1 <= @matched_depth
        # key_val = key.read_string(length)
        # I believe it's cheaper than read native string and convert it to ruby
        # The only disadvantage = we need to keep whole data in memory,
        # but it shouldn't be a problem, as the caller needs to keep it anyway to use offsets later
        # and current interface doesn't allow streamed data process
        key_val = @data[YajlFFI::get_bytes_consumed(@handle) - length - 1, length]
        @matched_depth = @path[@current_depth] === key_val ? @current_depth : @current_depth - 1
      end
      YajlFFI::CONTINUE_PARSE
    end

    @callbacks[:on_end_object] = ::FFI::Function.new(:int, [:pointer]) do |ctx|
      @matched_depth -= 1 if @matched_depth == @current_depth
      @current_depth -= 1
      @points << [@start, YajlFFI::get_bytes_consumed(@handle) - @start, :object] if save_point?
      update_index
      YajlFFI::CONTINUE_PARSE
    end

    @callbacks[:on_start_array] = ::FFI::Function.new(:int, [:pointer]) do |ctx|
      @start = YajlFFI::get_bytes_consumed(@handle) - 1 if save_point?
      @current_depth += 1
      @indices[@current_depth] = -1 if @current_depth <= @target_depth && @current_depth - 1 == @matched_depth
      update_index
      YajlFFI::CONTINUE_PARSE
    end

    @callbacks[:on_end_array] = ::FFI::Function.new(:int, [:pointer]) do |ctx|
      @matched_depth -= 1 if @matched_depth == @current_depth
      @indices[@current_depth] = nil if @current_depth <= @target_depth
      @current_depth -= 1
      @points << [@start, YajlFFI::get_bytes_consumed(@handle) - @start, :array] if save_point?
      update_index
      YajlFFI::CONTINUE_PARSE
    end

    # Must not be used after @callbacks is freed: @callbacks must outlive @handle.
    # TODO: Find a way to tie their lifetimes together.
    # Check with
    # # @callbacks = nil
    # # GC.stress = true
    @handle = ::FFI::AutoPointer.new(YajlFFI.alloc(@callbacks, nil, nil), method(:release_native))
    options.each do |key, value|
      rv = YajlFFI.config(@handle, key, :int, value ? 1 : 0)
      raise ArgumentError, "Invalid option: #{key}" if rv == 0
    end
  end

  # Free the memory held by a yajl parser handle previously allocated
  # with Yajl::FFI.alloc.
  #
  # It's not sufficient to just allow the handle pointer to be freed
  # normally because it contains pointers that must also be freed. The
  # native yajl API provides a `yajl_free` function for this purpose.
  #
  # This method is invoked by the FFI::AutoPointer, wrapping the yajl
  # parser handle, when it's garbage collected by Ruby.
  #
  # pointer - The FFI::Pointer that references the native yajl parser.
  #
  # Returns nothing.
  def release_native(pointer)
    YajlFFI.free(pointer)
  end
end

require 'json'
data = '{"foo": {"bar": ["baz", 42, 42.2, false, [42], {"a": "b"}]} /* comment */}______'
data.force_encoding(Encoding::ASCII_8BIT)
JsonSlicer.slice(
  data,
  ['foo', 'bar', JsonSlicer::ALL],
  allow_trailing_garbage: true,
  allow_invalid_utf8: true,
  allow_comments: true,
).each do |start, len, type, val|
  p JSON.parse(data[start, len], quirks_mode: true)
end

puts

data = '[[[1, 2], [3, 4]], [[5, 6], [7, 8, 9, 10]]]'
data.force_encoding(Encoding::ASCII_8BIT)
JsonSlicer.slice(
  data,
  [JsonSlicer::ALL, 1, (0..2)],
).each do |start, len, type, val|
  p data[start, len]
end
