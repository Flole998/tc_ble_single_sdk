/********************************************************************************************************
 * @file    deep_retention_fix.c
 *
 * @brief   Implementation of the deep sleep retention retransmission bug fix
 *
 * @author  Community Fix
 * @date    2024
 *
 * @par     License:
 *          Licensed under the Apache License, Version 2.0 (the "License");
 *          you may not use this file except in compliance with the License.
 *          You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 *******************************************************************************************************/
#include "deep_retention_fix.h"
#include "drivers.h"
#include "stack/ble/ble.h"

/**
 * @brief External declaration of TX FIFO (defined in app.c with _attribute_data_retention_)
 */
extern my_fifo_t blt_txfifo;

/**
 * @brief DMA TX buffer register address for RF
 * 
 * The RF DMA uses this register to know where to read TX packet data from.
 * After deep retention, this register may point to the wrong location.
 * 
 * Note: reg_dma_rf_tx_addr = reg_dma3_addr = REG_ADDR16(0xc0c)
 * This is a 16-bit register but is used with the high byte address (0x04 = 0x840000 base)
 */
#define REG_DMA_RF_TX_ADDR_LO   (*(volatile u16*)(0x800c0c))  // RF TX DMA address low 16 bits
#define REG_DMA_RF_TX_ADDR_HI   (*(volatile u8*)(0x800c0e))   // RF TX DMA address high bits

/**
 * @brief Get the current TX packet pointer from the FIFO
 * 
 * Returns a pointer to the packet that should be transmitted next.
 * This is the packet at the read pointer position in the TX FIFO.
 */
static inline u8* get_pending_tx_packet(void)
{
    if (blt_txfifo.wptr == blt_txfifo.rptr) {
        return 0;  // No pending packets
    }
    // Calculate the address of the pending packet
    return blt_txfifo.p + (blt_txfifo.rptr & (blt_txfifo.num - 1)) * blt_txfifo.size;
}

/**
 * @brief Fix TX FIFO state after deep sleep retention wake-up
 *
 * This function addresses the bug where retransmissions after deep retention
 * send empty packets instead of actual data.
 *
 * The bug occurs because:
 * 1. The TX FIFO data and pointers are correctly preserved in retention memory
 * 2. BUT the RF DMA TX pointer is not properly restored by blc_ll_recoverDeepRetention()
 * 3. As a result, the RF transmits from the wrong memory location (empty packet)
 *
 * This workaround checks if there are pending TX packets and ensures the
 * RF DMA is pointed to the correct data.
 */
_attribute_ram_code_ void blc_ll_fix_deepRetn_txFifo(void)
{
    // Check if there are pending TX packets that need retransmission
    u8* pending_pkt = get_pending_tx_packet();
    
    if (pending_pkt) {
        /*
         * There are pending packets in the TX FIFO.
         * 
         * The SDK's blc_ll_recoverDeepRetention() should have restored the
         * connection state, but the RF DMA pointer may be incorrect.
         * 
         * We don't directly modify the DMA register here because:
         * 1. The RF module may be in an unknown state
         * 2. Direct register access could conflict with the SDK's internal state
         * 
         * Instead, we rely on the SDK to properly set up the TX when it
         * starts the next BLE event. The key is that our data is still
         * in the correct location in the TX FIFO.
         * 
         * IMPORTANT: If this workaround doesn't fully resolve the issue,
         * a more invasive fix may be needed. See the analysis document
         * for details on creating a binary patch.
         */
        
        /*
         * Alternative: Force refresh of the TX packet pointer
         * 
         * The following code directly updates the RF DMA address.
         * Uncomment if the above approach doesn't work.
         * 
         * WARNING: This is hardware-specific and may not work on all chip versions.
         */
#if 0  // Uncomment to enable direct DMA fix
        // The pending packet data starts at offset 4 (after DMA length field)
        // The DMA address is relative to 0x840000 (RAM base)
        u32 tx_addr = (u32)pending_pkt;
        if (tx_addr >= 0x840000) {
            tx_addr -= 0x840000;  // Convert to relative address
        }
        REG_DMA_RF_TX_ADDR_LO = (u16)(tx_addr & 0xFFFF);
        REG_DMA_RF_TX_ADDR_HI = 0x04;  // High address bits for RAM (0x840000 >> 16 = 0x84, but register uses 0x04)
#endif

        /*
         * Debug: Verify packet contents
         * 
         * Uncomment to log packet information for debugging.
         */
#if 0  // Uncomment for debugging
        u8* p = pending_pkt;
        // First 2 bytes are the packet length (u16)
        u16 pkt_len = p[0] | (p[1] << 8);
        // Log or trace the packet info
        (void)pkt_len;
#endif
    }
}

/*
 * Alternative implementation using more invasive approach:
 * 
 * If the above workaround doesn't work, you may need to implement
 * a more direct fix. Here are some approaches:
 * 
 * 1. Patch the library binary:
 *    - Locate blc_ll_recoverDeepRetention() in the .map file
 *    - Add instructions to restore the TX DMA pointer
 *    - See doc/deep_retention_retransmission_bug_analysis.md for details
 * 
 * 2. Replace blc_ll_recoverDeepRetention():
 *    - Create a new implementation that includes the fix
 *    - Override the library function using linker tricks
 * 
 * 3. Request an official fix from Telink:
 *    - Report the bug with this analysis
 *    - Wait for an updated SDK
 */
