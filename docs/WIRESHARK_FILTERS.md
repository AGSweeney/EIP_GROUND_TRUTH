# Wireshark Filters for ACD and ARP Debugging

## ARP Probe Filters

### Basic ARP Probe Filter
ARP probes are identified by having a source IP address of 0.0.0.0:
```
arp.opcode == 1 && arp.src.proto_ipv4 == 0.0.0.0
```

### ARP Probes for Specific IP
Filter for probes targeting a specific IP address (e.g., 172.16.82.100):
```
arp.opcode == 1 && arp.src.proto_ipv4 == 0.0.0.0 && arp.dst.proto_ipv4 == 172.16.82.100
```

### ARP Probes from Specific MAC Address
Filter for probes from a specific device (e.g., 30:ed:a0:e3:34:c1):
```
arp.opcode == 1 && arp.src.proto_ipv4 == 0.0.0.0 && arp.src.hw_mac == 30:ed:a0:e3:34:c1
```

### All ARP Probes on Network
Shows all ARP probes from any device:
```
arp.opcode == 1 && arp.src.proto_ipv4 == 0.0.0.0
```

## ARP Announcement Filters

### ARP Announcements
ARP announcements have the source IP matching the target IP:
```
arp.opcode == 1 && arp.src.proto_ipv4 == arp.dst.proto_ipv4 && arp.src.proto_ipv4 != 0.0.0.0
```

### ARP Announcements for Specific IP
```
arp.opcode == 1 && arp.src.proto_ipv4 == 172.16.82.100 && arp.dst.proto_ipv4 == 172.16.82.100
```

## ARP Conflict Detection Filters

### ARP Replies That Could Indicate Conflicts
Shows ARP replies from devices responding to probes:
```
arp.opcode == 2 && (arp.src.proto_ipv4 == 172.16.82.100 || arp.dst.proto_ipv4 == 172.16.82.100)
```

### Complete ACD Sequence for Specific IP
Shows probes, announcements, and potential conflicts for a specific IP:
```
(arp.src.proto_ipv4 == 172.16.82.100 || arp.dst.proto_ipv4 == 172.16.82.100) && (arp.src.proto_ipv4 == 0.0.0.0 || arp.src.proto_ipv4 == arp.dst.proto_ipv4)
```

## General ARP Filters

### All ARP Traffic
```
arp
```

### ARP Requests Only
```
arp.opcode == 1
```

### ARP Replies Only
```
arp.opcode == 2
```

### ARP Traffic for Specific IP (any ARP activity)
```
arp.src.proto_ipv4 == 172.16.82.100 || arp.dst.proto_ipv4 == 172.16.82.100
```

## ESP32-P4 Specific Filters

### Complete Filter for Your Device (MAC: 30:ed:a0:e3:34:c1)
**Captures ALL traffic related to your ESP32 device:**
```
eth.addr == 30:ed:a0:e3:34:c1
```

This filter shows:
- All Ethernet frames sent by your device (source MAC)
- All Ethernet frames sent to your device (destination MAC)
- All ARP packets (probes, announcements, replies)
- All TCP/IP traffic (EtherNet/IP, Modbus TCP, HTTP, etc.)
- Broadcast frames where your device is the source

### ARP Probes from Your ESP32 Device
Replace `30:ed:a0:e3:34:c1` with your device's MAC address:
```
arp.opcode == 1 && arp.src.proto_ipv4 == 0.0.0.0 && arp.src.hw_mac == 30:ed:a0:e3:34:c1
```

### Complete ACD Sequence from ESP32
Shows all ACD-related ARP traffic from your device:
```
arp.src.hw_mac == 30:ed:a0:e3:34:c1 && (arp.src.proto_ipv4 == 0.0.0.0 || arp.src.proto_ipv4 == arp.dst.proto_ipv4)
```

### All ARP Traffic Involving Your Device
Shows all ARP packets where your device is involved (as source or destination):
```
arp.src.hw_mac == 30:ed:a0:e3:34:c1 || arp.dst.hw_mac == 30:ed:a0:e3:34:c1
```

## Example Usage

1. **Capture ARP probes during boot**: Use filter `arp.opcode == 1 && arp.src.proto_ipv4 == 0.0.0.0` to see all ARP probes on the network.

2. **Verify ACD is working**: Filter for `arp.src.proto_ipv4 == 0.0.0.0 && arp.dst.proto_ipv4 == 172.16.82.100` to see probes for your IP.

3. **Check for conflicts**: Filter for `arp.opcode == 2 && arp.src.proto_ipv4 == 172.16.82.100` to see if another device is claiming your IP.

4. **Monitor your device**: Filter for `arp.src.hw_mac == 30:ed:a0:e3:34:c1` to see all ARP traffic from your ESP32.

## Notes

- **ARP Probes**: Source IP = 0.0.0.0, asking "Who has IP X?"
- **ARP Announcements**: Source IP = IP X, announcing "I have IP X"
- **ARP Replies**: Opcode = 2, responding to a probe or announcement
- **Conflicts**: Detected when a probe receives a reply, or when an announcement receives a conflicting reply

## RFC 5227 ACD Behavior

1. **Probe Phase**: Device sends 3 ARP probes (source IP = 0.0.0.0) asking "Who has IP X?"
2. **Announce Phase**: If no conflicts, device sends 2 ARP announcements (source IP = IP X)
3. **Ongoing Phase**: Device periodically sends defensive ARP announcements

Use these filters to verify each phase is working correctly.

