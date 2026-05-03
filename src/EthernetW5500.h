/*
 modified 12 Aug 2013
 by Soohwan Kim (suhwan@wiznet.co.kr)

 - 10 Apr. 2015
 Added support for Arduino Ethernet Shield 2
 by Arduino.org team

 */
#ifndef ethernetw5500_h
#define ethernetw5500_h

#include <inttypes.h>
#include "utility/w5500.h"
#include "IPAddress.h"
#include "EthernetClient.h"
#include "EthernetServer.h"
#include "EthernetUdp3.h"
#include "Dhcp.h"
#if defined(PICO_RP2040) || defined(PICO_RP2350)
  #include "pico/stdlib.h"
#endif

enum phyMode_t {
  HALF_DUPLEX_10,
  FULL_DUPLEX_10,
  HALF_DUPLEX_100,
  FULL_DUPLEX_100,
  FULL_DUPLEX_100_AUTONEG,
  POWER_DOWN,
  ALL_AUTONEG
  };

enum EthernetLinkStatus {
  Unknown,
  LinkOFF,
  LinkON
  };

enum EthernetHardwareStatus {
  EthernetNoHardware,
  EthernetW5500
  };

class EthernetClass {
private:
  IPAddress _dnsServerAddress;
  DhcpClass* _dhcp;
  char _customHostname[32];
  phyMode_t _autoNegFallback;
  uint16_t _autoNegStableMs;
  uint16_t _autoNegTotalMs;
  uint8_t _simr;
  EthernetHardwareStatus _hwStatus;

  // Chip init + version probe + optional auto-neg fallback wait + interrupt
  // mask re-apply. Returns true if the W5500 was detected. Called by every
  // begin() overload.
  bool _initChip();
public:
  uint8_t _maxSockNum;
  uint8_t _pinCS;
  uint8_t _pinRST;
  uint16_t _startupDelayMs;
  uint8_t _socketImr;  // Sticky Sn_IMR re-applied by socket() on every open

  static uint8_t _state[MAX_SOCK_NUM];
  static uint16_t _server_port[MAX_SOCK_NUM];

  EthernetClass() {
    _dhcp = NULL;
    _customHostname[0] = 0;
    _pinCS = 10;
    _maxSockNum = 8;
    _startupDelayMs = 1000;
    _socketImr = 0;
    _simr = 0;
    _autoNegFallback = ALL_AUTONEG;
    _autoNegStableMs = 1000;
    _autoNegTotalMs = 3000;
    _hwStatus = EthernetNoHardware;
    }

  void setRstPin(uint8_t pinRST = 9); // for WIZ550io or USR-ES1, must set befor Ethernet.begin
  void setCsPin(uint8_t pinCS = 10); // must set befor Ethernet.begin

  // Initialize with less sockets but more RX/TX Buffer
  // maxSockNum = 1 Socket 0 -> RX/TX Buffer 16k
  // maxSockNum = 2 Socket 0, 1 -> RX/TX Buffer 8k
  // maxSockNum = 4 Socket 0...3 -> RX/TX Buffer 4k
  // maxSockNum = 8 (Standard) all sockets -> RX/TX Buffer 2k
  // be carefull of the MAX_SOCK_NUM, because in the moment it can't dynamicly changed
  // startupDelayMs is the post-power-up settle delay before the first SPI
  // transaction (default 1000 ms). Pass 0 if your sketch already delayed
  // sufficiently before reaching init().
  void init(uint8_t maxSockNum = 8, uint16_t startupDelayMs = 1000);

  // PHY auto-neg fallback. Configured before begin(); begin() then waits up to
  // totalWaitMs after w5500.init() for a stable link (link bit set continuously
  // for at least stableMs), and forces the given mode if link came up but never
  // stabilised. If link never came up at all, the PHY is left in auto-neg.
  // Pass ALL_AUTONEG (default) to disable the workaround.
  void setAutoNegFallback(phyMode_t fallback,
                          uint16_t stableMs = 1000,
                          uint16_t totalWaitMs = 3000);

  // W5500 socket-interrupt config.
  //   simr: SIMR (0x0018) = bitmask of sockets that forward Sn_IR to the INT pin.
  //   socketImr: per-socket Sn_IMR mask applied to every socket on every open.
  //              Stored in the library and re-applied inside socket() so that
  //              accept()/begin() cycles cannot strip it.
  // Use SnIR::CON | SnIR::RECV | SnIR::DISCON | SnIR::TIMEOUT | SnIR::SEND_OK.
  void enableInterrupts(uint8_t simr, uint8_t socketImr);

  EthernetHardwareStatus hardwareStatus(); // EthernetNoHardware or EthernetW5500
  EthernetLinkStatus linkStatus();         // LinkON / LinkOFF (Unknown reserved)

  uint8_t softreset(); // can set only after Ethernet.begin
  void hardreset(); // You need to set the Rst pin

#if defined(WIZ550io_WITH_MACADDRESS) || defined(PICO_RP2350) || defined(PICO_RP2040)
  // Initialize function when use the ioShield serise (included WIZ550io)
  // WIZ550io has a MAC address which is written after reset.
  // Default IP, Gateway and subnet address are also writen.
  // so, It needs some initial time. please refer WIZ550io Datasheet in details.
  // It also allows a random generated MAC address for Raspberry Pi Pico(2)
  int begin(void);
  void begin(IPAddress local_ip);
  void begin(IPAddress local_ip, IPAddress subnet);
  void begin(IPAddress local_ip, IPAddress subnet, IPAddress gateway);
  void begin(IPAddress local_ip, IPAddress subnet, IPAddress gateway, IPAddress dns_server);
#endif
  // Initialize the Ethernet shield to use the provided MAC address and gain the rest of the
  // configuration through DHCP.
  // Returns 0 if the DHCP configuration failed, and 1 if it succeeded.
  // The 3-arg form mirrors the bundled Arduino Ethernet library: timeout is the
  // total DHCP attempt budget in ms (default 60000), responseTimeout is the
  // per-message wait in ms (default 5000).
  int begin(uint8_t *mac_address);
  int begin(uint8_t *mac_address, unsigned long timeout, unsigned long responseTimeout = 5000);
  void begin(uint8_t *mac_address, IPAddress local_ip);
  void begin(uint8_t *mac_address, IPAddress local_ip, IPAddress subnet);
  void begin(uint8_t *mac_address, IPAddress local_ip, IPAddress subnet, IPAddress gateway);
  void begin(uint8_t *mac_address, IPAddress local_ip, IPAddress subnet, IPAddress gateway, IPAddress dns_server);

  int maintain();
  void WoL(bool wol); // set Wake on LAN
  bool WoL(); // get the WoL state
  void phyMode(phyMode_t mode); // set PHYCFGR
  uint8_t phyState(); // returns the PHYCFGR
  uint8_t link(); // returns the linkstate, 1 = linked, 0 = no link
  const char* linkReport(); // returns the linkstate as a string
  uint8_t speed(); // returns speed in MB/s
  const char* speedReport(); // returns speed as a string
  uint8_t duplex(); // returns duplex mode 0 = no link, 1 = Half Duplex, 2 = Full Duplex
  const char* duplexReport(); // returns duplex mode as a string

  void setRtTimeOut(uint16_t timeout = 2000); // set the retransmission timout *100us
  uint16_t getRtTimeOut(); // get the retransmission timout
  void setRtCount(uint8_t count = 8); // set the retransmission count
  uint8_t getRtCount(); // get the retransmission count
  
  void macAddress(uint8_t mac[]); // get the MAC Address
  const char* macAddressReport(); // returns the the MAC Address as a string

  void setHostname(const char* hostname);
  
  IPAddress localIP();
  IPAddress subnetMask();
  IPAddress gatewayIP();
  IPAddress dnsServerIP();

  friend class EthernetClient;
  friend class EthernetServer;
};

extern EthernetClass Ethernet;

#endif
