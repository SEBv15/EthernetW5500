#include "utility/w5500.h"
#include "utility/socket.h"
extern "C" {
#include "string.h"
}

#include "EthernetW5500.h"
#include "EthernetClient.h"
#include "EthernetServer.h"

// Yield-once tracker: bit `s` set == socket s has already been returned by
// accept() and the caller "owns" it. Cleared when the socket leaves
// ESTABLISHED state (CLOSED, FIN_WAIT, etc.) so the slot can be re-used.
static uint8_t _accepted_mask = 0;

EthernetServer::EthernetServer(uint16_t port)
{
  _port = port;
}

void EthernetServer::begin()
{
  for (int sock = 0; sock < MAX_SOCK_NUM; sock++) {
    EthernetClient client(sock);
    if (client.status() == SnSR::CLOSED) {
      socket(sock, SnMR::TCP, _port, 0);
      listen(sock);
      EthernetClass::_server_port[sock] = _port;
      break;
    }
  }
}

void EthernetServer::_relisten()
{
  int listening = 0;

  for (int sock = 0; sock < MAX_SOCK_NUM; sock++) {
    EthernetClient client(sock);

    if (EthernetClass::_server_port[sock] == _port) {
      uint8_t st = client.status();
      // Reap previously-yielded sockets so accept() can re-yield them after
      // a future re-connect on the same socket index.
      if (st != SnSR::ESTABLISHED && st != SnSR::CLOSE_WAIT) {
        _accepted_mask &= ~(1 << sock);
      }
      if (st == SnSR::LISTEN) {
        listening = 1;
      }
      else if (st == SnSR::CLOSE_WAIT && !client.available()) {
        client.stop();
      }
    }
  }

  if (!listening) {
    begin();
  }
}

EthernetClient EthernetServer::accept()
{
  _relisten();

  for (int sock = 0; sock < MAX_SOCK_NUM; sock++) {
    if (EthernetClass::_server_port[sock] != _port) continue;
    if (_accepted_mask & (1 << sock)) continue;
    EthernetClient client(sock);
    if (client.status() == SnSR::ESTABLISHED) {
      _accepted_mask |= (1 << sock);
      return client;
    }
  }
  return EthernetClient(MAX_SOCK_NUM);
}

EthernetClient EthernetServer::available()
{
  _relisten();

  for (int sock = 0; sock < MAX_SOCK_NUM; sock++) {
    EthernetClient client(sock);
    if (EthernetClass::_server_port[sock] == _port &&
        (client.status() == SnSR::ESTABLISHED ||
         client.status() == SnSR::CLOSE_WAIT)) {
      if (client.available()) {
        // XXX: don't always pick the lowest numbered socket.
        return client;
      }
    }
  }

  return EthernetClient(MAX_SOCK_NUM);
}

size_t EthernetServer::write(uint8_t b)
{
  return write(&b, 1);
}

size_t EthernetServer::write(const uint8_t *buffer, size_t size)
{
  size_t n = 0;

  _relisten();

  for (int sock = 0; sock < MAX_SOCK_NUM; sock++) {
    EthernetClient client(sock);

    if (EthernetClass::_server_port[sock] == _port &&
      client.status() == SnSR::ESTABLISHED) {
      n += client.write(buffer, size);
    }
  }

  return n;
}
