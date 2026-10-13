# Changelog

All notable changes to OnionHEN Plugin SDK will be documented in this file.

The project follows [Semantic Versioning](https://semver.org/) for SDK releases.
Until version 1.0, minor releases may revise public APIs when the migration is
documented. Patch releases must remain source and wire compatible within the
same minor release.

## [Unreleased]

### Added

- Initial CMake SDK and PS5 toolchain integration
- Versioned plugin descriptor and public C ABI
- Lifecycle Runtime, Host Services, typed configuration, and status model
- Thread-safe event bus
- Replaceable transport and IPC client adapter
- Cooperative connection `HELLO` with immutable plugin ID and capabilities
- Owned Unix socket connector for the default OnionHEN plugin endpoint
- Versioned UI contribution model, validation, encoder, and chunked IPC client
- `onion.ui` optional Host Service and UI action event contract
- ELF plugin inspection and atomic deployment tools
- Host-side tests, samples, architecture documentation, and repository policies
