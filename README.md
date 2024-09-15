# Stop-and-Wait Protocol Simulation

This project implements a simulation of the Stop-and-Wait protocol between multiple nodes in a network using CNET.

## Problem Setting

The Stop-and-Wait protocol, also known as the Alternating Bit Protocol, uses two sequence numbers (`0` and `1`) for each data packet and the same acknowledgement numbers to confirm the arrival of packets. A timer system is employed to manage packet retransmissions in case of timeouts.

### Key Features:

- **Sequence Numbers:** Alternating sequence numbers (0 and 1) for packet identification.
- **Acknowledgement Numbers:** Matching acknowledgement numbers for confirming packet receipt.
- **Timer System:** Used to manage retransmissions if an acknowledgement is not received within a certain timeframe.

## Test Topologies

The protocol is tested on various topology configurations:

1. **Basic Implementation:** 2 hosts to demonstrate the basic functioning of the protocol.
2. **Multiple Connections:** 5 hosts interconnected with 1 host in the middle, showcasing multiple connections.
3. **Router Connection:** 2 hosts with a router connecting them to illustrate the protocol's functionality with routers.
4. **Complex Topology:** 4 hosts and 2 routers, demonstrating the protocol's performance in more complex networks.

## Implementation Details

- **Connection Encapsulation:** Maintains state information for each connection, including the next sequence number, expected acknowledgement, and destination address.
- **Routing Table:** Tracks which link is used for which destination address, enabling proper routing of packets.
- **Network Encapsulation:** Manages multiple connections within the network simulation.

## Tech Stack

- CNET

## How to Run

Follow these steps to compile and run the simulation:

1. **Install CNET:** Ensure CNET is installed on your system. You can download it from the official [CNET website](https://www.csse.uwa.edu.au/cnet/).
   
2. **Compile the Source Code:** Compile the simulation using the following command:
   ```bash
    gcc -o stopandwait stopandwait.c

3. **Run CNET**:
  ```bash
    CNET stopandwait
