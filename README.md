# EthernetW5500

Arduino Ethernet library for the Wiznet **W5500** chip. Forked from
[sstaub/Ethernet3](https://github.com/sstaub/Ethernet3) v1.6.0 for use in a
personal project, with a handful of reliability improvements layered on top.
Everything Ethernet3 already shipped (DHCP, TCP/UDP, multicast, PHY mode
selection, WoL, etc.) still works — the additions just expose more knobs and
fix a few rough edges.

> [!WARNING]
> The changes to the library involved a lot of ✨vibe coding✨
>
> Everything has been thoroughly tested and seems to work, but exercise caution

## Installation

This library is **not** in the Arduino Library Manager. Install it from a
release archive:

1. Go to the [Releases](https://github.com/SEBv15/EthernetW5500/releases) page
   and download the `.zip` for the latest release.
2. In the Arduino IDE: **Sketch → Include Library → Add .ZIP Library…** and
   select the downloaded file.
3. In your sketch:
   ```cpp
   #include <EthernetW5500.h>
   ```
   The class is still `EthernetClass` and the global is still `Ethernet`, so
   sketches written against Ethernet3 only need their `#include` updated.

## What's new compared to Ethernet3 1.6.0

### PHY auto-negotiation fallback

Some cables (long runs, MoCA / powerline bridges) negotiate fine with PC NICs
but make the W5500's simpler PHY thrash — the link bit briefly comes up, drops,
comes back, never stabilises. Forcing 10BT-FD makes it work reliably, but only
on those problem cables; you don't want to force it everywhere because then a
plain 100M-only switch won't link at all.

`setAutoNegFallback()` lets `Ethernet.begin()` try auto-neg first and fall back
to a fixed mode only if the link came up but never stayed up. If the link
never appears at all (cable unplugged), the PHY is left in auto-neg so a later
plug-in still negotiates correctly.

```cpp
// Defaults: stableMs = 1000, totalWaitMs = 3000.
Ethernet.setAutoNegFallback(FULL_DUPLEX_10);
Ethernet.begin(mac);  // does the wait + fallback internally
```

The `phyMode_t` values from Ethernet3 are reused (`HALF_DUPLEX_10`,
`FULL_DUPLEX_10`, `HALF_DUPLEX_100`, `FULL_DUPLEX_100`,
`FULL_DUPLEX_100_AUTONEG`, `POWER_DOWN`, `ALL_AUTONEG`). Pass `ALL_AUTONEG`
(or just don't call `setAutoNegFallback()`) to disable the workaround.

### Configurable startup delay

`W5500Class::init()` did a hard 1 second `delay()` before the first SPI
transaction to let the chip settle after power-up. If your sketch already
delays in `setup()` before reaching `Ethernet.begin()`, that second is wasted.
Now adjustable via the second arg to `Ethernet.init()`:

```cpp
Ethernet.init(8, 0);    // 8 sockets, no extra startup delay
Ethernet.init(8, 250);  // 8 sockets, 250 ms startup delay
Ethernet.init();        // 8 sockets, 1000 ms (default — same as before)
```

### Configurable DHCP timeouts

The bundled Arduino `Ethernet` library accepts DHCP timeouts in `begin()`;
Ethernet3 dropped them. Restored here, with the same signature:

```cpp
// timeout = total DHCP attempt budget (ms), responseTimeout = per-message wait (ms)
Ethernet.begin(mac, 4000, 1000);  // fail fast: 4 s total, 1 s per response
Ethernet.begin(mac);              // unchanged: 60 s / 5 s defaults
```

### Hardware-presence detection

`hardwareStatus()` returns whether a W5500 actually answered on SPI (probed
via `VERSIONR` during `begin()`). If no chip is present, `begin()` now returns
`0` immediately instead of grinding through the DHCP retry loop.

```cpp
if (Ethernet.hardwareStatus() == EthernetNoHardware) {
  // … chip not detected (CS wiring? power? wrong CS pin?)
}
```

`linkStatus()` returns the cable / link state as an enum, mirroring the
bundled Ethernet library:

```cpp
EthernetLinkStatus status = Ethernet.linkStatus();
// LinkON, LinkOFF, or Unknown (no W5500 detected)
```

The original `link()` (returning `uint8_t`) is still there.

### Per-socket interrupt mask

The W5500 can route per-socket events to the `INT` pin via `SIMR` + per-socket
`Sn_IMR`. Ethernet3 didn't expose either, and any direct `Sn_IMR` poke gets
clobbered the next time the library opens that socket (e.g. on `accept()`).

`enableInterrupts(simr, socketImr)` writes `SIMR` immediately and stores the
`Sn_IMR` mask, which `socket()` re-applies on every socket open — so
connection-recycle cycles can't strip it.

```cpp
// Forward all 8 sockets' CON + RECV events to the INT pin.
Ethernet.enableInterrupts(0xFF, SnIR::CON | SnIR::RECV);
```

Use any combination of `SnIR::CON`, `SnIR::DISCON`, `SnIR::RECV`,
`SnIR::TIMEOUT`, `SnIR::SEND_OK`. Set the corresponding bit in `simr` (one
bit per socket index, 0–7) for each socket whose events you want forwarded.

### `EthernetServer::accept()` returning a fresh client

Modern Arduino `Ethernet` API: `accept()` returns each newly ESTABLISHED
client exactly once, regardless of whether data has arrived yet — useful when
the server needs to send a greeting before the client says anything.
Ethernet3 only had `available()`, which waits for data first.

```cpp
EthernetClient c = server.accept();
if (c) {
  c.write("HELLO\n", 6);   // can write immediately
  // … remember c yourself; subsequent accept() won't return this socket again.
}
```

`available()` still works as before (returns clients with pending data, may
return the same client multiple times).

---

## Features inherited from Ethernet3

The sections below come from the upstream Ethernet3 README and document
behaviour that this fork has not changed.

### Custom DHCP hostname

```cpp
Ethernet.setHostname(char* hostname);  // call before Ethernet.begin(mac)
```

### PHY mode selection (runtime)

```cpp
Ethernet.phyMode(phyMode_t mode);  // call after Ethernet.begin()
```

`phyMode_t` values: `HALF_DUPLEX_10`, `FULL_DUPLEX_10`, `HALF_DUPLEX_100`,
`FULL_DUPLEX_100`, `FULL_DUPLEX_100_AUTONEG`, `POWER_DOWN`, `ALL_AUTONEG`
(default).

### Wake on LAN

```cpp
Ethernet.WoL(bool wol);
bool state = Ethernet.WoL();
```

### Static-IP `begin()` overloads

```cpp
Ethernet.begin(mac, ip, subnet, gateway, dns);
Ethernet.begin(ip, subnet, gateway, dns);  // for WIZ550io / RP Pico with auto-generated MAC
```

### Multicast UDP

```cpp
EthernetUDP udp;
udp.beginMulticast(multicastIP, port);
```

### Unicast blocking (UDP)

```cpp
udp.setUnicastBlock(true);
udp.setUnicastBlock(false);  // restore default
bool blocked = udp.getUnicastBlock();
```

### Broadcast blocking (UDP)

```cpp
udp.setBroadcastBlock(true);
udp.setBroadcastBlock(false);  // restore default
bool blocked = udp.getBroadcastBlock();
```

### PHY status helpers

```cpp
uint8_t  phy   = Ethernet.phyState();   // raw PHYCFGR
uint8_t  link  = Ethernet.link();       // 1 = linked, 0 = no link
uint8_t  speed = Ethernet.speed();      // 10 or 100 (MB/s)
uint8_t  dup   = Ethernet.duplex();     // 0 = no link, 1 = HD, 2 = FD
const char* linkStr   = Ethernet.linkReport();
const char* speedStr  = Ethernet.speedReport();
const char* duplexStr = Ethernet.duplexReport();
```

### MAC address

```cpp
uint8_t mac[6];
Ethernet.macAddress(mac);
const char* macStr = Ethernet.macAddressReport();
```

### Socket RAM size

Reduce socket count to give each socket more RX/TX buffer:

```cpp
Ethernet.init(1);  // 1 socket  × 16 k RX/TX
Ethernet.init(2);  // 2 sockets ×  8 k RX/TX
Ethernet.init(4);  // 4 sockets ×  4 k RX/TX
Ethernet.init();   // 8 sockets ×  2 k RX/TX (default)
```

`MAX_SOCK_NUM` in `utility/w5500.h` is fixed at 8 and cannot be changed at
runtime.

### CS / RST pins

```cpp
Ethernet.setCsPin(3);   // default: 10
Ethernet.setRstPin(4);  // default: 9
```

Both must be called before `Ethernet.begin(...)`.

### Soft / hard reset

```cpp
Ethernet.softreset();   // SPI-issued reset, after Ethernet.begin()
Ethernet.hardreset();   // toggles the configured RST pin
```

### TCP retransmission tuning

Reduce blocking on dead sockets. Timeout is in 100 µs units.

```cpp
Ethernet.setRtTimeOut(500);  // 50 ms base
Ethernet.setRtCount(2);
Ethernet.setRtTimeOut();     // restore default (2000 = 200 ms)
Ethernet.setRtCount();       // restore default (8)
```

### Per-client TCP options

```cpp
EthernetClient tcp;
tcp.setNoDelayedACK(true);
bool ack = tcp.getNoDelayedACK();

uint8_t ip[4];  tcp.remoteIP(ip);
uint8_t mac[6]; tcp.remoteMAC(mac);
```

### Per-UDP-socket helpers

```cpp
uint8_t ip[4];  udp.remoteIP(ip);
uint8_t mac[6]; udp.remoteMAC(mac);
```

---

## Credits

- Upstream library: [sstaub/Ethernet3](https://github.com/sstaub/Ethernet3),
  itself based on Arduino.org's Ethernet2 and the original Wiznet driver.
- Claude Code