/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#pragma once

#include <msg.hpp>
#include <atomic>
#include <cstddef>
#include <string>

// ---------------------------------------------------------------------------
// Shared daemon-side Unix IPC transport.
//
// Architecture:
//   wire protocol  →  msg.hpp (IPCMessage, paths, commands)
//   transport      →  this module (listen / accept / recv / send / loop)
//   business       →  process handleIPC (daemon vs util command tables)
//
// Clients live in ipc_client.hpp (injectees). Servers must not #include the
// client implementation.
// ---------------------------------------------------------------------------

namespace onion {

struct IpcClientArgs {
  std::string ip;
  int socket = -1;
  int cl_nmb = 0;
};

using IpcCommandHandler = void (*)(IpcClientArgs *client, std::string &msg,
                                   DaemonCommands cmd);


// --- transport primitives ---
int ipc_network_listen(const char *soc_path);
int ipc_network_accept(int socket_fd);

/** Connect to an AF_UNIX socket at @path. Returns fd >= 0, or -1. */
int ipc_unix_connect(const char *path);

/** Single recv (may be short). Prefer ipc_network_recv_full for frames. */
int ipc_network_recv(int socket_fd, void *buffer, int32_t size);

/**
 * Read exactly `size` bytes (loop on short reads / EINTR).
 * Returns size on success, 0 on clean EOF before any data, 0 < n < size on
 * short EOF, -1 on error.
 */
int ipc_network_recv_full(int socket_fd, void *buffer, int32_t size);

/** Send exactly `size` bytes (loop on short writes / EINTR). */
int ipc_network_send_full(int socket_fd, const void *buffer, int32_t size);

int ipc_network_send(int socket_fd, void *buffer, int32_t size);
int ipc_network_close(int socket_fd);

// --- pure wire helpers (host-testable) ---

/** True when nbytes matches a full IPCMessage frame. */
inline bool ipc_frame_is_complete(int nbytes) {
  return nbytes == static_cast<int>(sizeof(IPCMessage));
}

/** Force trailing NUL on the payload buffer (msg is char[DAEMON_BUFF_MAX]). */
inline void ipc_message_force_nul(IPCMessage &msg) {
  msg.msg[sizeof(msg.msg) - 1] = '\0';
}

/** Escape one JSON string value using cJSON, without surrounding quotes. */
std::string ipc_json_escape(const std::string &in);

/**
 * JSON body for daemon/util replies.
 * Shape: {"res":N,"var":"..."} with `var` properly escaped.
 */
std::string ipc_format_reply_body(bool error, const std::string &out_var);

// Build {"res":N,"var":"..."} reply with the process's return command ordinal.
void ipc_reply(int sender_socket, DaemonCommands reply_cmd, bool error,
               const std::string &out_var = "Nothing");

// Server loop options (passed by pointer into pthread entry).
struct IpcServerOptions {
  const char *socket_path = nullptr;
  IpcCommandHandler handler = nullptr;
  DaemonCommands reply_cmd = BREW_RETURN_VALUE;
  bool detach_clients = true; // util always did; daemon should too (no leak)
  const char *tag = "ipc";
  // Optional lifecycle control (nullptr = run forever, existing behaviour):
  //  * running  — the accept loop exits once this goes false.
  //  * server_fd — holds the current listening fd so a restart can shut it
  //    down; accept() then returns and the loop re-listens on a fresh socket.
  std::atomic_bool *running = nullptr;
  std::atomic<int> *server_fd = nullptr;
};

// pthread-compatible entry: arg must be IpcServerOptions* with static lifetime.
void *ipc_server_loop(void *options_ptr);

/** Request a permanent shutdown: clear *running and shut the listener down. */
void ipc_server_stop(IpcServerOptions *opts);

/** Take ownership of *fd (store -1), then shutdown+close it. */
void ipc_release_fd(std::atomic<int> *fd);

/**
 * Take ownership of *fd (store -1), then shutdown+close it. Safe to call
 * concurrently with the accept loop: only one side closes the descriptor.
 */
void ipc_release_listen_fd(std::atomic<int> *fd);

/**
 * Re-create the listening socket (restore service). Shuts the current
 * listener down so the accept loop closes it and re-listens; the loop keeps
 * running. Used after a rest-mode resume, when the old socket may be dead.
 */
void ipc_server_restart(IpcServerOptions *opts);

} // namespace onion

// Historical name used by daemon/util sources.
using clientArgs = onion::IpcClientArgs;
