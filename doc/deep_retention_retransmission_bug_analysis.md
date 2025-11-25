# Deep Sleep Retention Retransmission Bug Analysis

## Problem Description

After waking up from deep sleep retention mode, BLE packet retransmissions fail. Instead of retransmitting packets with the original data, only empty packets are transmitted.

## Background

In BLE Link Layer, reliable data transfer is ensured through:
1. Sequence Number (SN) and Next Expected Sequence Number (NESN)
2. When a packet is not acknowledged (NACK), it should be retransmitted with the same data
3. The TX FIFO stores packets pending transmission/acknowledgment

## Root Cause Analysis

### Retention Data Structure Analysis

Through disassembly and analysis of the precompiled libraries (`liblt_825x.a`, `liblt_827x.a`, `liblt_tc321x.a`), the following key data structures were identified:

#### Data in Retention Memory (`.retention_data` section):
| Symbol | Size | Object File | Description |
|--------|------|-------------|-------------|
| `bltc` | 96 bytes | ll_slave.o | Connection state structure |
| `blttcon` | 89 bytes | ll_conn.o | Connection control structure |
| `blt_txfifo` | 12 bytes | Common | TX FIFO control structure |
| `blt_txfifo_b` | varies | Common | TX FIFO data buffer |
| `bltPm` | 48 bytes | ll_pm.o | Power management state |
| `blt_pconn` | 4 bytes | ll_conn.o | Connection pointer |
| `blt_bb` | 4 bytes | ll.o | Baseband pointer |

#### Data NOT in Retention Memory (regular `.data` section):
| Symbol | Size | Object File | Description |
|--------|------|-------------|-------------|
| `blt_tx_empty_packet` | 6 bytes | ll_slave.o | Empty TX packet template |

### The Bug

The issue lies in how the RF DMA packet pointer is managed during deep retention recovery. 

During normal BLE operation:
1. When a packet needs to be transmitted, the RF module is configured with a pointer to the packet data in the TX FIFO
2. If the packet is not acknowledged, the same pointer is used for retransmission
3. The `blc_ll_recoverDeepRetention()` function is responsible for restoring the BLE state after deep retention wake-up

**The bug**: After deep retention wake-up, the internal packet pointer used by the RF DMA appears to be reset or pointing to the wrong location (possibly `blt_tx_empty_packet` instead of the actual TX FIFO data), causing retransmissions to send empty packets.

### Evidence

1. The `blt_tx_empty_packet` (6 bytes) is NOT in retention memory, meaning it gets reinitialized from flash after wake-up
2. The TX FIFO data (`blt_txfifo_b`) IS in retention memory and contains valid packet data
3. The FIFO pointers (`wptr`, `rptr`) in `blt_txfifo` are in retention memory and should be correctly preserved
4. The RF DMA must be reconfigured after wake-up, and this is where the pointer to pending TX data may be lost

## Key Functions Involved

1. `blc_ll_recoverDeepRetention()` (ll_pm.o, ~192 bytes) - Main recovery function
2. `blt_brx_start()` (ll_slave.o, ~286 bytes) - Starts BLE RX event and sets up TX
3. `blt_push_fifo_hw()` (ll_slave.o, ~220 bytes) - Pushes packet to hardware FIFO
4. `irq_blc_slave_tx()` (ll_slave.o, ~80 bytes) - TX interrupt handler

## Proposed Solutions

### Solution 1: Software Workaround (Application Level)

Add a workaround in `user_init_deepRetn()` to force retransmission of pending TX packets after deep retention wake-up:

```c
_attribute_ram_code_ void user_init_deepRetn(void)
{
#if (PM_DEEPSLEEP_RETENTION_ENABLE)
    blc_app_loadCustomizedParameters_deepRetn();
    blc_ll_initBasicMCU();
    rf_set_power_level_index(MY_RF_POWER_INDEX);
    blc_ll_recoverDeepRetention();

    // WORKAROUND: Force TX FIFO reinitialization for pending packets
    // This ensures the RF DMA pointer is correctly set to the pending TX data
    extern my_fifo_t blt_txfifo;
    if (blt_txfifo.wptr != blt_txfifo.rptr) {
        // There are pending packets - force re-push to hardware
        // The SDK should handle this, but due to the bug it doesn't
        // Call internal function to reinitialize TX pointer
        // (Note: This requires exposing internal SDK functions)
    }

    // ... rest of initialization
#endif
}
```

### Solution 2: Binary Patch for `blc_ll_recoverDeepRetention()`

The `blc_ll_recoverDeepRetention()` function needs to be patched to properly restore the TX packet pointer.

#### Identifying the Patch Location

The function is located in the `.ram_code` section of `ll_pm.o` at offset 0x500 (function entry at 0x501 due to Thumb mode).

**Function Details:**
- Size: 192 bytes (0xC0)
- Entry: Offset 0x501 in .ram_code
- Symbol: `blc_ll_recoverDeepRetention`

#### Patch Strategy

Without access to the full source code, the patch should:
1. After recovering the connection state, check if there are pending TX packets (`wptr != rptr`)
2. If yes, reconfigure the RF DMA with the correct pointer to `blt_txfifo_b + (rptr & (num-1)) * size`
3. Ensure the packet header (SN/NESN) is correctly set for retransmission

### Solution 3: Request SDK Fix from Telink

The most reliable solution is to report this bug to Telink Semiconductor and request an official SDK update. Include this analysis in the bug report.

## Verification Steps

To verify this is the issue:
1. Enable PM with deep sleep retention in a connection state
2. Queue a TX packet (e.g., notification)
3. Trigger deep retention sleep
4. Wake up before the packet is acknowledged
5. Observe that empty packets are transmitted instead of the actual data

## Binary Patch Details

For advanced users who need an immediate fix, here's how to create a binary patch:

### Locating the Function in Firmware

1. Build your firmware with symbols
2. Find the address of `blc_ll_recoverDeepRetention` in the .map file
3. The function is typically at `0x840000 + ramcode_offset + function_offset`

### Patch Bytes

**Note:** The exact patch bytes depend on the specific firmware build. The following is a template:

At the end of `blc_ll_recoverDeepRetention()`, before the return:
- Add code to check `blt_txfifo.wptr != blt_txfifo.rptr`
- If true, recalculate and set the TX DMA pointer

This requires careful analysis of the specific firmware binary and may vary between SDK versions.

## Affected Versions

Based on the SDK structure, this bug likely affects:
- tc_ble_single_sdk V3.4.2.x and earlier
- All chip variants: B85 (TLSR825x), B87 (TLSR827x), TC321X

## References

- Telink SDK: tc_ble_single_sdk
- BLE Core Specification: Link Layer Protocol
- Telink Developer Forum: [Check for related discussions]

## Appendix: Symbol Tables

### ll_pm.o Key Symbols
```
blc_ll_initPowerManagement_module (FUNC, 48 bytes)
blt_brx_sleep (FUNC, 1278 bytes)
blc_ll_recoverDeepRetention (FUNC, 192 bytes)
bltPm (OBJECT, 48 bytes, retention_data)
blt_next_event_tick (OBJECT, 4 bytes, retention_data)
```

### ll_slave.o Key Symbols
```
blt_push_fifo_hw (FUNC, 220 bytes)
irq_blc_slave_tx (FUNC, 80 bytes)
blt_brx_start (FUNC, 286 bytes)
blt_brx_post (FUNC, 286 bytes)
bltc (OBJECT, 96 bytes, retention_data)
blt_tx_empty_packet (OBJECT, 6 bytes, .data - NOT retention!)
```

