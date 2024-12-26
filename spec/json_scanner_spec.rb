# frozen_string_literal: true

require_relative "spec_helper"

RSpec.describe JsonScanner do
  it "has a version number" do
    expect(described_class::VERSION).not_to be_nil
  end

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

  it "raises on invalid json" do
    expect do
      begin
        GC.stress = true
        # TODO: investigate
        # got "munmap_chunk(): invalid pointer" in in console once after
        # JsonScanner.scan '[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[]]]]]]]]]]]]]]]]]]]]]]', [[0,0,0,0,0,0,0]], true + Ctrl+D
        # (last arg wasn't handled at the time and was intended for with_path kwarg)
        # but I don't think it's a problem of tht extension or libyajl, it happened at exit and I free everything before
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
  end

  it "allows to return an actual path to the element" do
    expect(
      described_class.scan(
        "[[1,2],[3,4]]",
        [
          [described_class::ANY_INDEX],
          [described_class::ANY_INDEX, described_class::ANY_INDEX],
        ],
        with_path: true,
      ),
    ).to eq(
      [
        # result for first mathcer, each element array of two items:
        # array of path elements and 3-element array start,end,type
        [[[0], [1, 6, :array]], [[1], [7, 12, :array]]],
        [
          [[0, 0], [2, 3, :number]], [[0, 1], [4, 5, :number]],
          [[1, 0], [8, 9, :number]], [[1, 1], [10, 11, :number]],
        ],
      ],
    )
  end
end
