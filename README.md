# CS378 Networks Lab : Link State Emulator

## Team
| Name | Roll No.|
| --- | --- |
| Sagar V | 24B1021 |
| Tamanna Kumari | 24B1015 |

## Overview
An emulation of a link state routing protocol using TCP and UDP sockets.
- **Oracle Node (ON)**, written in **Java**: reads the network topology from a config file and tells every Virtual Node who it is and who its neighbours are (over TCP). It re-reads the config file while running, so link costs can change during the demo.
- **Virtual Node (VN)**, written in **C++**: connects to the ON, learns its neighbours, then exchanges Link State Packets (LSPs) directly with them (over UDP) and builds a picture of the whole network.

The ON is only used for configuration. LSPs travel directly between VNs and never go through the ON.

```
   ON --- TCP: CONNECT (VN -> ON), LINK-STATE (ON -> VN) ---> each VN
   VN <--- UDP: LSP (flooded between neighbouring VNs) ---> VN
```

## Requirements
- `g++` and `make`
- a JDK, version 11 or newer (`javac -version`)
- a network connection with a real (non-loopback) IPv4 address. The spec forbids `127.0.0.1` / `0.0.0.0`, and the VN refuses to start with a loopback address as its own IP.

## Folder Structure
```
oracle/      Oracle Node (Java): OracleNode.java (event loop), Config.java (config parser), Protocol.java (constants)
vn/          Virtual Node (C++): vn.cpp, protocol.h (constants + message packing/parsing)
Makefile
```

## Running Instructions
Run the following commands from base directory of `cs378-link-state-emulator`
```bash
make # run to compile all the files

java -cp build oracle.OracleNode <path-to-config-file>

./vn/vn <ON-IP> <VN-port> # without giving VN IP address (derives it programmatically) and ON Port (uses default)

./vn/vn ON-IP:ON-port <port> <VN-IP> # ON-port and VN IP address are optional but if they are included then use this format
```

Default ON Port: **5000**

### Shortcuts (Makefile)
| Command | What it does |
| --- | --- |
| `make` | builds the VN (`vn/vn`) and the ON (`build/`) |
| `make run-oracle CONFIG=<path-to-config>` | builds and runs ON with given config file |
| `make run-vn PORT=5001` | runs a VN with UDP port 5001. The ON IP defaults to the running machine's first address (`hostname -I`) |
| `make run-vn PORT=5001 ON_IP=192.168.1.5` | runs a VN against an ON on another machine |
| `make clean` | removes the binary and `build/` |


## Config File Format
Same as the problem statement: an adjacency matrix, only the upper-right triangle is given, since link costs are symmetric.
- Lines starting with `#` and blank lines are ignored; tabs/spaces at the start of a line are ignored
- Row *i* (A is the first row) lists the costs from node *i* to the nodes after it, so the first row has N-1 entries and the last has 1
- A cost is a positive integer, or `-1` if there is no link
- At most 26 nodes (A to Z)

The ON checks all of this at startup and exits with a message that names the line if the file is wrong. While running, a broken edit is reported and the previous config stays running.

## Messages (binary, big-endian)
TCP messages (ON and VN) begin with a 3-byte header: `type (1 byte) | body length (2 bytes)`. This is because TCP is a byte stream, so one `recv` can return half a message or two. UDP keeps message boundaries, so LSPs have no length header.

| Message | Direction | Transport | Layout |
| --- | --- | --- | --- |
| CONNECT (type 1) | VN to ON | TCP | header, then `IPv4 (4 bytes) \| UDP port (2)` |
| LINK-STATE (type 2) | ON to VN | TCP | header, then `count k (1)` and `k` tuples of `letter (1) \| IPv4 (4) \| UDP port (2) \| cost (4)`. Exactly one tuple has cost 0: it is the receiving VN itself. The others are its neighbours (links at -1 are left out) |
| LSP (type 3) | VN to VN | UDP | `type (1) \| origin (1) \| sender (1) \| seq (4) \| count k (1)`, then `k` entries of `neighbour letter (1) \| cost (4)`. Length is `8 + 5k` |

- `origin` is the VN the LSP is about, and stays the same as the LSP is forwarded. `sender` is the VN that sent this particular copy, and is changed at every hop so a VN doesn't send an LSP straight back to where it came from.
- `seq` is increased by the origin every time it creates a new LSP. A VN only accepts an LSP whose `seq` is higher than what it already has for that origin; everything else is dropped
- Malformed messages (wrong length, bad letters, zero cost, ...) are dropped.

## Non-trivial Design Choices
- Each LSP is sent twice for reliability (though mentioned that we can assume that no UDP packets will be lost)
- Prints graph to terminal every 10 seconds
- Sends out LSP periodically also every 10 seconds (kept low for testing/demo purposes)
- A new LSP is also sent every time a new LINK-STATE arrives from the ON
- Connections beyond the number of nodes in the config ("island" nodes) are accepted and told they have no neighbours (a LINK-STATE with only themselves)
- If there are more than 26 incoming connections to ON, the additional nodes are ignored and connection closed
- The ON sends LINK-STATE only after CONNECT has arrived from all N nodes in the config, and again after every change to the config file. Saving the file without changing the graph does nothing
- The ON checks the config file about once a second (modified time and size) and re-reads it when it changed
- Non blocking syscalls used in ON (Java)
- Blocking syscalls used in VN (C++) with `select()`
- Both programs are single threaded and event driven. The VN waits on the TCP and UDP sockets together with `select()`, and only calls `recv`/`recvfrom` once `select()` says there is data, so it never gets stuck on one socket. The ON does the same thing with a Java NIO `Selector`, which needs non-blocking channels
- TCP receive buffers are reassembled by hand (a partial message waits for the rest, and two messages in one read are both handled)
- The VN works out its own IP address from the TCP connection to the ON (`getsockname`), so it is the address of the interface that actually reaches the ON. It can be overridden on the command line
- The ON and VN listening sockets bind to all interfaces, but the addresses sent in messages are always real ones, never `127.0.0.1` or `0.0.0.0`
- Message encoding is done by hand, one field at a time, in network byte order, instead of sending raw structs (which would differ between machines because of padding and endianness)
- If a VN's connection to the ON closes, the VN exits
- The number of nodes is fixed when the ON starts. A change in the number of nodes in the config file is ignored
- A VN that disconnects and connects again is treated as a new node by the ON: letters are not reused


### Seeing every LSP
Set `VERBOSE` to `true` at the top of `vn/vn.cpp` and rebuild. The VN then logs each LSP it sends, stores, forwards or drops. It is off by default, since duplicates are dropped all the time and the output gets noisy. This is more for debugging purposes and they don't really serve a purpose aside from that


## Troubleshooting
| Message | Likely cause |
| --- | --- |
| `connect to oracle: Connection refused` | the ON is not running yet, or the wrong ON IP or port was given |
| `udp bind: Address already in use` | another VN (or program) is already using that UDP port |
| `own IP is loopback/0.0.0.0 invalid` | no network connection. Pass a real address as the 3rd argument |
| ON: `Address already in use` on start | an old ON is still running on port 5000 |
| VNs print nothing after connecting | normal until all N VNs have connected: the ON waits for the last CONNECT before sending LINK-STATE |
