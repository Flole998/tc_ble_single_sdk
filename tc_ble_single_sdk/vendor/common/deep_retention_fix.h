/********************************************************************************************************
 * @file    deep_retention_fix.h
 *
 * @brief   Workaround for deep sleep retention retransmission bug
 *
 * @author  Community Fix
 * @date    2024
 *
 * @par     Description:
 *          This file provides a workaround for the bug in the Telink BLE SDK where
 *          retransmissions after deep sleep retention wake-up send empty packets
 *          instead of the actual data.
 *
 *          The bug is caused by the RF DMA packet pointer not being properly restored
 *          after deep retention wake-up. The TX FIFO data is preserved in retention
 *          memory, but the internal pointer used by the RF module points to the wrong
 *          location (likely blt_tx_empty_packet instead of the actual TX FIFO data).
 *
 * @par     Usage:
 *          Call `blc_ll_fix_deepRetn_txFifo()` immediately after `blc_ll_recoverDeepRetention()`
 *          in your `user_init_deepRetn()` function:
 *
 *          void user_init_deepRetn(void) {
 *              blc_app_loadCustomizedParameters_deepRetn();
 *              blc_ll_initBasicMCU();
 *              rf_set_power_level_index(MY_RF_POWER_INDEX);
 *              blc_ll_recoverDeepRetention();
 *              blc_ll_fix_deepRetn_txFifo();  // <-- Add this line
 *              // ... rest of initialization
 *          }
 *
 *******************************************************************************************************/
#ifndef DEEP_RETENTION_FIX_H_
#define DEEP_RETENTION_FIX_H_

#include "tl_common.h"
#include "common/utility.h"

/**
 * @brief      Fix TX FIFO state after deep sleep retention wake-up
 * @param      none
 * @return     none
 *
 * @note       This function should be called immediately after blc_ll_recoverDeepRetention()
 *             to ensure pending TX packets are properly set up for retransmission.
 */
_attribute_ram_code_ void blc_ll_fix_deepRetn_txFifo(void);

#endif /* DEEP_RETENTION_FIX_H_ */
