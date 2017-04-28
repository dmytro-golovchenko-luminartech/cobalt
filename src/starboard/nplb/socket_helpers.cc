// Copyright 2015 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "starboard/nplb/socket_helpers.h"

#include "starboard/once.h"
#include "starboard/socket.h"
#include "starboard/socket_waiter.h"
#include "starboard/thread.h"
#include "starboard/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace starboard {
namespace nplb {
namespace {

int port_number_for_tests = 0;
SbOnceControl valid_port_once_control = SB_ONCE_INITIALIZER;

void InitializePortNumberForTests() {
  // Create a listening socket. Let the system choose a port for us.
  SbSocket socket = CreateListeningTcpSocket(kSbSocketAddressTypeIpv4, 0);
  SB_DCHECK(socket != kSbSocketInvalid);

  // Query which port this socket was bound to and save it to valid_port_number.
  SbSocketAddress socket_address = {0};
  bool result = SbSocketGetLocalAddress(socket, &socket_address);
  SB_DCHECK(result);
  port_number_for_tests = socket_address.port;

  // Clean up the socket.
  result = SbSocketDestroy(socket);
  SB_DCHECK(result);
}
}  // namespace

int GetPortNumberForTests() {
#if defined(SB_SOCKET_OVERRIDE_PORT_FOR_TESTS)
  return SB_SOCKET_OVERRIDE_PORT_FOR_TESTS;
#else
  SbOnce(&valid_port_once_control, &InitializePortNumberForTests);
  return port_number_for_tests;
#endif
}

bool IsUnspecified(const SbSocketAddress* address) {
  // Look at each piece of memory and make sure too many of them aren't zero.
  int components = (address->type == kSbSocketAddressTypeIpv4 ? 4 : 16);
  int zero_count = 0;
  for (int i = 0; i < components; ++i) {
    if (address->address[i] == 0) {
      ++zero_count;
    }
  }
  return components == zero_count;
}

bool IsLocalhost(const SbSocketAddress* address) {
  if (address->type == kSbSocketAddressTypeIpv4) {
    return address->address[0] == 127;
  }

  if (address->type == kSbSocketAddressTypeIpv6) {
    bool may_be_localhost = true;
    for (int i = 0; i < 15; ++i) {
      may_be_localhost &= (address->address[i] == 0);
    }

    return (may_be_localhost && address->address[15] == 1);
  }

  return false;
}

SbSocketAddress GetLocalhostAddress(SbSocketAddressType address_type,
                                    int port) {
  SbSocketAddress address = GetUnspecifiedAddress(address_type, port);
  switch (address_type) {
    case kSbSocketAddressTypeIpv4: {
      address.address[0] = 127;
      address.address[3] = 1;
      return address;
    }
    case kSbSocketAddressTypeIpv6: {
      address.address[15] = 1;
      return address;
    }
  }
  ADD_FAILURE() << "GetLocalhostAddress for unknown address type";
  return address;
}

SbSocketAddress GetUnspecifiedAddress(SbSocketAddressType address_type,
                                      int port) {
  SbSocketAddress address = {0};
  address.type = address_type;
  address.port = port;
  return address;
}

SbSocket CreateServerTcpSocket(SbSocketAddressType address_type) {
  SbSocket server_socket = SbSocketCreate(address_type, kSbSocketProtocolTcp);
  if (!SbSocketIsValid(server_socket)) {
    ADD_FAILURE() << "SbSocketCreate failed";
    return kSbSocketInvalid;
  }

  if (!SbSocketSetReuseAddress(server_socket, true)) {
    ADD_FAILURE() << "SbSocketSetReuseAddress failed";
    SbSocketDestroy(server_socket);
    return kSbSocketInvalid;
  }

  return server_socket;
}

SbSocket CreateBoundTcpSocket(SbSocketAddressType address_type, int port) {
  SbSocket server_socket = CreateServerTcpSocket(address_type);
  if (!SbSocketIsValid(server_socket)) {
    return kSbSocketInvalid;
  }

  SbSocketAddress address = GetUnspecifiedAddress(address_type, port);
  SbSocketError result = SbSocketBind(server_socket, &address);
  if (result != kSbSocketOk) {
    ADD_FAILURE() << "SbSocketBind to " << port << " failed: " << result;
    SbSocketDestroy(server_socket);
    return kSbSocketInvalid;
  }

  return server_socket;
}

SbSocket CreateListeningTcpSocket(SbSocketAddressType address_type, int port) {
  SbSocket server_socket = CreateBoundTcpSocket(address_type, port);
  if (!SbSocketIsValid(server_socket)) {
    return kSbSocketInvalid;
  }

  SbSocketError result = SbSocketListen(server_socket);
  if (result != kSbSocketOk) {
    ADD_FAILURE() << "SbSocketListen failed: " << result;
    SbSocketDestroy(server_socket);
    return kSbSocketInvalid;
  }

  return server_socket;
}

namespace {
SbSocket CreateConnectingTcpSocket(SbSocketAddressType address_type, int port) {
  SbSocket client_socket = SbSocketCreate(address_type, kSbSocketProtocolTcp);
  if (!SbSocketIsValid(client_socket)) {
    ADD_FAILURE() << "SbSocketCreate failed";
    return kSbSocketInvalid;
  }

  // Connect to localhost:<port>.
  SbSocketAddress address = GetLocalhostAddress(address_type, port);

  // This connect will probably return pending, but we'll assume it will connect
  // eventually.
  SbSocketError result = SbSocketConnect(client_socket, &address);
  if (result != kSbSocketOk && result != kSbSocketPending) {
    ADD_FAILURE() << "SbSocketConnect failed: " << result;
    SbSocketDestroy(client_socket);
    return kSbSocketInvalid;
  }

  return client_socket;
}
}  // namespace

SbSocket AcceptBySpinning(SbSocket server_socket, SbTime timeout) {
  SbTimeMonotonic start = SbTimeGetMonotonicNow();
  while (true) {
    SbSocket accepted_socket = SbSocketAccept(server_socket);
    if (SbSocketIsValid(accepted_socket)) {
      return accepted_socket;
    }

    // If we didn't get a socket, it should be pending.
    EXPECT_EQ(kSbSocketPending, SbSocketGetLastError(server_socket));

    // Check if we have passed our timeout.
    if (SbTimeGetMonotonicNow() - start >= timeout) {
      break;
    }

    // Just being polite.
    SbThreadYield();
  }

  return kSbSocketInvalid;
}

bool WriteBySpinning(SbSocket socket,
                     const char* data,
                     int data_size,
                     SbTime timeout) {
  SbTimeMonotonic start = SbTimeGetMonotonicNow();
  int total = 0;
  while (total < data_size) {
    int sent = SbSocketSendTo(socket, data + total, data_size - total, NULL);
    if (sent >= 0) {
      total += sent;
      continue;
    }

    if (SbSocketGetLastError(socket) != kSbSocketPending) {
      return false;
    }

    if (SbTimeGetMonotonicNow() - start >= timeout) {
      return false;
    }

    SbThreadYield();
  }

  return true;
}

bool ReadBySpinning(SbSocket socket,
                    char* out_data,
                    int data_size,
                    SbTime timeout) {
  SbTimeMonotonic start = SbTimeGetMonotonicNow();
  int total = 0;
  while (total < data_size) {
    int received =
        SbSocketReceiveFrom(socket, out_data + total, data_size - total, NULL);
    if (received >= 0) {
      total += received;
      continue;
    }

    if (SbSocketGetLastError(socket) != kSbSocketPending) {
      return false;
    }

    if (SbTimeGetMonotonicNow() - start >= timeout) {
      return false;
    }

    SbThreadYield();
  }

  return true;
}

ConnectedTrio CreateAndConnect(SbSocketAddressType server_address_type,
                               SbSocketAddressType client_address_type,
                               int port,
                               SbTime timeout) {
  // Verify the listening socket.
  SbSocket listen_socket = CreateListeningTcpSocket(server_address_type, port);
  if (!SbSocketIsValid(listen_socket)) {
    ADD_FAILURE() << "Could not create listen socket.";
    return ConnectedTrio();
  }

  // Verify the socket to connect to the listening socket.
  SbSocket client_socket = CreateConnectingTcpSocket(client_address_type, port);
  if (!SbSocketIsValid(client_socket)) {
    ADD_FAILURE() << "Could not create client socket.";
    EXPECT_TRUE(SbSocketDestroy(listen_socket));
    return ConnectedTrio();
  }

  // Spin until the accept happens (or we get impatient).
  SbTimeMonotonic start = SbTimeGetMonotonicNow();
  SbSocket server_socket = AcceptBySpinning(listen_socket, timeout);
  if (!SbSocketIsValid(server_socket)) {
    ADD_FAILURE() << "Failed to accept within " << timeout;
    EXPECT_TRUE(SbSocketDestroy(listen_socket));
    EXPECT_TRUE(SbSocketDestroy(client_socket));
    return ConnectedTrio();
  }

  return ConnectedTrio(listen_socket, client_socket, server_socket);
}

SbTimeMonotonic TimedWait(SbSocketWaiter waiter) {
  SbTimeMonotonic start = SbTimeGetMonotonicNow();
  SbSocketWaiterWait(waiter);
  return SbTimeGetMonotonicNow() - start;
}

// Waits on the given waiter, and returns the elapsed time.
SbTimeMonotonic TimedWaitTimed(SbSocketWaiter waiter, SbTime timeout) {
  SbTimeMonotonic start = SbTimeGetMonotonicNow();
  SbSocketWaiterWaitTimed(waiter, timeout);
  return SbTimeGetMonotonicNow() - start;
}

}  // namespace nplb
}  // namespace starboard
