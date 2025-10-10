# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2025-10-10

## Added

- `JsonScanner.parse` method
- `JsonScanner::Options` parameter instead of a hash
- Support compilation with the `yajl2` library provided by the `libyajl2` gem

### Changed

- Rename `JsonScanner::Config` into `JsonScanner::Selector`

### Removed

- Third positional arg in the `JsonScanner.scan` method that had the same meaning as `with_path:`

## [0.3.1] - 2025-08-14

### Fixed

- Update `bytes_consumed` value properly

## [0.3.0] - 2025-08-14

### Fixed

- Potential problems with garbage collection of the `result` array and other `VALUE`s
- Memory leak when `ArgumentError` is raised by `JsonScanner.scan`
- Fix `yajl_get_bytes_consumed` usage for cases when `yajl_complete_parse` causes callbacks with another `" "` "chunk" - premature EOF with number in the end

### Added

- Report `bytes_consumed` in `ParseError`s
- Possibility to reuse config using `JsonScanner::Config`

## [0.2.0] - 2024-12-27

### Fixed

- Handling of `max_path_len` which led `JsonSlicer.scan` not to match arrays and hashes to the longest path

- Exception encoding - `utf8` instead of `binary`

- Dev config - version in `Gemfile.lock` for old rubies

### Added

- Implement `with_path` flag

- Add kwargs/hash config to configure `yajl`, `with_path` flag and `verbose_error` option

## [0.1.1] - 2024-12-16

### Fixed

- `JsonSlicer.scan` ranges validation and `ANY_INDEX` support

## [0.1.0] - 2024-12-15

### Added

- `JsonSlicer.scan` method basic implementation
