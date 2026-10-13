<p align="center">
  <img src="../../source/bootstrapper/assets/hyperhen_sicon.png" alt="HyperHEN" height="128" width="128"/>
</p>

<p align="center">
  <b>HyperHEN Plugin SDK</b><br/>
  A modular C SDK for independent PlayStation 5 payload plugins
</p>

<p align="center">
  <a href="README_ZH.md">简体中文</a>
  ·
  <b>English</b>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="license"/></a>
  <img src="https://img.shields.io/badge/Platform-PlayStation%205-003791?style=flat&logo=playstation" alt="PlayStation 5"/>
  <img src="https://img.shields.io/badge/C-00599C?style=flat&logo=c&logoColor=white" alt="C"/>
  <img src="https://img.shields.io/badge/Build-CMake-064F8C?style=flat&logo=cmake" alt="CMake"/>
</p>

HyperHEN Plugin SDK provides the contracts and small runtime needed to build
standalone PS5 ELF plugins. Plugins run as independent processes and use a
versioned C ABI, host services, lifecycle callbacks, events, and a replaceable
IPC transport. The SDK is designed to evolve without exposing HyperHEN daemon
internals or a C++ ABI to plugin authors.

> This vendored SDK is the client side of the integration. HyperHEN has the host UI
> registry, cooperative daemon plugin socket, ShellUI XML adapter, and plugin
> manager. A plugin is a standard little-endian ELF containing the
> `.onion_plugin` descriptor; no ZIP container or manifest file is required.
> The `onion_*` identifiers are retained solely as the stable wire/C ABI.

## Features

- CMake `onion_add_plugin(...)` helper for PS5 targets
- Stable, versioned plugin descriptor embedded in `.onion_plugin`
- Explicit lifecycle state machine and callbacks
- Capability-scoped host services for logging, notifications, and configuration
- Typed configuration helpers for strings, integers, and booleans
- Thread-safe event bus with safe unsubscribe during callbacks
- Fixed-width IPC frames with request IDs and status responses
- Connection-scoped `HELLO` with immutable plugin ID and capability declaration
- Replaceable transport interface for sockets, mocks, or future transports
- Versioned `onion.ui` service with page, menu, group, label, action, toggle,
  list, list-item, and input contributions
- Validated little-endian UI documents and chunked IPC registration
- Owner-scoped UI action polling with strict event decoding
- Python tools to inspect and atomically deploy plugin ELF artifacts
- Minimal `hello`, daemon, and dynamic UI smoke-test samples

## Requirements

- PS5 Payload SDK (`PS5_PAYLOAD_SDK`)
- CMake 3.20 or newer
- Ninja (recommended)
- Clang/LLVM with the Prospero target
- Python 3.9 or newer

Host-side Runtime tests only require a C compiler, CMake, Python, and pthreads.

## Build and test

Build the host-side runtime tests:

```sh
cmake -S . -B build -G Ninja -DONION_SDK_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Build the PS5 samples:

```sh
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
cmake -S . -B build-ps5 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/ps5-toolchain.cmake \
  -DONION_SDK_BUILD_SAMPLES=ON \
  -DPS5_PAYLOAD_SDK="$PS5_PAYLOAD_SDK"
cmake --build build-ps5 --target hello
```

The plugin ELF is written to `build-ps5/bin/hello.elf`. Deploy it with
`tools/deploy_plugin.py`; the host installs it as
`/data/HyperHEN/plugins/<plugin_id>.elf` after validating its descriptor.

For the end-to-end dynamic UI check, build and deploy the auto-start smoke-test
plugin:

```sh
cmake --build build-ps5 --target dynamic_ui
python3 tools/deploy_plugin.py build-ps5/bin/dynamic_ui.elf
```

It installs as `/data/HyperHEN/plugins/ONIO00003.elf`. After HyperHEN starts or
reconciles plugins, open the `Dynamic UI Smoke Test` page and exercise its
toggle, list, numeric input, and confirmed action. The plugin appends registration,
action, and shutdown records to `/data/HyperHEN/dynamic_ui_smoke.log`.

## Create a plugin

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyPlugin C)

find_package(HyperHENPluginSDK CONFIG REQUIRED)
onion_add_plugin(
    NAME my_plugin
    TITLE_ID MYPL00001
    VERSION 1.00
    SOURCES source/main.c
    LIBRARIES SceLibcInternal kernel_sys)
```

`onion_add_plugin` automatically links `HyperHEN::Runtime`. The `main(void)`
entry point is still an ordinary PS5 payload entry point. Keep all data crossing
the host boundary in the public C headers; pointers, STL objects, exceptions,
and C++ class layouts are not part of the ABI.

## UI contributions

An IPC-backed client opens its connection session once, before querying optional
services. The descriptor ID is self-declared: the host binds it to that
connection and rejects duplicate active IDs, but does not cryptographically
authenticate the plugin.

```c
onion_socket_transport_connect(&transport, &socket_state,
                               ONION_PLUGIN_IPC_SOCKET_PATH);
onion_client_init(&client, &transport);
onion_client_open_session(&client, &onion_plugin_descriptor);
onion_client_make_services(&client, &host_services);
```

Include `<onion/ui.h>` and build an opaque `onion_ui_document`. Add the root
page before its children, validate the document, then register it through the
Host Services object received by the plugin. A contribution with the same
`plugin_id` and `contribution_id` replaces the previous document atomically and
keeps its handle.

```c
onion_ui_document_desc_v1 document_desc = {
    .struct_size = sizeof(document_desc),
    .abi_version = ONION_UI_ABI_VERSION,
    .plugin_id = "MYPL00001",
    .contribution_id = "settings",
    .title = "My plugin",
    .root_page_id = "main",
};
onion_ui_document *document = NULL;
onion_ui_document_create(&document_desc, &document);

onion_ui_node_desc_v1 page = {
    .struct_size = sizeof(page),
    .abi_version = ONION_UI_ABI_VERSION,
    .kind = ONION_UI_NODE_PAGE,
    .id = "main",
    .title = "Settings",
};
onion_ui_document_add_node(document, &page);
onion_ui_register(host_services, document, &handle);
```

Poll actions from the plugin's normal event loop. One call consumes at most one
event; an empty queue returns `ONION_E_NOT_FOUND` immediately.

```c
onion_ui_event_v1 event;
if (onion_client_poll_ui_event(&client, &event) == ONION_OK) {
    onion_event_publish(event_bus, ONION_EVENT_UI_ACTION,
                        &event, sizeof(event));
}
```

The UI document is limited to 256 KiB, 256 nodes, and a nesting depth of 8.
Plugin IDs and node IDs are stable identifiers, not display strings. Unregister
contributions during normal shutdown; the host also removes all contributions
owned by a disconnected plugin session.

## Architecture

```text
plugin code
    -> runtime + host services
    -> IPC client adapter
    -> transport (socket, mock, or future implementation)
```

Read [docs/architecture.md](docs/architecture.md) for ownership boundaries,
state transitions, ABI rules, and extension guidance.

## Current scope and roadmap

The current release includes the plugin-side UI contribution contract, the
HyperHEN registry/XML adapter, cross-process snapshot publication, and owner-
scoped action delivery. Remaining host-facing layers are planned in this order:

1. Remaining daemon Host Service handlers for log, notify, and configuration
2. Plugin status, auto-start, stop/restart, and crash recovery
3. Optional WebUI contribution backend
4. Optional etaHEN `.plugin` compatibility tooling

The current daemon plugin socket serves `HELLO`, `PING`, event poll command 9,
and UI commands 10–15. `onion_client_poll_ui_event()` returns one validated
`onion_ui_event_v1`, or `ONION_E_NOT_FOUND` when the queue is empty.
Other Host Service commands return `ONION_E_NOT_SUPPORTED` until their daemon
handlers are added.

## Contributing

Please read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request.
Bug reports and feature requests use the GitHub issue forms. Security-sensitive
reports should follow [SECURITY.md](SECURITY.md).

## Credits and related projects

- [OnionHEN](https://github.com/aydencharles/onionHEN) — the original host project and
  original logo used by this repository
- [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) — Prospero toolchain
  and target headers
- [etaHEN-Plugins](https://github.com/etaHEN/etaHEN-Plugins) — reference for PS5
  plugin project layout and packaging conventions

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).
Third-party components retain their respective licenses and notices.

> HyperHEN is an unofficial homebrew project and is not affiliated with Sony
> Interactive Entertainment. Use software from this repository only on
> hardware you own and at your own risk. No warranty is provided.
