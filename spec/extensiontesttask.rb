# frozen_string_literal: true

require "rake/clean"
require "rake/extensiontask"

module Rake
  class ExtensionTestTask < ExtensionTask
    #
    # The C files to compile.
    #
    attr_accessor :c_spec_files

    #
    # The folders where includes for the test files are.
    #
    # Default: %w{/usr/include /usr/include/google}
    #
    attr_accessor :test_includes

    #
    # The libraries to link against.
    #
    # Default: %w{cmockery}
    #
    attr_accessor :test_libraries

    #
    # The folders where the libraries are
    #
    # Default: %w{/usr/lib}
    #
    attr_accessor :test_lib_folders

    def initialize(*args, &block)
      super
      @c_spec_files = []
      @test_includes = %w[/usr/include /usr/include/google]
      @test_libraries = %w[cmockery]
      @test_lib_folders = %w[/usr/lib]
      init_test_tasks(
        "#{@tmp_dir}/test", "compile:#{@name}:test",
        "spec:c:#{@name}", "spec:valgrind:#{@name}", "spec:gdb:#{@name}",
      )
    end

    private

    def includes
      @includes ||= (@test_includes + [
        ".",
        "../../#{@ext_dir}",
        "/usr/include/ruby-#{RUBY_VERSION}",
        "/usr/include/ruby-#{RUBY_VERSION}/#{RUBY_PLATFORM}",
      ]).map { |l| "-I#{l}" }.join(" ")
    end

    def libraries
      @libraries ||= (@test_libraries + %w[ruby pthread crypto]).map { |l| "-l#{l}" }.join(" ")
    end

    def lib_folders
      @lib_folders ||= (@test_lib_folders + %w[/usr/lib .]).map { |l| "-L#{l}" }.join(" ")
    end

    def compile_tests
      # compile the test sources
      FileList["*.c"].each do |cfile|
        sh "gcc -g #{includes} -c #{cfile}"
      end

      source_objects = FileList["../#{RUBY_PLATFORM}/#{@name}/#{RUBY_VERSION}/*.o"]
      # link the executables
      FileList["*.o"].each do |ofile|
        sh "gcc -g #{lib_folders} #{libraries} #{source_objects} #{ofile} -o #{ofile.ext}"
      end
    end

    def init_compile_task(compile_dir, compile_task)
      directory compile_dir
      desc "Compile #{@name} tests"
      task compile_task => ["compile:#{@name}", compile_dir] do
        # copy the test files into the compilation folder
        @c_spec_files.each { |file| cp file, compile_dir }

        # start compilation
        chdir(compile_dir) { compile_tests }
      end
    end

    def init_valgrind_task(compile_dir, compile_task, valgrind_task)
      desc "Execute valgrind for a #{@name} test"
      task valgrind_task => [compile_task] do |_t, args|
        sh "valgrind --num-callers=50 --error-limit=no --partial-loads-ok=yes --undef-value-errors=no " \
           "--leak-check=full #{compile_dir}/#{args.test}"
      end
    end

    def init_gdb_task(compile_dir, compile_task, gdb_task)
      desc "Execute gdb for a #{@name} test"
      task gdb_task => [compile_task] do |_t, args|
        sh "gdb #{compile_dir}/#{args.test}"
      end
    end

    def init_test_task(compile_dir, compile_task, test_task)
      desc "Test #{@name}"
      task test_task => [compile_task] do |_t, args|
        if args.test
          sh "#{compile_dir}/#{args.test}"
        else
          FileList["#{compile_dir}/*.o"].each do |ofile|
            sh ofile.ext.to_s
          end
        end
      end
    end

    def init_test_tasks(compile_dir, compile_task, test_task, valgrind_task, gdb_task)
      init_compile_task(compile_dir, compile_task)
      init_valgrind_task(compile_dir, compile_task, valgrind_task)
      init_gdb_task(compile_dir, compile_task, gdb_task)
      init_test_task(compile_dir, compile_task, test_task)

      desc "Test all C extensions"
      task "spec:c" => [test_task]
    end
  end
end
