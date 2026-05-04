/*
 modified 12 Aug 2013
 by Soohwan Kim (suhwan@wiznet.co.kr)

- 10 Apr. 2015
 Added support for Arduino Ethernet Shield 2
 by Arduino.org team

 */

#include "EthernetW5500.h"
#include "Dhcp.h"

// XXX: don't make assumptions about the value of MAX_SOCK_NUM.
uint8_t EthernetClass::_state[MAX_SOCK_NUM] = { 0, };
uint16_t EthernetClass::_server_port[MAX_SOCK_NUM] = { 0, };

void EthernetClass::setRstPin(uint8_t pinRST) {
  _pinRST = pinRST;
  pinMode(_pinRST, OUTPUT);
  digitalWrite(_pinRST, HIGH);
  }
void EthernetClass::setCsPin(uint8_t pinCS) {
  _pinCS = pinCS;
  }

void EthernetClass::init(uint8_t maxSockNum, uint16_t startupDelayMs) {
  _maxSockNum = maxSockNum;
  _startupDelayMs = startupDelayMs;
  // Force a fresh chip init on the next begin().
  _chipInitialized = false;
  }

void EthernetClass::setAutoNegFallback(phyMode_t fallback,
                                       uint16_t stableMs,
                                       uint16_t totalWaitMs) {
  _autoNegFallback = fallback;
  _autoNegStableMs = stableMs;
  _autoNegTotalMs = totalWaitMs;
  }

void EthernetClass::enableInterrupts(uint8_t simr, uint8_t socketImr) {
  _simr = simr;
  _socketImr = socketImr;
  // SIMR is a global register, write it now if the chip is up.
  if (_hwStatus == EthernetW5500) {
    w5500.writeSIMR(simr);
    for (uint8_t s = 0; s < MAX_SOCK_NUM; s++) {
      w5500.writeSnIMR(s, socketImr);
      }
    }
  }

EthernetHardwareStatus EthernetClass::hardwareStatus() {
  return _hwStatus;
  }

EthernetLinkStatus EthernetClass::linkStatus() {
  if (_hwStatus == EthernetNoHardware) return Unknown;
  return (w5500.getPHYCFGR() & 0x01) ? LinkON : LinkOFF;
  }

// Apply the configured auto-neg fallback. Called from begin() between
// w5500.init() and DHCP. Quietly returns if no fallback is configured.
// Mirrors the original ring_light.ino:151-179 block: only forces the fallback
// when link came up but never stabilised; if link never appeared at all (e.g.
// cable unplugged) the PHY is left in auto-neg so a later plug-in into a
// 100M-only switch still negotiates.
static void applyAutoNegFallback(phyMode_t fallback,
                                 uint16_t stableMs,
                                 uint16_t totalWaitMs) {
  if (fallback == ALL_AUTONEG) return;

  bool everUp = false;
  bool stable = false;
  uint16_t consecutiveUp = 0;
  const uint16_t step = 100;
  uint16_t needed = stableMs / step;
  if (needed == 0) needed = 1;

  for (uint16_t elapsed = 0; elapsed < totalWaitMs; elapsed += step) {
    delay(step);
    if (w5500.getPHYCFGR() & 0x01) {
      everUp = true;
      if (++consecutiveUp >= needed) {
        stable = true;
        break;
        }
      }
    else {
      consecutiveUp = 0;
      }
    }

  if (stable) {
    uint8_t phy = w5500.getPHYCFGR();
    Serial.print(F("PHY: auto-neg ok, "));
    Serial.print(((phy >> 1) & 1) ? F("100M") : F("10M"));
    Serial.println(((phy >> 2) & 1) ? F("-FD") : F("-HD"));
    }
  else if (everUp) {
    Serial.println(F("PHY: auto-neg unstable, forcing fallback"));
    Ethernet.phyMode(fallback);
    // phyMode() toggles PHY reset back-to-back with no settle. Give the PHY
    // time to come up under the new mode before begin() proceeds to DHCP.
    delay(50);
    }
  else {
    Serial.println(F("PHY: no link yet (cable unplugged?)"));
    }
  }

bool EthernetClass::_initChip() {
  // Idempotent: skip the heavy work (soft-reset, PHY wait) on every begin()
  // after the first. Without this, retrying begin() in a DHCP loop keeps
  // soft-resetting the PHY, dropping the link briefly each time, so it never
  // gets enough uninterrupted time to negotiate.
  if (_chipInitialized) return _hwStatus == EthernetW5500;

  w5500.init(_maxSockNum, _pinCS, _startupDelayMs);
  // VERSIONR is 0x04 on a real W5500. Anything else (typically 0xFF when SPI
  // floats with no chip present) means no hardware.
  _hwStatus = (w5500.readVersion() == 0x04) ? EthernetW5500 : EthernetNoHardware;
  if (_hwStatus == EthernetNoHardware) return false;
  applyAutoNegFallback(_autoNegFallback, _autoNegStableMs, _autoNegTotalMs);
  // SIMR is global and survives chip init, but the per-socket Sn_IMR is
  // re-applied each time socket() opens a socket. Touch SIMR here so the
  // first call to enableInterrupts() before begin() still takes effect.
  if (_simr) w5500.writeSIMR(_simr);
  _chipInitialized = true;
  return true;
  }

uint8_t EthernetClass::softreset() {
  // Force the next begin() to re-do chip init (soft-reset, PHY wait, etc.).
  _chipInitialized = false;
  return w5500.softReset();
  }

void EthernetClass::hardreset() {
  // Force the next begin() to re-do chip init.
  _chipInitialized = false;
  if(_pinRST != 0) {
    digitalWrite(_pinRST, LOW);
    delay(1);
    digitalWrite(_pinRST, HIGH);
    delay(150);
    }
  }

#if defined(WIZ550io_WITH_MACADDRESS) || defined(PICO_RP2350) || defined(PICO_RP2040)

int EthernetClass::begin(void)
{
  uint8_t mac_address[6] ={0,};
  _dhcp = new DhcpClass();

  // Initialise the basic info
  if (!_initChip()) return 0;
  w5500.setIPAddress(IPAddress(0,0,0,0).raw_address());
  #if defined(WIZ550io_WITH_MACADDRESS)
    w5500.getMACAddress(mac_address);
  #endif
  #if defined(PICO_RP2350) || defined(PICO_RP2040)
    uint32_t rnd1 = get_rand_32();
    mac_address[0] = rnd1 >> 24;
    mac_address[1] = rnd1 >> 16;
    mac_address[2] = rnd1 >> 8;
    mac_address[3] = rnd1;
    uint32_t rnd2 = get_rand_32();
    mac_address[4] = rnd2 >> 24;
    mac_address[5] = rnd2 >> 16;

    mac_address[0] &= ~(1 << 0); // unicast = 0, multicast / broadcast = 1
    mac_address[0] |= 1 << 1; // local = 1, universal = 0
    mac_address[0] &= ~(1 << 2) & ~(1 << 3); // administratively assigned

    w5500.setMACAddress(mac_address);
  #endif

  if (strlen(_customHostname) != 0)
  {
    _dhcp->setCustomHostname(_customHostname);
  }
  
  // Now try to get our config info from a DHCP server
  int ret = _dhcp->beginWithDHCP(mac_address);
  if(ret == 1)
  {
    // We've successfully found a DHCP server and got our configuration info, so set things
    // accordingly
    w5500.setIPAddress(_dhcp->getLocalIp().raw_address());
    w5500.setGatewayIp(_dhcp->getGatewayIp().raw_address());
    w5500.setSubnetMask(_dhcp->getSubnetMask().raw_address());
    _dnsServerAddress = _dhcp->getDnsServerIp();
  }
  return ret;
}

void EthernetClass::begin(IPAddress local_ip)
{
  IPAddress subnet(255, 255, 255, 0);
  begin(local_ip, subnet);
}

void EthernetClass::begin(IPAddress local_ip, IPAddress subnet)
{
  // Assume the gateway will be the machine on the same network as the local IP
  // but with last octet being '1'
  IPAddress gateway = local_ip;
  gateway[3] = 1;
  begin(local_ip, subnet, gateway);
}

void EthernetClass::begin(IPAddress local_ip, IPAddress subnet, IPAddress gateway)
{
  // Assume the DNS server will be the machine on the same network as the local IP
  // but with last octet being '1'
  IPAddress dns_server = local_ip;
  dns_server[3] = 1;
  begin(local_ip, subnet, gateway, dns_server);
}

void EthernetClass::begin(IPAddress local_ip, IPAddress subnet, IPAddress gateway, IPAddress dns_server)
{
  _initChip();
  #if defined(PICO_RP2350) || defined(PICO_RP2040)
    uint8_t mac[6];
    uint32_t rnd1 = get_rand_32();
    mac[0] = rnd1 >> 24;
    mac[1] = rnd1 >> 16;
    mac[2] = rnd1 >> 8;
    mac[3] = rnd1;
    uint32_t rnd2 = get_rand_32();
    mac[4] = rnd2 >> 24;
    mac[5] = rnd2 >> 16;

    mac[0] &= ~(1 << 0); // unicast = 0, multicast / broadcast = 1
    mac[0] |= 1 << 1; // local = 1, universal = 0
    mac[0] &= ~(1 << 2) & ~(1 << 3); // administratively assigned

    w5500.setMACAddress(mac);
  #endif
  w5500.setIPAddress(local_ip.raw_address());
  w5500.setGatewayIp(gateway.raw_address());
  w5500.setSubnetMask(subnet.raw_address());
  _dnsServerAddress = dns_server;
}

#endif

int EthernetClass::begin(uint8_t *mac_address)
{
  return begin(mac_address, 60000UL, 5000UL);
}

int EthernetClass::begin(uint8_t *mac_address, unsigned long timeout, unsigned long responseTimeout)
{
  _dhcp = new DhcpClass();
  // Initialise the basic info
  if (!_initChip()) return 0;
  w5500.setMACAddress(mac_address);
  w5500.setIPAddress(IPAddress(0,0,0,0).raw_address());

  if (strlen(_customHostname) != 0)
  {
    _dhcp->setCustomHostname(_customHostname);
  }

  // Now try to get our config info from a DHCP server
  int ret = _dhcp->beginWithDHCP(mac_address, timeout, responseTimeout);
  if(ret == 1)
  {
    // We've successfully found a DHCP server and got our configuration info, so set things
    // accordingly
    w5500.setIPAddress(_dhcp->getLocalIp().raw_address());
    w5500.setGatewayIp(_dhcp->getGatewayIp().raw_address());
    w5500.setSubnetMask(_dhcp->getSubnetMask().raw_address());
    _dnsServerAddress = _dhcp->getDnsServerIp();
  }

  return ret;
}

void EthernetClass::begin(uint8_t *mac_address, IPAddress local_ip)
{
  IPAddress subnet(255, 255, 255, 0);
  begin(mac_address, local_ip, subnet);
}

void EthernetClass::begin(uint8_t *mac_address, IPAddress local_ip, IPAddress subnet)
{
  // Assume the gateway will be the machine on the same network as the local IP
  // but with last octet being '1'
  IPAddress gateway = local_ip;
  gateway[3] = 1;
  begin(mac_address, local_ip, subnet, gateway);
}

void EthernetClass::begin(uint8_t *mac_address, IPAddress local_ip, IPAddress subnet, IPAddress gateway)
{
  // Assume the DNS server will be the machine on the same network as the local IP
  // but with last octet being '1'
  IPAddress dns_server = local_ip;
  dns_server[3] = 1;
  begin(mac_address, local_ip, subnet, gateway, dns_server);
}

void EthernetClass::begin(uint8_t *mac, IPAddress local_ip, IPAddress subnet, IPAddress gateway, IPAddress dns_server)
{
  _initChip();
  w5500.setMACAddress(mac);
  w5500.setIPAddress(local_ip.raw_address());
  w5500.setGatewayIp(gateway.raw_address());
  w5500.setSubnetMask(subnet.raw_address());
  _dnsServerAddress = dns_server;
}

int EthernetClass::maintain(){
  int rc = DHCP_CHECK_NONE;
  if(_dhcp != NULL){
    //we have a pointer to dhcp, use it
    rc = _dhcp->checkLease();
    switch ( rc ){
      case DHCP_CHECK_NONE:
        //nothing done
        break;
      case DHCP_CHECK_RENEW_OK:
      case DHCP_CHECK_REBIND_OK:
        //we might have got a new IP.
        w5500.setIPAddress(_dhcp->getLocalIp().raw_address());
        w5500.setGatewayIp(_dhcp->getGatewayIp().raw_address());
        w5500.setSubnetMask(_dhcp->getSubnetMask().raw_address());
        _dnsServerAddress = _dhcp->getDnsServerIp();
        break;
      default:
        //this is actually a error, it will retry though
        break;
    }
  }
  return rc;
}

void EthernetClass::WoL(bool wol) { 
  uint8_t val = w5500.readMR();
  bitWrite(val, 5, wol);
  w5500.writeMR(val);
  }

bool EthernetClass::WoL() {
  uint8_t val = w5500.readMR();
  return bitRead(val, 5);
  }

void EthernetClass::phyMode(phyMode_t mode) {
  uint8_t val = w5500.getPHYCFGR();
  bitWrite(val, 6, 1);
  if (mode == HALF_DUPLEX_10) {
    bitWrite(val, 3, 0);
    bitWrite(val, 4, 0);
    bitWrite(val, 5, 0);
    w5500.setPHYCFGR(val);
    }
  else if (mode == FULL_DUPLEX_10) {
    bitWrite(val, 3, 1);
    bitWrite(val, 4, 0);
    bitWrite(val, 5, 0);
    w5500.setPHYCFGR(val);
    }
  else if (mode == HALF_DUPLEX_100) {
    bitWrite(val, 3, 0);
    bitWrite(val, 4, 1);
    bitWrite(val, 5, 0);
    w5500.setPHYCFGR(val);
    }
  else if (mode == FULL_DUPLEX_100) {
    bitWrite(val, 3, 1);
    bitWrite(val, 4, 1);
    bitWrite(val, 5, 0);
    w5500.setPHYCFGR(val);
    }
  else if (mode == FULL_DUPLEX_100_AUTONEG) {
    bitWrite(val, 3, 0);
    bitWrite(val, 4, 0);
    bitWrite(val, 5, 1);
    w5500.setPHYCFGR(val);
    }
  else if (mode == POWER_DOWN) {
    bitWrite(val, 3, 0);
    bitWrite(val, 4, 1);
    bitWrite(val, 5, 1);
    w5500.setPHYCFGR(val);
    }
  else if (mode == ALL_AUTONEG) {
    bitWrite(val, 3, 1);
    bitWrite(val, 4, 1);
    bitWrite(val, 5, 1);
    w5500.setPHYCFGR(val);
    }
  bitWrite(val, 7, 0);
  w5500.setPHYCFGR(val);
  bitWrite(val, 7, 1);
  w5500.setPHYCFGR(val);
  }

void EthernetClass::setHostname(const char* hostname) {
  memset(_customHostname, 0, 32);
  memcpy((void*)_customHostname, (void*)hostname, strlen(hostname) >= 31 ? 31 : strlen(hostname));
  }

uint8_t EthernetClass::phyState() {
  return w5500.getPHYCFGR();
  }

uint8_t EthernetClass::link() {
  return bitRead(w5500.getPHYCFGR(), 0);
  }

const char* EthernetClass::linkReport() {
  if(bitRead(w5500.getPHYCFGR(), 0) == 1) return "LINK";
  else return "NO LINK";
  }

uint8_t EthernetClass::speed() {
  if(bitRead(w5500.getPHYCFGR(), 0) == 1) {
    if(bitRead(w5500.getPHYCFGR(), 1) == 1) return 100;
    if(bitRead(w5500.getPHYCFGR(), 1) == 0) return 10;
    }
   return 0;
  }

const char* EthernetClass::speedReport() {
  if(bitRead(w5500.getPHYCFGR(), 0) == 1) {
    if(bitRead(w5500.getPHYCFGR(), 1) == 1) return "100 MB";
    if(bitRead(w5500.getPHYCFGR(), 1) == 0) return "10 MB";
    }
   return "NO LINK";
  }

uint8_t EthernetClass::duplex() {
  if(bitRead(w5500.getPHYCFGR(), 0) == 1) {
    if(bitRead(w5500.getPHYCFGR(), 2) == 1) return 2;
    if(bitRead(w5500.getPHYCFGR(), 2) == 0) return 1;
    }
   return 0;
  }

const char* EthernetClass::duplexReport() {
  if(bitRead(w5500.getPHYCFGR(), 0) == 1) {
    if(bitRead(w5500.getPHYCFGR(), 2) == 1) return "FULL DUPLEX";
    if(bitRead(w5500.getPHYCFGR(), 2) == 0) return "HALF DUPLEX";
    }
   return "NO LINK";
  }

void EthernetClass::setRtTimeOut(uint16_t timeout) {
  w5500.setRetransmissionTime(timeout);
  }

uint16_t EthernetClass::getRtTimeOut() {
  return w5500.getRetransmissionTime();
  }

void EthernetClass::setRtCount(uint8_t count) {
  w5500.setRetransmissionCount(count);
  }

uint8_t EthernetClass::getRtCount() {
  return w5500.getRetransmissionCount();
  }

void EthernetClass::macAddress(uint8_t mac[]) {
  w5500.getMACAddress(mac);
  }

const char* EthernetClass::macAddressReport() {
  uint8_t mac[6];
  static char str[18];
  w5500.getMACAddress(mac);
  sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return str;
  }

IPAddress EthernetClass::localIP()
{
  IPAddress ret;
  w5500.getIPAddress(ret.raw_address());
  return ret;
}

IPAddress EthernetClass::subnetMask()
{
  IPAddress ret;
  w5500.getSubnetMask(ret.raw_address());
  return ret;
}

IPAddress EthernetClass::gatewayIP()
{
  IPAddress ret;
  w5500.getGatewayIp(ret.raw_address());
  return ret;
}

IPAddress EthernetClass::dnsServerIP()
{
  return _dnsServerAddress;
}

EthernetClass Ethernet;
