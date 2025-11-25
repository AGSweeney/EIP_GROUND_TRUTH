# RFC 5227 Compliance Refactoring Design Document

## Overview

This document proposes moving RFC 5227 compliant Address Conflict Detection (ACD) logic from application code (`main/main.c`) into the lwIP NetIf layer (`components/lwip/lwip/src/core/netif.c`). This refactoring will simplify application code, improve code reusability, and provide automatic RFC 5227 compliance for all static IP assignments.

**Status**: Design Phase (Not Implemented)  
**Date**: 2025-01-XX  
**Author**: Design Document  
**Note**: This is a proposed refactoring. The current implementation uses application-layer ACD coordination with RFC 5227 compliant behavior.

---

## Table of Contents

1. [Current State Analysis](#current-state-analysis)
2. [Proposed Architecture](#proposed-architecture)
3. [Benefits and Trade-offs](#benefits-and-trade-offs)
4. [API Design](#api-design)
5. [Implementation Plan](#implementation-plan)
6. [Migration Strategy](#migration-strategy)
7. [Testing Considerations](#testing-considerations)
8. [Backward Compatibility](#backward-compatibility)

---

## Current State Analysis

### Current Implementation Location

RFC 5227 compliance is currently implemented in `main/main.c` with the following components:

#### 1. State Management (main.c)
- `s_static_ip_acd` - ACD state structure
- `s_acd_registered` - Registration flag
- `s_acd_sem` - Semaphore for synchronization
- `s_acd_registration_sem` - Registration semaphore
- `s_acd_last_state` - Last ACD callback state
- `s_acd_callback_received` - Callback received flag
- `s_acd_probe_pending` - Probe pending flag
- `s_pending_static_ip_cfg` - Pending IP configuration

#### 2. Callback Management (main.c)
- `tcpip_acd_conflict_callback()` - Main ACD callback handler
- `tcpip_acd_start_cb()` - Thread-safe ACD start callback
- `tcpip_perform_acd()` - ACD coordination function
- `tcpip_try_pending_acd()` - Main ACD orchestration function

#### 3. Application-Specific Logic (main.c)
- DNS configuration (`opener_configure_dns()`)
- EtherNet/IP status updates (`CipTcpIpSetLastAcdActivity()`)
- LED control (`user_led_start_flash()`, `user_led_stop_flash()`)
- EtherNet/IP conflict reporting (`CipTcpIpSetLastAcdMac()`, `CipTcpIpSetLastAcdRawData()`)

#### 4. Issues with Current Implementation

1. **Complexity**: ~400+ lines of ACD-related code in application layer
2. **Tight Coupling**: Application code tightly coupled to ACD internals
3. **Reusability**: Cannot be reused by other applications without copying code
4. **Maintenance**: Changes to ACD logic require application code changes
5. **Application Layer Logic**: RFC 5227 compliance logic in application rather than network stack

### Current Flow

```
Application (main.c)
  ↓
tcpip_try_pending_acd()
  ↓
tcpip_perform_acd() [ACD coordination]
  ↓
acd_start() [lwIP ACD module]
  ↓
ACD Callback → tcpip_acd_conflict_callback()
  ↓
Application handles callback:
  - Assign IP (if ACD_IP_OK)
  - Configure DNS
  - Update EtherNet/IP status
  - Control LEDs
```

---

## Proposed Architecture

### High-Level Design

Move RFC 5227 compliance into lwIP NetIf layer, providing a clean API that applications can use without managing ACD internals.

```
Application (main.c)
  ↓
netif_set_addr_with_acd() [New API in netif.c]
  ↓
Internal ACD Management (netif.c)
  - Start ACD probe sequence
  - Defer IP assignment
  - Manage ACD state
  ↓
ACD Callback → netif internal handler
  ↓
Assign IP when ACD_IP_OK received
  ↓
Application Callback (optional)
  - DNS configuration
  - Application-specific actions
```

### Key Components

#### 1. New NetIf API (`netif.c`)

**Function**: `netif_set_addr_with_acd()`
- Starts ACD probe sequence BEFORE assigning IP
- Defers IP assignment until `ACD_IP_OK` callback
- Manages ACD state internally
- Provides optional application callback for post-assignment actions

#### 2. Internal State Management (`netif.c`)

- Store pending IP configuration in `struct netif`
- Manage ACD client registration
- Handle ACD callbacks internally
- Automatically assign IP when ACD confirms safety

#### 3. Application Callback Interface

- Optional callback for application-specific logic
- Called after IP assignment completes
- Allows DNS configuration, status updates, etc.

---

## Benefits and Trade-offs

### Benefits

#### 1. **Simplified Application Code**
- Remove ~500 lines of ACD management code from `main.c`
- No semaphore management in application
- No callback state tracking
- No thread synchronization concerns

#### 2. **Automatic RFC 5227 Compliance**
- All static IP assignments automatically use RFC 5227
- No special application code needed
- Consistent behavior across all applications

#### 3. **Better Encapsulation**
- RFC 5227 logic encapsulated in network stack
- Application doesn't need to know ACD internals
- Clean separation of concerns

#### 4. **Improved Reusability**
- Can be used by any application
- No code duplication
- Standard API for RFC 5227 compliance

#### 5. **Easier Maintenance**
- Changes to ACD logic isolated to NetIf layer
- Application code unaffected by ACD improvements
- Single source of truth for RFC 5227 compliance

### Trade-offs

#### 1. **Application-Specific Logic**
- DNS configuration, LED control, EtherNet/IP status updates still need application callbacks
- **Mitigation**: Provide optional callback mechanism for application-specific actions

#### 2. **ESP-NetIf Abstraction Layer**
- Application uses `esp_netif_set_ip_info()` (ESP-IDF API), not direct `netif_set_addr()`
- **Mitigation**: 
  - Option A: Modify ESP-NetIf to use `netif_set_addr_with_acd()` internally
  - Option B: Create ESP-NetIf wrapper API (`esp_netif_set_ip_info_with_acd()`)
  - Option C: Use direct `netif_set_addr_with_acd()` in application (bypass ESP-NetIf)

#### 3. **Configuration Control**
- Application checks `g_tcpip.select_acd` (EtherNet/IP attribute) to decide whether to use ACD
- **Mitigation**: Pass ACD enable flag to NetIf API, or check internally

#### 4. **Initial Implementation Effort**
- Requires implementing new API in NetIf layer
- Requires testing and validation
- **Mitigation**: Incremental migration, maintain backward compatibility

---

## API Design

### Core API

#### `netif_set_addr_with_acd()`

```c
/**
 * @ingroup netif_ip4
 * Set IP address configuration with RFC 5227 compliant ACD.
 * 
 * This function performs Address Conflict Detection (ACD) before assigning
 * the IP address. The IP address is NOT assigned until ACD confirms it is safe.
 * 
 * @param netif the network interface to configure
 * @param ipaddr the new IP address (will be checked for conflicts)
 * @param netmask the new netmask
 * @param gw the new default gateway
 * @param callback optional callback function called after IP assignment completes
 *                 (NULL if not needed)
 * @param callback_arg argument passed to callback function
 * @return ERR_OK if ACD started successfully
 *         ERR_INPROGRESS if ACD probe sequence started (IP will be assigned later)
 *         ERR_ARG if invalid arguments
 *         ERR_MEM if memory allocation failed
 */
err_t netif_set_addr_with_acd(struct netif *netif,
                               const ip4_addr_t *ipaddr,
                               const ip4_addr_t *netmask,
                               const ip4_addr_t *gw,
                               netif_acd_callback_fn callback,
                               void *callback_arg);
```

#### Callback Type

```c
/**
 * @ingroup netif_ip4
 * Callback function type for netif_set_addr_with_acd() completion.
 * 
 * Called after IP assignment completes (either successfully or on conflict).
 * 
 * @param netif the network interface
 * @param ipaddr the IP address that was assigned (or NULL if conflict detected)
 * @param state ACD callback state (ACD_IP_OK, ACD_DECLINE, ACD_RESTART_CLIENT)
 * @param arg user-provided argument from netif_set_addr_with_acd()
 */
typedef void (*netif_acd_callback_fn)(struct netif *netif,
                                      const ip4_addr_t *ipaddr,
                                      acd_callback_enum_t state,
                                      void *arg);
```

### Internal Structures

#### Pending IP Configuration

```c
/**
 * @ingroup netif_ip4
 * Structure to hold pending IP configuration during ACD probe sequence.
 */
struct netif_pending_ip_config {
    ip4_addr_t ipaddr;
    ip4_addr_t netmask;
    ip4_addr_t gw;
    struct acd acd;                    /* ACD state */
    netif_acd_callback_fn callback;    /* Application callback */
    void *callback_arg;                /* Callback argument */
    bool acd_registered;                /* ACD client registered flag */
};
```

#### NetIf Structure Addition

```c
/* In struct netif (netif.h) */
#if LWIP_ACD && LWIP_ACD_RFC5227_COMPLIANT_STATIC
    struct netif_pending_ip_config *pending_ip_config;
#endif
```

### ESP-NetIf Integration (Optional)

#### Option A: ESP-NetIf Wrapper API

```c
/**
 * @brief Set IP address information with RFC 5227 compliant ACD
 * 
 * @param esp_netif Handle to esp-netif instance
 * @param ip_info IP address information to set
 * @param callback Optional callback called after IP assignment
 * @param callback_arg Callback argument
 * @return
 *      - ESP_OK: ACD started successfully
 *      - ESP_ERR_INVALID_ARG: Invalid arguments
 *      - ESP_ERR_INVALID_STATE: NetIf not initialized
 *      - ESP_FAIL: ACD start failed
 */
esp_err_t esp_netif_set_ip_info_with_acd(esp_netif_t *esp_netif,
                                          const esp_netif_ip_info_t *ip_info,
                                          esp_netif_acd_callback_fn callback,
                                          void *callback_arg);
```

#### Option B: Modify ESP-NetIf Internals

Modify `esp_netif_set_ip_info()` to check for ACD enable flag and use `netif_set_addr_with_acd()` internally when ACD is enabled.

---

## Implementation Plan

### Phase 1: Core NetIf API Implementation

**Goal**: Implement `netif_set_addr_with_acd()` in `netif.c`

**Tasks**:
1. Add `struct netif_pending_ip_config` to `netif_pending_ip.h` (new file)
2. Add `pending_ip_config` field to `struct netif` in `netif.h`
3. Implement `netif_set_addr_with_acd()` function:
   - Validate parameters
   - Allocate `netif_pending_ip_config` structure
   - Store IP configuration
   - Register ACD client
   - Start ACD probe sequence
   - Return `ERR_INPROGRESS` (IP will be assigned later)
4. Implement internal ACD callback handler:
   - Handle `ACD_IP_OK`: Assign IP, call application callback
   - Handle `ACD_DECLINE`/`ACD_RESTART_CLIENT`: Call application callback with conflict state
5. Add cleanup in `netif_remove()`:
   - Stop ACD if pending
   - Free pending IP configuration

**Files Modified**:
- `components/lwip/lwip/src/include/lwip/netif_pending_ip.h` (NEW)
- `components/lwip/lwip/src/include/lwip/netif.h`
- `components/lwip/lwip/src/core/netif.c`

**Estimated Effort**: 2-3 days

### Phase 2: Configuration Option

**Goal**: Add compile-time option to enable/disable RFC 5227 mode

**Tasks**:
1. Add `LWIP_ACD_RFC5227_COMPLIANT_STATIC` option to `opt.h`
2. Default to enabled when `LWIP_ACD` is enabled
3. Wrap all RFC 5227 code with `#if LWIP_ACD_RFC5227_COMPLIANT_STATIC`

**Files Modified**:
- `components/lwip/lwip/src/include/lwip/opt.h`

**Estimated Effort**: 0.5 days

### Phase 3: Application Integration

**Goal**: Update `main.c` to use new API

**Tasks**:
1. Replace `tcpip_try_pending_acd()` with `netif_set_addr_with_acd()` call
2. Remove ACD state management code:
   - Remove `s_static_ip_acd`, `s_acd_registered`, semaphores, etc.
   - Remove `tcpip_perform_acd()`, `tcpip_acd_start_cb()`, etc.
3. Create application callback function:
   - Move DNS configuration to callback
   - Move EtherNet/IP status updates to callback
   - Move LED control to callback
4. Update `tcpip_acd_conflict_callback()`:
   - Simplify to application-specific logic only
   - Remove ACD state management

**Files Modified**:
- `main/main.c`

**Estimated Effort**: 1-2 days

### Phase 4: ESP-NetIf Integration (Optional)

**Goal**: Provide ESP-NetIf wrapper API

**Tasks**:
1. Implement `esp_netif_set_ip_info_with_acd()` in ESP-NetIf
2. Map ESP-NetIf types to lwIP types
3. Call `netif_set_addr_with_acd()` internally
4. Update application to use ESP-NetIf API (if preferred)

**Files Modified**:
- ESP-IDF `components/esp_netif/` (or create wrapper in project)

**Estimated Effort**: 1-2 days

### Phase 5: Testing and Validation

**Goal**: Comprehensive testing of new implementation

**Tasks**:
1. Unit tests for `netif_set_addr_with_acd()`
2. Integration tests:
   - Static IP assignment with ACD enabled
   - Static IP assignment with ACD disabled
   - Conflict detection scenarios
   - Callback execution
3. Regression tests:
   - DHCP functionality
   - Existing static IP assignments
   - Application-specific features (DNS, LEDs, EtherNet/IP)
4. Performance testing:
   - ACD probe timing
   - Memory usage
   - CPU overhead

**Estimated Effort**: 2-3 days

### Total Estimated Effort

**Core Implementation**: 6-10 days  
**Optional ESP-NetIf Integration**: +1-2 days

---

## Migration Strategy

### Backward Compatibility

#### Option 1: Maintain Legacy API

Keep existing `netif_set_addr()` function unchanged:
- If `LWIP_ACD_RFC5227_COMPLIANT_STATIC` is disabled, behavior unchanged
- If enabled, `netif_set_addr()` could optionally use ACD (breaking change) or remain non-ACD

#### Option 2: New API Only

- `netif_set_addr()` remains non-ACD (backward compatible)
- `netif_set_addr_with_acd()` provides RFC 5227 compliance
- Applications choose which API to use

**Recommendation**: Option 2 (New API Only) - maintains backward compatibility while providing RFC 5227 option.

### Migration Steps

1. **Phase 1-2**: Implement NetIf API (no application changes)
2. **Phase 3**: Update application to use new API
3. **Testing**: Validate functionality
4. **Cleanup**: Remove old application ACD code after validation

### Rollback Plan

- Keep old application code commented out initially
- Can revert to old implementation if issues arise
- New API is additive, doesn't break existing code

---

## Testing Considerations

### Unit Tests

1. **API Function Tests**:
   - `netif_set_addr_with_acd()` with valid parameters
   - `netif_set_addr_with_acd()` with invalid parameters
   - Callback execution on success
   - Callback execution on conflict
   - Memory allocation failure handling

2. **State Management Tests**:
   - Pending IP config allocation/free
   - ACD client registration
   - Cleanup on netif removal

### Integration Tests

1. **Happy Path**:
   - Static IP assignment with no conflicts
   - IP assigned after ACD_IP_OK
   - Application callback executed
   - DNS configured correctly

2. **Conflict Scenarios**:
   - Conflict detected during probe
   - IP not assigned on conflict
   - Application callback executed with conflict state
   - Retry behavior (if implemented)

3. **Edge Cases**:
   - NetIf removed during ACD probe
   - Multiple concurrent ACD requests
   - ACD disabled at compile time
   - Invalid IP configuration

### Regression Tests

1. **DHCP Functionality**:
   - DHCP still works correctly
   - DHCP ACD (if enabled) unaffected

2. **Application Features**:
   - EtherNet/IP status updates
   - LED control
   - DNS configuration
   - Conflict reporting

### Performance Tests

1. **Timing**:
   - ACD probe sequence timing matches configured values
   - IP assignment delay acceptable
   - No performance regression

2. **Memory**:
   - Memory usage acceptable
   - No memory leaks
   - Proper cleanup

---

## Backward Compatibility

### API Compatibility

- ✅ `netif_set_addr()` remains unchanged (backward compatible)
- ✅ New API is additive (doesn't break existing code)
- ✅ Can be disabled via compile-time option

### Application Compatibility

- ✅ Existing applications continue to work
- ✅ New applications can opt-in to RFC 5227
- ✅ Gradual migration possible

### Configuration Compatibility

- ✅ `LWIP_ACD` option still controls ACD availability
- ✅ New `LWIP_ACD_RFC5227_COMPLIANT_STATIC` option controls RFC 5227 mode
- ✅ Can be enabled/disabled independently

---

## Open Questions

1. **ESP-NetIf Integration**: Should we modify ESP-NetIf or create wrapper?
   - **Recommendation**: Start with direct `netif_set_addr_with_acd()` usage, add ESP-NetIf wrapper later if needed

2. **ACD Enable Flag**: How should application control ACD enable/disable?
   - **Option A**: Pass flag to API (`netif_set_addr_with_acd()` vs `netif_set_addr()`)
   - **Option B**: Check `g_tcpip.select_acd` internally
   - **Recommendation**: Option A (explicit API choice)

3. **Callback Timing**: When should application callback be called?
   - **Option A**: After IP assignment (ACD_IP_OK only)
   - **Option B**: On all ACD states (including conflicts)
   - **Recommendation**: Option B (more flexible)

4. **Error Handling**: How should ACD failures be handled?
   - **Option A**: Return error, don't assign IP
   - **Option B**: Fall back to immediate assignment
   - **Recommendation**: Option A (fail safe, don't assign IP if ACD fails)

5. **Multiple Pending Configs**: Should we support multiple pending IP configs?
   - **Recommendation**: No (one pending config per netif, cancel previous if new one started)

---

## Success Criteria

1. ✅ Application code reduced by ~500 lines
2. ✅ RFC 5227 compliance automatic for static IP assignments
3. ✅ No regression in existing functionality
4. ✅ Application-specific logic still works (DNS, LEDs, EtherNet/IP)
5. ✅ Performance acceptable (no significant delay)
6. ✅ Memory usage acceptable
7. ✅ Code maintainable and well-documented

---

## References

- **RFC 5227**: IPv4 Address Conflict Detection - https://tools.ietf.org/html/rfc5227
- **Current Implementation**: `main/main.c` (lines ~307-980)
- **LWIP NetIf Documentation**: `components/lwip/lwip/src/core/netif.c`
- **ACD Implementation**: `components/lwip/lwip/src/core/ipv4/acd.c`

---

## Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2025-01-XX | Design Document | Initial design document |

---

**Status**: Ready for Review  
**Next Steps**: Review design, address open questions, proceed with Phase 1 implementation

