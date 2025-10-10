# frozen_string_literal: true

require_relative "spec_helper"
require "json"

RSpec.describe JsonScanner do
  it "has a version number" do
    expect(described_class::VERSION).not_to be_nil
  end

  describe ".scan" do
    it "scans json" do
      result = described_class.scan('["1", {"a": 2}]', [[0], [1, "a"], []])
      expect(result).to eq([[[1, 4, :string]], [[12, 13, :number]], [[0, 15, :array]]])
      expect(described_class.scan('"2"', [[]])).to eq([[[0, 3, :string]]])
      expect(
        described_class.scan("[0,1,2,3,4,5,6,7]", [[(0..2)], [(4...6)]]),
      ).to eq(
        [[[1, 2, :number], [3, 4, :number], [5, 6, :number]], [[9, 10, :number], [11, 12, :number]]],
      )
      expect(described_class.scan('{"a": 1}', [["a"], []])).to eq(
        [[[6, 7, :number]], [[0, 8, :object]]],
      )
    end

    it "validates params" do
      expect(described_class.scan("1", [])).to eq([])
      expect(described_class.scan("1", [], nil)).to eq([])
      expect(described_class.scan("1", [], {})).to eq([])
      expect { described_class.scan("1", [], 1) }.to raise_error(TypeError)
    end

    it "supports 'symbolize_path_keys'" do
      expect(
        described_class.scan('{"a": {"b": 1}}', [[:a, "b"]], with_path: true),
      ).to eq([[[%w[a b], [12, 13, :number]]]])
      expect(
        described_class.scan('{"a": {"b": 1}}', [[:a, "b"]], with_path: true, symbolize_path_keys: true),
      ).to eq([[[%i[a b], [12, 13, :number]]]])
    end

    it "supports any key selector" do
      expect(
        described_class.scan(
          '[{"a":1,"b":2},{"c":3,"d":4},[5]]',
          [[described_class::ANY_INDEX, described_class::ANY_KEY]],
        ),
      ).to eq(
        [[[6, 7, :number], [12, 13, :number], [20, 21, :number], [26, 27, :number]]],
      )
      expect(
        described_class.scan(
          '{"a":[1,2],"b":{"c":3}}',
          [[described_class::ANY_KEY, described_class::ANY_INDEX]],
        ),
      ).to eq(
        [[[6, 7, :number], [8, 9, :number]]],
      )
    end

    it "works with max path len correctly" do
      expect(
        described_class.scan('{"a": [1]}', [[], ["a"]]),
      ).to eq(
        [[[0, 10, :object]], [[6, 9, :array]]],
      )
      expect(
        described_class.scan('{"a": {"b": 1}}', [[], ["a"]]),
      ).to eq(
        [[[0, 15, :object]], [[6, 14, :object]]],
      )
      expect(described_class.scan('{"a": 1}', [[]])).to eq([[[0, 8, :object]]])
      expect(described_class.scan("[[1]]", [[]])).to eq([[[0, 5, :array]]])
      expect(described_class.scan("[[1]]", [[0]])).to eq([[[1, 4, :array]]])
    end

    it "raises on invalid json" do
      expect do
        begin
          GC.stress = true
          # TODO: investigate
          # got "munmap_chunk(): invalid pointer" in in console once after
          # JsonScanner.scan '[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[]]]]]]]]]]]]]]]]]]]]]]', [[0,0,0,0,0,0,0]], true + Ctrl+D
          # (last arg wasn't handled at the time and was intended for with_path kwarg)
          # but I don't think it's a problem of the extension or libyajl,
          #   it happened at exit and I free everything before
          # `JsonScanner.scan` returns
          described_class.scan "[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[]]]]]]]]]]]]]]]]]]]]]]", [[0, 0, 0, 0, 0, 0, 0]]
        ensure
          GC.stress = false
        end
      end.to raise_error described_class::ParseError
    end

    it "allows to select ranges" do
      expect(
        described_class.scan("[[1,2],[3,4]]", [[described_class::ANY_INDEX, described_class::ANY_INDEX]]),
      ).to eq(
        [[[2, 3, :number], [4, 5, :number], [8, 9, :number], [10, 11, :number]]],
      )
      expect(
        described_class.scan("[[1,2],[3,4]]", [[described_class::ANY_INDEX, (0...1)]]),
      ).to eq(
        [[[2, 3, :number], [8, 9, :number]]],
      )
    end

    it "allows only positive or -1 values" do
      expect do
        described_class.scan("[[1,2],[3,4]]", [[(0...-1)]])
      end.to raise_error ArgumentError
      expect do
        described_class.scan("[[1,2],[3,4]]", [[(0..-2)]])
      end.to raise_error ArgumentError
      expect do
        described_class.scan("[[1,2],[3,4]]", [[(-42..1)]])
      end.to raise_error ArgumentError
    end

    it "allows to configure error messages" do
      expect do
        described_class.scan "{1}", []
      end.to raise_error described_class::ParseError, /invalid object key(?!.*\(right here\))/m
      expect do
        described_class.scan "{1}", [], verbose_error: false
      end.to raise_error described_class::ParseError, /invalid object key(?!.*\(right here\))/m
      expect do
        described_class.scan "{1}", [], verbose_error: true
      end.to raise_error described_class::ParseError, /invalid object key(?=.*\(right here\))/m
      expect do
        described_class.scan("[0, 42,", [[(1..-1)]], verbose_error: true)
      end.to raise_error described_class::ParseError, /parse error: premature EOF.*\[0, 42,.*\(right here\) ------\^/m
    end

    it "includes bytes consumed in the exception" do
      expect do
        described_class.scan("[[1,2],,[3,4]]", [])
      end.to(
        raise_error(described_class::ParseError) do |exc|
          expect(exc.bytes_consumed).to eq(8)
        end,
      )
      expect do
        described_class.scan("[[1,2", [])
      end.to(
        raise_error(described_class::ParseError) do |exc|
          # 6 because of the final " " chunk - that's how yajl works
          expect(exc.bytes_consumed).to eq(6)
        end,
      )
    end

    it "allows to return an actual path to the element" do
      with_path_expected_res = [
        # result for first matcher, each element array of two items:
        # array of path elements and 3-element array start,end,type
        [[[0], [1, 6, :array]], [[1], [7, 12, :array]]],
        [
          [[0, 0], [2, 3, :number]], [[0, 1], [4, 5, :number]],
          [[1, 0], [8, 9, :number]], [[1, 1], [10, 11, :number]],
        ],
      ]
      params = [
        "[[1,2],[3,4]]",
        [
          [described_class::ANY_INDEX],
          [described_class::ANY_INDEX, described_class::ANY_INDEX],
        ],
      ]
      expect(described_class.scan(*params, with_path: true)).to eq(with_path_expected_res)
    end

    it "allows to pass options as a hash" do
      expect(
        described_class.scan("[1]", [[0]], { with_path: true }),
      ).to eq(
        [
          [[[0], [1, 2, :number]]],
        ],
      )
    end

    it "allows to configure yajl" do
      expect(
        described_class.scan("[1]____________", [[0]], { allow_trailing_garbage: true }),
      ).to eq([[[1, 2, :number]]])
      expect(
        described_class.scan(
          '["1", {"a": /* comment */ 2}]____________', [[1, "a"]],
          { allow_trailing_garbage: true, allow_comments: true },
        ),
      ).to eq([[[26, 27, :number]]])
      expect(
        described_class.scan(
          '[{"a": /* comment */ 1}]_________', [[]],
          { allow_comments: true, allow_trailing_garbage: true },
        ),
      ).to eq([[[0, 24, :array]]])
    end

    it "works with utf-8" do
      json = '{"ルビー": ["Руби"]}'.encode(Encoding::UTF_8)
      expect(described_class.scan(json, [[]])).to eq([[[0, json.bytesize, :object]]])
      res = described_class.scan(json, [["ルビー", 0]])
      expect(res).to eq([[[15, 25, :string]]])
      elem = res.first.first
      expect(JSON.parse(json.byteslice(elem[0]...elem[1]), quirks_mode: true)).to eq("Руби")
    end

    it "raises exceptions in utf-8" do
      bad_json = '{"ルビー": ["Руби" 1]}'.encode(Encoding::UTF_8)
      expect do
        described_class.scan(bad_json, [[]], verbose_error: true)
        # Checks encoding
      end.to raise_error(described_class::ParseError, Regexp.new(Regexp.escape(bad_json)))
    end

    it "works with different encodings" do
      # TODO: encoding validation
      json = '{"a": 1}'.encode(Encoding::UTF_32LE)
      expect do
        described_class.scan(json, [[]])
      end.to raise_error(described_class::ParseError)
    end

    context "with yajl params" do
      it "supports 'allow_comments'" do
        params = ["[0, /* answer */ 42, 0]", [[(1..-1)]]]
        expect(described_class.scan(*params, allow_comments: true)).to eq(
          [[[17, 19, :number], [21, 22, :number]]],
        )
        expect do
          described_class.scan(*params)
        end.to raise_error(described_class::ParseError)
      end

      it "supports 'dont_validate_strings'" do
        params = ["\"\x81\x83\"", [[]]]
        expect(described_class.scan(*params, dont_validate_strings: true)).to eq(
          [[[0, 4, :string]]],
        )
        expect do
          described_class.scan(*params)
        end.to raise_error(described_class::ParseError)
        params = ["{\"\x81\x83\": 42}", [[JsonScanner::ANY_KEY]]]
        expect(described_class.scan(*params, dont_validate_strings: true, with_path: true)).to eq(
          [[[["\x81\x83".dup.force_encoding(Encoding::BINARY)], [7, 9, :number]]]],
        )
        expect do
          described_class.scan(*params, with_path: true)
        end.to raise_error(described_class::ParseError)
      end

      it "supports 'allow_trailing_garbage'" do
        params = ["[0, 42, 0]garbage", [[(1..-1)]]]
        expect(described_class.scan(*params, allow_trailing_garbage: true)).to eq(
          [[[4, 6, :number], [8, 9, :number]]],
        )
        expect do
          described_class.scan(*params)
        end.to raise_error(described_class::ParseError)
      end

      it "supports 'allow_multiple_values'" do
        params = ["[0, 42, 0]  [0, 34]", [[(1..-1)]]]
        expect(described_class.scan(*params, allow_multiple_values: true)).to eq(
          [[[4, 6, :number], [8, 9, :number], [16, 18, :number]]],
        )
        expect do
          described_class.scan(*params)
        end.to raise_error(described_class::ParseError)
      end

      it "handles multiple top-level values correctly with 'allow_multiple_values'" do
        expect(described_class.scan("[0, 42, 0]  [0, 34]", [[]], allow_multiple_values: true)).to eq(
          [[[0, 10, :array], [12, 19, :array]]],
        )
        expect(described_class.scan('{"42": 34}  [0, 34]', [[]], allow_multiple_values: true)).to eq(
          [[[0, 10, :object], [12, 19, :array]]],
        )
        expect(described_class.scan('[0, 42, 0]  {"42": 34}', [[]], allow_multiple_values: true)).to eq(
          [[[0, 10, :array], [12, 22, :object]]],
        )
        expect(described_class.scan('{"42": 34}  {"0": 34}', [[]], allow_multiple_values: true)).to eq(
          [[[0, 10, :object], [12, 21, :object]]],
        )
      end

      it "supports 'allow_partial_values'" do
        params = ["[0, 42, 0,", [[(1..-1)]]]
        expect(described_class.scan(*params, allow_partial_values: true)).to eq(
          [[[4, 6, :number], [8, 9, :number]]],
        )
        expect do
          described_class.scan(*params)
        end.to raise_error(described_class::ParseError)
        expect(described_class.scan("[0, 42, 0", [[(1..-1)]], allow_partial_values: true)).to eq(
          [[[4, 6, :number], [8, 9, :number]]],
        )
        expect(described_class.scan("[0, 42, true", [[(1..-1)]], allow_partial_values: true)).to eq(
          [[[4, 6, :number], [8, 12, :boolean]]],
        )
      end
    end
  end

  describe ".parse" do
    it "extracts values" do
      json_str = 'null false {"a": 1, "b": [0,1], "c": 42}  4.2 {"b": [1,2,3]} {} 1 []'
      json_selector = [["a"], ["b", 2]]
      expect(
        described_class.parse(json_str, json_selector, allow_multiple_values: true),
      ).to eq(
        [
          :null, :boolean, { "a" => 1 }, :number, { "b" => [:stub, :stub, 3] }, :object, :number, :array,
        ],
      )
    end
  end

  describe described_class::Selector do
    it "saves state" do
      key = "abracadabra".dup
      conf = described_class.new [[], [key]]
      key["cad"] = 0.chr
      key = nil # rubocop:disable Lint/UselessAssignment
      GC.start
      expect(
        10.times.map do
          JsonScanner.scan '{"abracadabra": 10}', conf, with_path: true
        end.uniq,
      ).to eq([[[[[], [0, 19, :object]]], [[["abracadabra"], [16, 18, :number]]]]])
      expect(
        10.times.map do
          JsonScanner.scan '{"abracadabra": 10}', conf
        end.uniq,
      ).to eq([[[[0, 19, :object]], [[16, 18, :number]]]])
    end

    it "re-raises exceptions" do
      expect do
        described_class.new [[(0...-1)]]
      end.to raise_error ArgumentError
      expect do
        described_class.new [[(0..-2)]]
      end.to raise_error ArgumentError
      expect do
        described_class.new [[(-42..1)]]
      end.to raise_error ArgumentError
    end

    it "supports inspect" do
      expect(
        described_class.new([[], ["abracadabra", JsonScanner::ANY_INDEX], [42, JsonScanner::ANY_KEY]]).inspect,
      ).to eq("#<JsonScanner::Selector [[], ['abracadabra', (0..-1)], [42, ('*'..'*')]]>")
    end
  end

  describe described_class::Options do
    it "allows to reuse options" do
      options = described_class.new(allow_trailing_garbage: true, allow_partial_values: true)
      expect(
        JsonScanner.scan('{"question1": 1, "answer": 42, "question2": 2}___', [["answer"]], options),
      ).to eq([[[27, 29, :number]]])
      expect(JsonScanner.scan('{"question1": 1, "answer": 42', [["answer"]], options)).to eq([[[27, 29, :number]]])
    end

    it "supports inspect" do
      expect(
        described_class.new(allow_trailing_garbage: true, allow_partial_values: true).inspect,
      ).to eq("#<JsonScanner::Options {allow_trailing_garbage: true, allow_partial_values: true}>")
    end
  end
end
