# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- Potential problems with garbage collection of the `result` array and other `VALUE`s

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
