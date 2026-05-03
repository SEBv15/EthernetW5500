#ifndef ethernetserver_h
#define ethernetserver_h

#include "Server.h"

class EthernetClient;

class EthernetServer :
public Server {
private:
  uint16_t _port;
  void _relisten(); // Re-open any closed socket on this port (was: accept)
public:
  EthernetServer(uint16_t);
  EthernetClient available();
  // Yield-once accept(): returns each newly ESTABLISHED client exactly once,
  // independently of whether data has arrived yet (so callers can immediately
  // write protocol greetings). Returns an invalid EthernetClient (operator bool
  // == false) when nothing new is pending.
  EthernetClient accept();
  virtual void begin();
  virtual size_t write(uint8_t);
  virtual size_t write(const uint8_t *buf, size_t size);
  using Print::write;
};

#endif
