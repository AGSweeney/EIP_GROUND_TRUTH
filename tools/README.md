# Testing Tools

## ACD Conflict Simulator

`test_acd_conflict.py` - Python script to simulate IP address conflicts for testing Address Conflict Detection (ACD).

### Requirements

```bash
pip install scapy
```

**Note:** On Windows, you may need to install Npcap (WinPcap successor) for Scapy to work:
- Download from: https://npcap.com/
- Install Npcap (not Npcap SDK)

On Linux, you may need to run with `sudo` for raw socket access.

### Usage Examples

#### Send ARP announcements claiming an IP

```bash
# Send 3 ARP announcements
python test_acd_conflict.py --ip 172.16.82.100 --interface eth0 --count 3

# Continuously send announcements every 2 seconds
python test_acd_conflict.py --ip 172.16.82.100 --interface eth0 --continuous

# Use a specific MAC address
python test_acd_conflict.py --ip 172.16.82.100 --interface eth0 --mac 02:aa:bb:cc:dd:ee
```

#### Listen for ARP probes and automatically respond

This mode listens for ARP probes from the ESP32 and automatically sends ARP replies, simulating a device that already has the IP:

```bash
python test_acd_conflict.py --ip 172.16.82.100 --interface eth0 --respond-to-probes --duration 60
```

### Finding Your Network Interface

**Windows:**
```powershell
# List interfaces
python -c "from scapy.all import get_if_list; print('\n'.join(get_if_list()))"

# Common names: "Ethernet", "Ethernet 2", etc.
```

**Linux:**
```bash
# List interfaces
ip link show
# Common names: eth0, enp0s3, etc.
```

**macOS:**
```bash
# List interfaces
ifconfig
# Common names: en0, en1, etc.
```

### Testing Scenarios

1. **During Boot Probe Phase**: Run `--respond-to-probes` mode before booting the ESP32. The script will automatically respond to the ESP32's ARP probes, causing a conflict to be detected.

2. **After IP Assignment**: Run `--continuous` mode after the ESP32 has assigned its IP. The script will send periodic ARP announcements claiming the same IP, which should trigger ongoing conflict detection.

3. **Manual Testing**: Use `--count 1` to send a single ARP announcement at a specific time.

### Expected Behavior

When a conflict is detected, the ESP32 should:
- Log an ACD conflict error
- Set the user LED to solid (instead of blinking)
- Not assign the IP address (during probe phase)
- Report conflict details via EtherNet/IP Attribute #11

