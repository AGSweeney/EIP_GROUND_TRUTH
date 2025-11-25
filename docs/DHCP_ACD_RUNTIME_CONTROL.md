# DHCP ACD Runtime Control Implementation Guide

## Overview

This document details the changes required to enable runtime control of Address Conflict Detection (ACD) for DHCP-assigned IP addresses via EtherNet/IP CIP Attribute configuration. Currently, DHCP ACD is controlled only by the compile-time configuration `CONFIG_LWIP_DHCP_DOES_ACD_CHECK`.

## Current State Analysis

### Current DHCP ACD Behavior

1. **Compile-Time Control**: DHCP ACD is enabled/disabled via `CONFIG_LWIP_DHCP_DOES_ACD_CHECK` in `sdkconfig`
2. **Static IP ACD**: Already supports runtime control via EtherNet/IP Attribute #10 (`select_acd`)
3. **DHCP Flow**: When DHCP receives an offer:
   - `dhcp_check()` is called
   - If `LWIP_DHCP_DOES_ACD_CHECK` is enabled, `acd_start()` is called
   - ACD callback (`dhcp_conflict_callback`) handles results:
     - `ACD_IP_OK`: Calls `dhcp_bind()` to accept IP
     - `ACD_RESTART_CLIENT`: Waits 10s then restarts DHCP
     - `ACD_DECLINE`: Sends DHCPDECLINE and removes IP

### Current NVS Storage Structure

**File**: `components/opener/src/ports/nvdata/nvtcpip.c`

```c
typedef struct __attribute__((packed)) {
  uint32_t version;              // Currently version 2
  uint32_t config_control;
  uint32_t ip_address;
  uint32_t network_mask;
  uint32_t gateway;
  uint32_t name_server;
  uint32_t name_server2;
  uint16_t domain_length;
  uint16_t hostname_length;
  uint8_t domain[TCPIP_DOMAIN_MAX_LEN];
  uint8_t hostname[TCPIP_HOSTNAME_MAX_LEN];
  uint8_t select_acd;            // Static IP ACD flag
  // Missing: select_dhcp_acd
} TcpipNvBlob;
```

### Current TCP/IP Interface Object Attributes

**File**: `components/opener/src/cip/ciptcpipinterface.c`

- Attribute #10: `select_acd` (Static IP ACD) - ✅ Implemented
- Attribute #11: `Last Conflict Detected` - ✅ Implemented
- Attribute #12: Currently a **dummy attribute** (`kGetableAllDummy`) - ⚠️ **CONFLICT**
- Attribute #13: `encapsulation_inactivity_timeout` - ✅ Implemented

## Required Changes

### 1. Attribute Number Selection

**Issue**: Attribute #12 is currently registered as a dummy attribute for `GetAttributeAll` compatibility.

**Options**:
- **Option A**: Replace dummy Attribute #12 with real implementation (recommended)
- **Option B**: Use Attribute #14 (next available)

**Recommendation**: Use **Option A** - Replace Attribute #12 dummy with real implementation. This maintains attribute numbering consistency.

**Impact**: 
- `GetAttributeAll` will now return real data instead of dummy data for Attribute #12
- This is acceptable as the attribute will have a real value

### 2. Add DHCP ACD Flag to TCP/IP Object Structure

**File**: `components/opener/src/cip/ciptcpipinterface.h`

**Location**: Around line 60, after `select_acd` field

**Change**:
```c
  CipBool select_acd; /**< attribute #10 - Is ACD enabled for static IP? */
  CipBool select_dhcp_acd; /**< attribute #12 - Is ACD enabled for DHCP? */
```

**Rationale**: 
- Mirrors the structure of `select_acd` for consistency
- Uses same data type (`CipBool`) for compatibility

### 3. Update NVS Storage Structure

**File**: `components/opener/src/ports/nvdata/nvtcpip.c`

#### 3.1 Update TcpipNvBlob Structure

**Location**: Around line 47

**Change**:
```c
typedef struct __attribute__((packed)) {
  uint32_t version;
  uint32_t config_control;
  uint32_t ip_address;
  uint32_t network_mask;
  uint32_t gateway;
  uint32_t name_server;
  uint32_t name_server2;
  uint16_t domain_length;
  uint16_t hostname_length;
  uint8_t domain[TCPIP_DOMAIN_MAX_LEN];
  uint8_t hostname[TCPIP_HOSTNAME_MAX_LEN];
  uint8_t select_acd;
  uint8_t select_dhcp_acd;  // ADD THIS LINE
} TcpipNvBlob;
```

**Impact**: 
- Structure size increases by 1 byte
- Requires NVS version bump for compatibility

#### 3.2 Bump NVS Version

**Location**: Around line 28

**Change**:
```c
#define TCPIP_NV_VERSION     3U  // Changed from 2U
```

**Rationale**: 
- Version 2 structures won't have `select_dhcp_acd` field
- Version bump allows detection and migration of old data

#### 3.3 Update NvTcpipLoad() Function

**Location**: Around line 132, in the version 2 blob handling section

**Change**:
```c
  if (blob_v2 != NULL) {
    // Check version for migration
    if (blob_v2->version == 2U) {
      // Migrate from version 2: select_dhcp_acd field doesn't exist
      p_tcp_ip->config_control = blob_v2->config_control;
      p_tcp_ip->interface_configuration.ip_address = blob_v2->ip_address;
      p_tcp_ip->interface_configuration.network_mask = blob_v2->network_mask;
      p_tcp_ip->interface_configuration.gateway = blob_v2->gateway;
      p_tcp_ip->interface_configuration.name_server = blob_v2->name_server;
      p_tcp_ip->interface_configuration.name_server_2 = blob_v2->name_server2;
      p_tcp_ip->select_acd = (blob_v2->select_acd != 0u);
      
      // Set default for new field based on compile-time config
      #if CONFIG_LWIP_DHCP_DOES_ACD_CHECK
      p_tcp_ip->select_dhcp_acd = true;  // Default: enabled if compile-time enabled
      #else
      p_tcp_ip->select_dhcp_acd = false; // Default: disabled if compile-time disabled
      #endif
      
      // Save migrated data back to NVS with new version
      NvTcpipStore(p_tcp_ip);
      ESP_LOGI(kTag, "Migrated TCP/IP config from version 2 to version 3");
    } else if (blob_v2->version == TCPIP_NV_VERSION) {
      // Handle version 3 normally
      p_tcp_ip->config_control = blob_v2->config_control;
      p_tcp_ip->interface_configuration.ip_address = blob_v2->ip_address;
      p_tcp_ip->interface_configuration.network_mask = blob_v2->network_mask;
      p_tcp_ip->interface_configuration.gateway = blob_v2->gateway;
      p_tcp_ip->interface_configuration.name_server = blob_v2->name_server;
      p_tcp_ip->interface_configuration.name_server_2 = blob_v2->name_server2;
      p_tcp_ip->select_acd = (blob_v2->select_acd != 0u);
      p_tcp_ip->select_dhcp_acd = (blob_v2->select_dhcp_acd != 0u);
    } else {
      ESP_LOGW(kTag, "Stored TCP/IP configuration has unexpected version %" PRIu32,
               blob_v2->version);
      return kEipStatusError;
    }
  } else {
    // Version 1 handling (unchanged)
    p_tcp_ip->config_control = blob_v1.config_control;
    p_tcp_ip->interface_configuration.ip_address = blob_v1.ip_address;
    p_tcp_ip->interface_configuration.network_mask = blob_v1.network_mask;
    p_tcp_ip->interface_configuration.gateway = blob_v1.gateway;
    p_tcp_ip->interface_configuration.name_server = blob_v1.name_server;
    p_tcp_ip->interface_configuration.name_server_2 = blob_v1.name_server2;
    p_tcp_ip->select_acd = false;  // Version 1 didn't have ACD
    
    // Set default for new field based on compile-time config
    #if CONFIG_LWIP_DHCP_DOES_ACD_CHECK
    p_tcp_ip->select_dhcp_acd = true;
    #else
    p_tcp_ip->select_dhcp_acd = false;
    #endif
  }
```

**Rationale**:
- Version 2 migration: Sets default based on compile-time config to maintain expected behavior
- Version 3: Normal handling with new field
- Version 1: Also sets default for consistency
- Auto-migration saves updated structure back to NVS

#### 3.4 Update NvTcpipStore() Function

**Location**: Around line 273

**Change**:
```c
  blob.select_acd = p_tcp_ip->select_acd ? 1u : 0u;
  blob.select_dhcp_acd = p_tcp_ip->select_dhcp_acd ? 1u : 0u;  // ADD THIS LINE
```

**Rationale**: Persists the new field to NVS

### 4. Initialize Default Value

**File**: `components/opener/src/cip/ciptcpipinterface.c`

**Location**: Around line 96, in `CreateTcpIpInterface()` function

**Change**:
```c
  .select_acd = false,
  .select_dhcp_acd = 
#if CONFIG_LWIP_DHCP_DOES_ACD_CHECK
    true,   // Default: enabled if compile-time enabled
#else
    false,  // Default: disabled if compile-time disabled
#endif
```

**Rationale**: 
- Default value matches compile-time configuration
- Ensures consistent behavior if NVS data doesn't exist
- Users can override via EtherNet/IP if needed

### 5. Replace Dummy Attribute #12 with Real Implementation

**File**: `components/opener/src/cip/ciptcpipinterface.c`

**Location**: Around line 816-821, replace the dummy attribute registration

**Current Code**:
```c
  InsertAttribute(instance,
                  12,
                  kCipBool,
                  EncodeCipBool,
                  NULL,
                  &dummy_data_field, kGetableAllDummy);
```

**New Code**:
```c
  InsertAttribute(instance,
                  12,
                  kCipBool,
                  EncodeCipBool,
                  DecodeTcpIpSelectDhcpAcd,  // New decoder function
                  &g_tcpip.select_dhcp_acd,
                  kGetableSingleAndAll | kSetable | kNvDataFunc);
```

**Rationale**:
- Replaces dummy attribute with real implementation
- Uses same encoder (`EncodeCipBool`) as Attribute #10 for consistency
- Adds decoder function for Set_Attribute_Single support
- Includes `kNvDataFunc` flag so NVS persistence works automatically

### 6. Implement Decoder Function

**File**: `components/opener/src/cip/ciptcpipinterface.c`

**Location**: Near `DecodeTcpIpSelectAcd()` function (around line 750-760)

**New Function**:
```c
/** @brief Decode TCP/IP Interface Object Attribute #12 - DHCP ACD Selection
 *
 *  This function decodes the value for Attribute #12 (Select DHCP ACD)
 *  when Set_Attribute_Single service is called.
 *
 *  @param octet pointer to the octet string containing the attribute value
 *  @param length length of the octet string (must be 1 for BOOL)
 *  @param data pointer to the data structure to store the decoded value
 *  @return kEipStatusOk on success, kEipStatusError on failure
 */
static EipStatus DecodeTcpIpSelectDhcpAcd(const CipOctet *const octet,
                                           const CipUint length,
                                           void *const data) {
  if (octet == NULL || data == NULL) {
    return kEipStatusError;
  }
  
  if (length != 1) {
    ESP_LOGE("TCPIP", "DecodeTcpIpSelectDhcpAcd: Invalid length %d (expected 1)", length);
    return kEipStatusError;
  }
  
  CipBool *select_dhcp_acd = (CipBool *)data;
  CipBool old_value = *select_dhcp_acd;
  *select_dhcp_acd = (octet[0] != 0);
  
  // Persist to NVS immediately
  EipStatus nv_status = NvTcpipStore(&g_tcpip);
  if (nv_status != kEipStatusOk) {
    ESP_LOGE("TCPIP", "Failed to save DHCP ACD setting to NVS");
    // Restore old value on failure
    *select_dhcp_acd = old_value;
    return kEipStatusError;
  }
  
  ESP_LOGI("TCPIP", "DHCP ACD set to %s via EtherNet/IP (was %s)", 
           *select_dhcp_acd ? "enabled" : "disabled",
           old_value ? "enabled" : "disabled");
  
  // Note: Changes take effect on next DHCP lease renewal
  // This is by design to avoid disrupting active DHCP connections
  if (old_value != *select_dhcp_acd) {
    ESP_LOGI("TCPIP", "DHCP ACD setting changed - will take effect on next DHCP renewal");
  }
  
  return kEipStatusOk;
}
```

**Rationale**:
- Validates input parameters
- Checks length (BOOL must be 1 byte)
- Saves old value for rollback on error
- Persists to NVS immediately
- Provides informative logging
- Documents runtime change behavior

### 7. Modify DHCP Code to Check Runtime Flag

**File**: `components/lwip/lwip/src/core/ipv4/dhcp.c`

**Location**: Around line 415-426, in `dhcp_check()` function

**Current Code**:
```c
static void
dhcp_check(struct netif *netif)
{
  struct dhcp *dhcp = netif_dhcp_data(netif);

  LWIP_DEBUGF(DHCP_DEBUG | LWIP_DBG_TRACE, ("dhcp_check(netif=%p) %c%c\n", (void *)netif, (s16_t)netif->name[0],
              (s16_t)netif->name[1]));
  dhcp_set_state(dhcp, DHCP_STATE_CHECKING);

  /* start ACD module */
  acd_start(netif, &dhcp->acd, dhcp->offered_ip_addr);
}
```

**New Code**:
```c
static void
dhcp_check(struct netif *netif)
{
  struct dhcp *dhcp = netif_dhcp_data(netif);

  LWIP_DEBUGF(DHCP_DEBUG | LWIP_DBG_TRACE, ("dhcp_check(netif=%p) %c%c\n", (void *)netif, (s16_t)netif->name[0],
              (s16_t)netif->name[1]));
  dhcp_set_state(dhcp, DHCP_STATE_CHECKING);

  /* Check runtime flag for DHCP ACD */
  extern CipTcpIpObject g_tcpip;  // Forward declaration
  
  if (g_tcpip.select_dhcp_acd) {
    /* ACD enabled - start ACD module */
    LWIP_DEBUGF(DHCP_DEBUG | LWIP_DBG_TRACE, ("dhcp_check: DHCP ACD enabled, starting ACD check\n"));
    acd_start(netif, &dhcp->acd, dhcp->offered_ip_addr);
  } else {
    /* ACD disabled - accept IP immediately without conflict check */
    LWIP_DEBUGF(DHCP_DEBUG | LWIP_DBG_TRACE, ("dhcp_check: DHCP ACD disabled, binding IP immediately\n"));
    /* 
     * Note: dhcp_bind() can be called directly here because:
     * 1. We're in DHCP_STATE_CHECKING state (set above)
     * 2. dhcp_bind() doesn't check state - it just binds the IP
     * 3. This matches the behavior when ACD_IP_OK callback fires
     */
    dhcp_bind(netif);
  }
}
```

**Rationale**:
- Checks runtime flag before starting ACD
- When disabled, directly calls `dhcp_bind()` to accept IP
- `dhcp_bind()` is safe to call here because:
  - State is already set to `DHCP_STATE_CHECKING`
  - `dhcp_bind()` doesn't validate state - it just binds the IP
  - This matches the normal flow when `ACD_IP_OK` callback fires
- Adds debug logging for troubleshooting

**Alternative Consideration**: 
If there are concerns about calling `dhcp_bind()` directly, we could simulate the ACD callback:

```c
  } else {
    /* ACD disabled - simulate ACD_IP_OK callback */
    dhcp_conflict_callback(netif, ACD_IP_OK);
  }
```

However, this is less direct and the callback function name is misleading when ACD is disabled. Direct call to `dhcp_bind()` is cleaner.

### 8. Add Forward Declaration Header (if needed)

**File**: `components/lwip/lwip/src/core/ipv4/dhcp.c`

**Location**: At top of file, with other includes

**Potential Issue**: `dhcp.c` needs access to `CipTcpIpObject` type definition.

**Solution Options**:

**Option A**: Use forward declaration (current approach in code example)
```c
extern CipTcpIpObject g_tcpip;  // Forward declaration in function
```

**Option B**: Include OpENer header (may cause circular dependencies)
```c
#include "ciptcpipinterface.h"  // Not recommended - may cause issues
```

**Recommendation**: Use **Option A** (forward declaration) as shown in the code example. This avoids header dependency issues.

### 9. Update Initialization Logging

**File**: `main/main.c`

**Location**: Around line 1394, after `NvTcpipLoad()` call

**Change**:
```c
    (void)NvTcpipLoad(&g_tcpip);
    ESP_LOGI(TAG, "After NV load select_acd=%d", g_tcpip.select_acd);
    ESP_LOGI(TAG, "After NV load select_dhcp_acd=%d", g_tcpip.select_dhcp_acd);  // ADD THIS LINE
```

**Rationale**: Helps with debugging and confirms the value was loaded correctly

## Edge Cases and Considerations

### 1. Runtime Change Behavior

**Issue**: Changing `select_dhcp_acd` at runtime won't affect current DHCP lease.

**Behavior**: 
- Changes are saved to NVS immediately
- Take effect on next DHCP lease renewal (T1 renewal, T2 rebind, or lease expiration)
- This is by design to avoid disrupting active connections

**Documentation**: Should be documented in user manual/API docs.

**Optional Enhancement**: Could restart DHCP when flag changes, but this may be disruptive:
```c
// In DecodeTcpIpSelectDhcpAcd(), after saving:
if (old_value != *select_dhcp_acd && tcpip_config_uses_dhcp()) {
    // Restart DHCP to apply change immediately
    // WARNING: This will disrupt network connectivity temporarily
    esp_netif_dhcpc_stop(netif);
    esp_netif_dhcpc_start(netif);
}
```

**Recommendation**: Don't implement automatic restart - let users control when DHCP restarts.

### 2. Compile-Time vs Runtime Configuration Mismatch

**Scenario**: `CONFIG_LWIP_DHCP_DOES_ACD_CHECK=n` but user sets `select_dhcp_acd=true` via EtherNet/IP.

**Current Behavior**: 
- Runtime flag can be set to `true`
- But `dhcp_check()` is only compiled if `LWIP_DHCP_DOES_ACD_CHECK` is enabled
- If disabled at compile-time, `dhcp_check()` function doesn't exist

**Solution**: The code modification handles this:
- If `LWIP_DHCP_DOES_ACD_CHECK` is disabled, `dhcp_check()` isn't compiled
- The runtime flag is ignored (but still stored)
- This is acceptable - compile-time disable means ACD code isn't available

**Alternative**: Could validate in decoder:
```c
#if !LWIP_DHCP_DOES_ACD_CHECK
  if (*select_dhcp_acd) {
    ESP_LOGW("TCPIP", "DHCP ACD enabled via EtherNet/IP but disabled at compile-time");
    // Could reject the change or just log warning
  }
#endif
```

**Recommendation**: Just log a warning if mismatch detected, but allow the setting to be saved.

### 3. NVS Migration Failure

**Scenario**: NVS migration from version 2 to 3 fails.

**Handling**: 
- Migration code should handle errors gracefully
- If migration fails, use defaults based on compile-time config
- Log error for debugging
- Device continues to function with defaults

### 4. GetAttributeAll Behavior Change

**Issue**: Attribute #12 changes from dummy to real data.

**Impact**: 
- `GetAttributeAll` will now return actual value instead of dummy
- This is acceptable and expected behavior
- No code changes needed - encoder handles it automatically

### 5. DHCP Conflict Reporting

**Current State**: 
- Static IP conflicts: Reported via `CipTcpIpSetLastAcdMac()` and `CipTcpIpSetLastAcdRawData()`
- DHCP conflicts: Use same ACD module, so conflict data should be captured

**Verification Needed**: 
- Verify that `acd_arp_reply()` captures conflict data for DHCP ACD
- Check that `dhcp_conflict_callback()` doesn't interfere with conflict reporting
- Ensure Attribute #11 (Last Conflict Detected) works for DHCP conflicts

**Expected Behavior**: 
- DHCP ACD conflicts should be reported the same way as static IP conflicts
- Attribute #11 should contain conflict data regardless of IP assignment method

### 6. Multiple Network Interfaces

**Current Assumption**: Single Ethernet interface.

**Consideration**: 
- If multiple interfaces are added in future, `g_tcpip.select_dhcp_acd` applies globally
- May need per-interface ACD control in future
- Current implementation is sufficient for single interface

## Testing Requirements

### 1. Unit Tests

**NVS Migration**:
- Test loading version 2 data → should migrate to version 3
- Test loading version 3 data → should load normally
- Test loading version 1 data → should set defaults
- Test saving version 3 data → should save correctly

**Attribute Decoder**:
- Test valid BOOL value (0x00, 0x01) → should decode correctly
- Test invalid length → should return error
- Test NULL pointers → should return error
- Test NVS save failure → should restore old value

### 2. Integration Tests

**DHCP ACD Enabled**:
- Configure `select_dhcp_acd=true` via EtherNet/IP
- Verify DHCP performs ACD before accepting IP
- Verify conflict detection works
- Verify conflict reporting in Attribute #11

**DHCP ACD Disabled**:
- Configure `select_dhcp_acd=false` via EtherNet/IP
- Verify DHCP accepts IP immediately without ACD
- Verify no ACD probes are sent
- Verify IP assignment succeeds

**Runtime Change**:
- Start with ACD enabled, get DHCP lease
- Change to ACD disabled via EtherNet/IP
- Verify change is saved to NVS
- Verify current lease continues (no immediate effect)
- Force DHCP renewal (or wait for T1)
- Verify new lease doesn't use ACD

**Compile-Time Mismatch**:
- Build with `CONFIG_LWIP_DHCP_DOES_ACD_CHECK=n`
- Try to set `select_dhcp_acd=true` via EtherNet/IP
- Verify warning is logged (if implemented)
- Verify setting is saved but ignored

### 3. Regression Tests

**Static IP ACD**:
- Verify Attribute #10 (`select_acd`) still works
- Verify static IP ACD behavior unchanged

**Existing DHCP**:
- Verify DHCP still works when ACD is enabled (default)
- Verify conflict detection still works
- Verify conflict reporting still works

**NVS Compatibility**:
- Test upgrade from firmware with version 2 NVS
- Verify migration succeeds
- Verify no data loss

## Migration Strategy

### Phase 1: Preparation
1. Review and approve this document
2. Create feature branch
3. Set up test environment

### Phase 2: Implementation
1. Update data structures (TCP/IP object, NVS blob)
2. Bump NVS version and implement migration
3. Replace dummy Attribute #12
4. Implement decoder function
5. Modify DHCP code
6. Update initialization code

### Phase 3: Testing
1. Unit tests for NVS migration
2. Integration tests for DHCP ACD control
3. Regression tests for existing functionality
4. Edge case testing

### Phase 4: Documentation
1. Update API documentation
2. Update user manual
3. Add code comments

### Phase 5: Deployment
1. Code review
2. Merge to main branch
3. Release notes
4. Monitor for issues

## Documentation Updates Required

### 1. API Documentation

**File**: `docs/API_Endpoints.md` (if REST API is added later)

**Note**: Currently no REST API planned, but if added later, document:
- `GET /api/dhcp/acd` - Get DHCP ACD enabled state
- `POST /api/dhcp/acd` - Set DHCP ACD enabled state

### 2. EtherNet/IP Documentation

**File**: `docs/ASSEMBLY_DATA_LAYOUT.md` or new file

**Add Section**:
```markdown
## TCP/IP Interface Object Attributes

### Attribute #10: Select ACD (Static IP)
- **Type**: BOOL
- **Get/Set**: Both supported
- **Description**: Enable/disable ACD for static IP addresses

### Attribute #12: Select DHCP ACD
- **Type**: BOOL  
- **Get/Set**: Both supported
- **Description**: Enable/disable ACD for DHCP-assigned IP addresses
- **Note**: Changes take effect on next DHCP lease renewal
- **Default**: Matches compile-time configuration (`CONFIG_LWIP_DHCP_DOES_ACD_CHECK`)
```

### 3. Code Comments

**Files**: All modified files

**Add**:
- Function header comments explaining purpose
- Inline comments for complex logic
- TODO comments for future enhancements

### 4. README Updates

**File**: `README.md`

**Add Note**:
```markdown
### DHCP ACD Configuration

DHCP ACD can be enabled/disabled at runtime via EtherNet/IP CIP Attribute #12
(Select DHCP ACD) in the TCP/IP Interface Object (Class 0xF5, Instance 1).

Changes take effect on the next DHCP lease renewal to avoid disrupting active
connections.
```

## Risk Assessment

### Low Risk
- ✅ NVS structure change (well-tested migration path)
- ✅ Adding new attribute (follows existing pattern)
- ✅ Default value initialization (matches compile-time config)

### Medium Risk
- ⚠️ Replacing dummy Attribute #12 (changes `GetAttributeAll` behavior)
- ⚠️ DHCP code modification (core networking functionality)
- ⚠️ NVS migration (data compatibility)

### Mitigation Strategies
1. **Thorough Testing**: Comprehensive test coverage before merge
2. **Backward Compatibility**: NVS migration handles old data gracefully
3. **Default Behavior**: Defaults match compile-time config (no behavior change by default)
4. **Gradual Rollout**: Test on development hardware before production
5. **Rollback Plan**: Keep previous firmware version available

## Conclusion

This implementation adds runtime control of DHCP ACD while maintaining backward compatibility and following existing code patterns. The changes are well-contained and follow the same approach used for static IP ACD control.

**Estimated Implementation Time**: 4-6 hours
**Estimated Testing Time**: 4-6 hours
**Total Estimated Effort**: 1-2 days

**Key Success Criteria**:
1. ✅ DHCP ACD can be enabled/disabled via EtherNet/IP Attribute #12
2. ✅ Changes persist across reboots
3. ✅ NVS migration works for existing devices
4. ✅ No regression in existing functionality
5. ✅ Changes take effect on next DHCP renewal (as designed)

---

**Document Version**: 1.0  
**Last Updated**: 2025-01-XX  
**Author**: Implementation Guide  
**Status**: Draft for Review

