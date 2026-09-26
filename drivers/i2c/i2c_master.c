/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>

#include <soc/i2c_periph.h>
#include <soc/lp_i2c_reg.h>
#include <soc/clk_tree_defs.h>
#include <hal/i2c_ll.h>
#include <hal/gpio_ll.h>
#include <hal/rtc_io_ll.h>

#include <ulp_lp_core_utils.h>
#include <ulp_lp_core_interrupts.h>

#include <esp_err.h>

#include "i2c_master.h"

static const char *TAG = "i2c_master";

#define I2C_FIFO_LEN     SOC_I2C_FIFO_LEN
#define I2C_READ_MODE    I2C_MASTER_READ
#define I2C_WRITE_MODE   I2C_MASTER_WRITE
#define I2C_ACK          I2C_MASTER_ACK
#define I2C_NACK         I2C_MASTER_NACK

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

/* ACK check enable control variable. Enabled by default */
static bool s_ack_check_en = true;

/*
 * The I2C controller uses the I2C HW command registers to perform read/write operations.
 * The cmd registers have the following format:
 *
 *      31        30:14     13:11      10         9           8         7:0
 * |----------|----------|---------|---------|----------|------------|---------|
 * | CMD_DONE | Reserved |  OPCODE |ACK Value|ACK Expect|ACK Check En|Byte Num |
 * |----------|----------|---------|---------|----------|------------|---------|
 */
static void i2c_format_cmd(i2c_dev_t *dev, uint32_t cmd_idx, uint8_t op_code, uint8_t ack_val,
                                   uint8_t ack_expected, uint8_t ack_check_en, uint8_t byte_num)
{
    if (cmd_idx >= sizeof(dev->command)) {
        /* We only have limited HW command registers.
         * Although unlikely, make sure that we do not write to an out of bounds index.
         */
        return;
    }

    /* Form new command */
    i2c_ll_hw_cmd_t hw_cmd = {
        .done = 0,                // CMD Done
        .op_code = op_code,       // Opcode
        .ack_val = ack_val,       // ACK bit sent by I2C controller during READ.
        // Ignored during RSTART, STOP, END and WRITE cmds.
        .ack_exp = ack_expected,  // ACK bit expected by I2C controller during WRITE.
        // Ignored during RSTART, STOP, END and READ cmds.
        .ack_en = ack_check_en,   // I2C controller verifies that the ACK bit sent by the
        // slave device matches the ACK expected bit during WRITE.
        // Ignored during RSTART, STOP, END and READ cmds.
        .byte_num = byte_num,     // Byte Num
    };

    /* Write new command to cmd register */
    i2c_ll_master_write_cmd_reg(dev, hw_cmd, cmd_idx);
}

static inline int i2c_wait_for_interrupt(i2c_dev_t *dev, uint32_t intr_mask, int32_t ticks_to_wait)
{
    uint32_t intr_status = 0;
    uint32_t to = 0;
    while (1) {
        i2c_ll_get_intr_mask(dev, &intr_status);
        if (intr_status & intr_mask) {
            if (intr_status & I2C_NACK_INT_ST) {
                /* The ACK/NACK received during a WRITE operation does not match the expected ACK/NACK level
                 * Abort and return an error.
                 */
                i2c_ll_clear_intr_mask(dev, intr_mask);
                return ESP_ERR_INVALID_RESPONSE;
            } else if (intr_status & I2C_TRANS_COMPLETE_INT_ST_M) {
                /* Transaction complete.
                 * Disable and clear interrupt bits and break
                 */
                i2c_ll_disable_intr_mask(dev, intr_mask);
                i2c_ll_clear_intr_mask(dev, intr_mask);
                break;
            } else {
                /* We received an I2C_END_DETECT_INT.
                 * This means we are not yet done with the transaction.
                 * Simply clear the interrupt bit and break.
                 */
                i2c_ll_clear_intr_mask(dev, intr_mask);
                break;
            }
            break;
        }

        if (ticks_to_wait > -1) {
            /* If the ticks_to_wait value is not -1, keep track of ticks and
             * break from the loop once the timeout is reached.
             */
            ulp_lp_core_delay_cycles(1);
            to++;
            if (to >= ticks_to_wait) {
                /* Disable and clear interrupt bits */
                i2c_ll_disable_intr_mask(dev, intr_mask);
                i2c_ll_clear_intr_mask(dev, intr_mask);
                return ESP_ERR_TIMEOUT;
            }
        }
    }

    /* We reach here only if we are in a good state */
    return ESP_OK;
}

static inline void i2c_config_device_addr(i2c_dev_t *dev, uint32_t cmd_idx, uint16_t device_addr,  uint32_t rw_mode, uint8_t *addr_len)
{
    uint8_t data_byte = 0;
    uint8_t data_len = 0;

    /* 7-bit addressing mode. We do not support 10-bit addressing mode yet (IDF-7364) */

    // Write the device address + R/W mode in the first Tx FIFO slot
    data_byte = (uint8_t)(((device_addr & 0xFF) << 1) | (rw_mode << 0));
    i2c_ll_write_txfifo(dev, &data_byte, 1);
    data_len++;

    /* Update the HW command register. Expect an ACK from the device */
    i2c_format_cmd(dev, cmd_idx, I2C_LL_CMD_WRITE, 0, I2C_ACK, s_ack_check_en, data_len);

    /* Return the address length in bytes */
    *addr_len = data_len;
}

int i2c_master_read_from_device(int i2c_port, uint16_t device_addr,
                                              uint8_t *data_rd, size_t size,
                                              int32_t ticks_to_wait)
{

    /* Configure I2C port */
    if (i2c_port >= SOC_I2C_NUM) {
        printf("%s: Invalid i2c_port passed\n", TAG);
        return -1;
    }

    i2c_dev_t *dev = I2C_LL_GET_HW(i2c_port);

    bool is_lp_i2c = (i2c_port < SOC_HP_I2C_NUM) ? false : true;
    if (is_lp_i2c) {
        ulp_lp_core_intr_disable();
    }

    esp_err_t ret = ESP_OK;
    uint32_t cmd_idx = 0;

    if (size == 0) {
        // Quietly return
        return ESP_OK;
    }

    /* If SCL is busy, reset the Master FSM */
    if (i2c_ll_is_bus_busy(dev)) {
        i2c_ll_master_fsm_rst(dev);
    }

    /* Reset the Tx and Rx FIFOs */
    i2c_ll_txfifo_rst(dev);
    i2c_ll_rxfifo_rst(dev);

    /* Execute RSTART command to send the START bit */
    i2c_format_cmd(dev, cmd_idx++, I2C_LL_CMD_RESTART, 0, 0, 0, 0);

    /* Write device addr and update the HW command register */
    uint8_t addr_len = 0;
    i2c_config_device_addr(dev, cmd_idx++, device_addr, I2C_READ_MODE, &addr_len);

    /* Enable trans complete interrupt and end detect interrupt for read/write operation */
    uint32_t intr_mask = (1 << I2C_TRANS_COMPLETE_INT_ST_S) | (1 << I2C_END_DETECT_INT_ST_S);
    if (s_ack_check_en) {
        /* Enable I2C_NACK_INT to check for ACK errors on address byte */
        intr_mask |= (1 << I2C_NACK_INT_ST_S);
    }
    i2c_ll_enable_intr_mask(dev, intr_mask);

    /* Read data */
    uint32_t fifo_size = 0;
    uint32_t data_idx = 0;
    int32_t remaining_bytes = size;

    /* The data is received in sequential slots of the Rx FIFO.
     * We must account for FIFO wraparound in case the length of data being received is greater than I2C_FIFO_LEN.
     */
    while (remaining_bytes > 0) {
        /* Select the amount of data that fits in the Rx FIFO */
        fifo_size = MIN(remaining_bytes, I2C_FIFO_LEN);

        /* Update the number of bytes remaining to be read */
        remaining_bytes -= fifo_size;

        /* Update HW command register to read bytes */
        if (fifo_size == 1) {
            /* Read 1 byte and send NACK */
            i2c_format_cmd(dev, cmd_idx++, I2C_LL_CMD_READ, I2C_NACK, 0, 0, 1);

            /* STOP */
            i2c_format_cmd(dev, cmd_idx++, I2C_LL_CMD_STOP, 0, 0, 0, 0);
        } else if ((fifo_size > 1) && (remaining_bytes == 0)) {
            /* This means it is the last transaction.
             * Read fifo_size - 1 bytes and send ACKs
             */
            i2c_format_cmd(dev, cmd_idx++, I2C_LL_CMD_READ, I2C_ACK, 0, 0, fifo_size - 1);

            /* Read last byte and send NACK */
            i2c_format_cmd(dev, cmd_idx++, I2C_LL_CMD_READ, I2C_NACK, 0, 0, 1);

            /* STOP */
            i2c_format_cmd(dev, cmd_idx++, I2C_LL_CMD_STOP, 0, 0, 0, 0);
        } else {
            /* This means we have to read data more than what can fit in the Rx FIFO.
             * Read fifo_size bytes and send ACKs
             */
            i2c_format_cmd(dev, cmd_idx++, I2C_LL_CMD_READ, I2C_ACK, 0, 0, fifo_size);
            i2c_format_cmd(dev, cmd_idx++, I2C_LL_CMD_END, 0, 0, 0, 0);
            cmd_idx = 0;
        }

        /* Initiate I2C transfer */
        i2c_ll_update(dev);
        i2c_ll_master_trans_start(dev);

        // Wait for 10ms
        ulp_lp_core_delay_us(10000);

        /* Wait for the transfer to complete */
        ret = i2c_wait_for_interrupt(dev, intr_mask, ticks_to_wait);
        if (ret != ESP_OK) {
            /* Transaction error. Abort. */
            goto exit;
        }

        /* Read Rx FIFO */
        i2c_ll_read_rxfifo(dev, &data_rd[data_idx], fifo_size);

        /* Update data_idx */
        data_idx += fifo_size;
    }

exit:
    if (is_lp_i2c) {
        ulp_lp_core_intr_enable();
    }
    return ret;
}

int i2c_master_write_to_device(int i2c_port, uint16_t device_addr,
                                             const uint8_t *data_wr, size_t size,
                                             int32_t ticks_to_wait)
{ 
    /* Configure I2C port */
    if (i2c_port >= SOC_I2C_NUM) {
        printf("%s: Invalid i2c_port passed\n", TAG);
        return -1;
    }

    i2c_dev_t *dev = I2C_LL_GET_HW(i2c_port);

    bool is_lp_i2c = (i2c_port < SOC_HP_I2C_NUM) ? false : true;
    if (is_lp_i2c) {
        ulp_lp_core_intr_disable();
    }

    esp_err_t ret = ESP_OK;
    uint32_t cmd_idx = 0;

    if (size == 0) {
        // Quietly return
        return ESP_OK;
    }
    if (is_lp_i2c) {
        ulp_lp_core_intr_disable();
    }
    /* If SCL is busy, reset the Master FSM */
    if (i2c_ll_is_bus_busy(dev)) {
        i2c_ll_master_fsm_rst(dev);
    }

    /* Reset the Tx and Rx FIFOs */
    i2c_ll_txfifo_rst(dev);
    i2c_ll_rxfifo_rst(dev);

    /* Execute RSTART command to send the START bit */
    i2c_format_cmd(dev, cmd_idx++, I2C_LL_CMD_RESTART, 0, 0, 0, 0);

    /* Write device addr and update the HW command register */
    uint8_t addr_len = 0;
    i2c_config_device_addr(dev, cmd_idx++, device_addr, I2C_WRITE_MODE, &addr_len);

    /* Enable trans complete interrupt and end detect interrupt for read/write operation */
    uint32_t intr_mask = (1 << I2C_TRANS_COMPLETE_INT_ST_S) | (1 << I2C_END_DETECT_INT_ST_S);
    if (s_ack_check_en) {
        /* Enable I2C_NACK_INT to check for ACK errors */
        intr_mask |= (1 << I2C_NACK_INT_ST_S);
    }
    i2c_ll_enable_intr_mask(dev, intr_mask);

    /* Write data */
    uint32_t fifo_available = I2C_FIFO_LEN - addr_len; // Initially, 1 or 2 fifo slots are taken by the device address
    uint32_t fifo_size = 0;
    uint32_t data_idx = 0;
    int32_t remaining_bytes = size;

    /* The data to be sent must occupy sequential slots of the Tx FIFO.
     * We must account for FIFO wraparound in case the length of data being sent is greater than I2C_FIFO_LEN.
     */
    while (remaining_bytes > 0) {
        /* Select the amount of data that fits in the Tx FIFO */
        fifo_size = MIN(remaining_bytes, fifo_available);

        /* Update the number of bytes remaining to be sent */
        remaining_bytes -= fifo_size;

        /* Write data to the Tx FIFO and update the HW command register. Expect ACKs from the device */
        i2c_ll_write_txfifo(dev, &data_wr[data_idx], fifo_size);
        i2c_format_cmd(dev, cmd_idx++, I2C_LL_CMD_WRITE, 0, I2C_ACK, s_ack_check_en, fifo_size);

        if (remaining_bytes == 0) {
            /* This means it is the last transaction. Insert a Stop command. */
            i2c_format_cmd(dev, cmd_idx++, I2C_LL_CMD_STOP, 0, 0, 0, 0);
        } else {
            /* This means we have to send more than what can fit in the Tx FIFO. Insert an End command. */
            i2c_format_cmd(dev, cmd_idx++, I2C_LL_CMD_END, 0, 0, 0, 0);
            cmd_idx = 0;
        }

        /* Initiate I2C transfer */
        i2c_ll_update(dev);
        i2c_ll_master_trans_start(dev);

        // Wait for 10ms
        ulp_lp_core_delay_us(10000);

        /* Wait for the transfer to complete */
        ret = i2c_wait_for_interrupt(dev, intr_mask, ticks_to_wait);
        if (ret != ESP_OK) {
            /* Transaction error. Abort. */
            goto exit;
        }
        /* Update data_idx */
        data_idx += fifo_size;

        /* We now have the full fifo available for writing */
        fifo_available = I2C_FIFO_LEN;
    }

exit:
    if (is_lp_i2c) {
        ulp_lp_core_intr_enable();
    }
    return ret;
}

int i2c_master_write_read_device(int i2c_port, uint16_t device_addr,
                                               const uint8_t *data_wr, size_t write_size,
                                               uint8_t *data_rd, size_t read_size,
                                               int32_t ticks_to_wait)
{
    if ((write_size == 0) || (read_size == 0) || (write_size > 14) || (read_size > 14)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (i2c_port >= SOC_I2C_NUM) {
        printf("%s: Invalid i2c_port passed\n", TAG);
        return -1;
    }

    i2c_dev_t *dev = I2C_LL_GET_HW(i2c_port);
    bool is_lp_i2c = (i2c_port < SOC_HP_I2C_NUM) ? false : true;
    if (is_lp_i2c) {
        ulp_lp_core_intr_disable();
    }

    if (i2c_ll_is_bus_busy(dev)) {
        i2c_ll_master_fsm_rst(dev);
    }
    i2c_ll_txfifo_rst(dev);
    i2c_ll_rxfifo_rst(dev);

    uint32_t cmd_idx = 0;
    uint8_t addr_len = 0;

    // 1. START
    i2c_format_cmd(dev, cmd_idx++, I2C_LL_CMD_RESTART, 0, 0, 0, 0);

    // 2. Device Address (WRITE)
    i2c_config_device_addr(dev, cmd_idx++, device_addr, I2C_WRITE_MODE, &addr_len);

    // 3. Write data (e.g. register address)
    i2c_ll_write_txfifo(dev, data_wr, write_size);
    i2c_format_cmd(dev, cmd_idx++, I2C_LL_CMD_WRITE, 0, I2C_ACK, s_ack_check_en, write_size);

    // 4. REPEATED START (Sr) - No STOP in between!
    i2c_format_cmd(dev, cmd_idx++, I2C_LL_CMD_RESTART, 0, 0, 0, 0);

    // 5. Device Address (READ)
    i2c_config_device_addr(dev, cmd_idx++, device_addr, I2C_READ_MODE, &addr_len);

    // 6. Read bytes
    if (read_size == 1) {
        i2c_format_cmd(dev, cmd_idx++, I2C_LL_CMD_READ, I2C_NACK, 0, 0, 1);
    } else {
        i2c_format_cmd(dev, cmd_idx++, I2C_LL_CMD_READ, I2C_ACK, 0, 0, read_size - 1);
        i2c_format_cmd(dev, cmd_idx++, I2C_LL_CMD_READ, I2C_NACK, 0, 0, 1);
    }

    // 7. STOP
    i2c_format_cmd(dev, cmd_idx++, I2C_LL_CMD_STOP, 0, 0, 0, 0);

    // Enable interrupts
    uint32_t intr_mask = (1 << I2C_TRANS_COMPLETE_INT_ST_S) | (1 << I2C_END_DETECT_INT_ST_S);
    if (s_ack_check_en) {
        intr_mask |= (1 << I2C_NACK_INT_ST_S);
    }
    i2c_ll_enable_intr_mask(dev, intr_mask);

    // Execute transaction
    i2c_ll_update(dev);
    i2c_ll_master_trans_start(dev);
    ulp_lp_core_delay_us(10000);

    esp_err_t ret = i2c_wait_for_interrupt(dev, intr_mask, ticks_to_wait);
    if (ret == ESP_OK) {
        i2c_ll_read_rxfifo(dev, data_rd, read_size);
    }

    if (is_lp_i2c) {
        ulp_lp_core_intr_enable();
    }
    return ret;
}


static void s_hp_i2c_config_io(gpio_num_t io_num, bool pullup_en)
{
    gpio_ll_set_level(&GPIO, io_num, 0 /* output_invert */);
    gpio_ll_input_enable(&GPIO, io_num);
    gpio_ll_od_enable(&GPIO, io_num);
    if (pullup_en) {
        gpio_ll_pullup_en(&GPIO, io_num);
    } else {
        gpio_ll_pullup_dis(&GPIO, io_num);
    }

    /* gpio matrix config: 1. func: gpio, 2. I/O/D */
    gpio_ll_func_sel(&GPIO, io_num, PIN_FUNC_GPIO);
}

static void s_lp_i2c_config_io(int port, gpio_num_t io_num, bool pullup_en)
{
#if SOC_LP_IO_CLOCK_IS_INDEPENDENT
    _rtcio_ll_enable_io_clock(true);
#endif
    rtcio_ll_function_select(io_num, RTCIO_LL_FUNC_RTC);
    rtcio_ll_output_mode_set(io_num, RTCIO_LL_OUTPUT_OD);
    rtcio_ll_output_enable(io_num);
    rtcio_ll_input_enable(io_num);
    rtcio_ll_pulldown_disable(io_num);
    if (pullup_en) {
        rtcio_ll_pullup_enable(io_num);
    } else {
        rtcio_ll_pullup_disable(io_num);
    }
#if !SOC_LP_GPIO_MATRIX_SUPPORTED
    rtcio_ll_iomux_func_sel(io_num, i2c_periph_signal[port].iomux_func);
#endif
}

static void s_hp_i2c_config_clk(i2c_dev_t *dev)
{
    uint32_t source_freq = 40000000;
    soc_periph_i2c_clk_src_t source_clk = I2C_CLK_SRC_DEFAULT;
    i2c_ll_set_source_clk(dev, source_clk);
    i2c_hal_clk_config_t clk_cal = {0};
    i2c_ll_master_cal_bus_clk(source_freq, 100000, &clk_cal);
    i2c_ll_master_set_bus_timing(dev, &clk_cal);
}

static void s_lp_i2c_config_clk(i2c_dev_t *dev)
{
    uint32_t source_freq = 17500000;
    soc_periph_lp_i2c_clk_src_t source_clk = LP_I2C_SCLK_DEFAULT;

    // src_clk : (0) for LP_FAST_CLK (RTC Fast), (1) for XTAL_D2_CLK
    switch (source_clk) {
        case LP_I2C_SCLK_LP_FAST:
            LP_CLKRST.lpperi.lp_i2c_clk_sel = 0;
            break;
        case LP_I2C_SCLK_XTAL_D2:
            LP_CLKRST.lpperi.lp_i2c_clk_sel = 1;
            break;
        default:
            break;
    }

    i2c_hal_clk_config_t clk_cal = {0};
    i2c_ll_master_cal_bus_clk(source_freq, 400000, &clk_cal);
    i2c_ll_master_set_bus_timing(dev, &clk_cal);
}

int i2c_master_init(int i2c_port, gpio_num_t scl_io, gpio_num_t sda_io)
{
    /* Configure I2C port */
    if (i2c_port >= SOC_I2C_NUM) {
        printf("%s: Invalid i2c_port passed\n", TAG);
        return -1;
    }

    i2c_dev_t *dev = I2C_LL_GET_HW(i2c_port);

    bool is_lp_i2c = (i2c_port < SOC_HP_I2C_NUM) ? false : true;

    /* Configure I2C GPIOs */
    if (!is_lp_i2c) {
        s_hp_i2c_config_io(sda_io, true);
        s_hp_i2c_config_io(scl_io, true);

        /* gpio connect to I2C peripheral */
        REG_WRITE(GPIO_FUNC0_OUT_SEL_CFG_REG + (scl_io * 4), I2CEXT0_SCL_OUT_IDX);
        REG_WRITE(GPIO_FUNC0_OUT_SEL_CFG_REG + (sda_io * 4), I2CEXT0_SDA_OUT_IDX);
        REG_WRITE(GPIO_FUNC0_IN_SEL_CFG_REG + (I2CEXT0_SCL_IN_IDX * 4), ((1 << 7) /* !BYPASS */ | (0 << 6) /* !INV */ | scl_io));
        REG_WRITE(GPIO_FUNC0_IN_SEL_CFG_REG + (I2CEXT0_SDA_IN_IDX * 4), ((1 << 7) /* !BYPASS */ | (0 << 6) /* !INV */ | sda_io));

    } else {
    #if !SOC_LP_GPIO_MATRIX_SUPPORTED
        if (sda_io != LP_I2C_SDA_IOMUX_PAD) {
            printf("%s: %s\n", TAG, LP_I2C_SDA_PIN_ERR_LOG);
            return -1;
        }
        if (scl_io != LP_I2C_SCL_IOMUX_PAD) {
            printf("%s: %s\n", TAG, LP_I2C_SCL_PIN_ERR_LOG);
            return -1;
        }
    #endif
        s_lp_i2c_config_io(i2c_port, sda_io, true);
        s_lp_i2c_config_io(i2c_port, scl_io, true);

    #if SOC_LP_GPIO_MATRIX_SUPPORTED
        rtcio_hal_matrix_out(sda_io, i2c_periph_signal[i2c_port].sda_out_sig, 0, 0);
        rtcio_hal_matrix_in(sda_io, i2c_periph_signal[i2c_port].sda_in_sig, 0);
        rtcio_hal_matrix_out(scl_io, i2c_periph_signal[i2c_port].scl_out_sig, 0, 0);
        rtcio_hal_matrix_in(scl_io, i2c_periph_signal[i2c_port].scl_in_sig, 0);
    #endif
    }

    /* Enable I2C bus clock and Reset I2C register */
    if (!is_lp_i2c) {
        i2c_ll_enable_bus_clock(i2c_port, true);
        i2c_ll_reset_register(i2c_port);
    } else {
        _lp_i2c_ll_enable_bus_clock(i2c_port - SOC_HP_I2C_NUM, true);
        LPPERI.reset_en.lp_ext_i2c_reset_en = 1;
        LPPERI.reset_en.lp_ext_i2c_reset_en = 0;
    }

    /* Initialize I2C HAL */
    i2c_ll_enable_controller_clock(dev, true);

    /* Clear any pending interrupts */
    i2c_ll_clear_intr_mask(dev, UINT32_MAX);

    /* Initialize I2C Master mode */
    i2c_ll_set_mode(dev, I2C_BUS_MODE_MASTER);
    i2c_ll_enable_pins_open_drain(dev, true);
    i2c_ll_enable_arbitration(dev, false);
    i2c_ll_master_rx_full_ack_level(dev, false);
    // MSB
    i2c_ll_set_data_mode(dev, I2C_DATA_MODE_MSB_FIRST, I2C_DATA_MODE_MSB_FIRST);

    // Reset FIFO
    i2c_ll_txfifo_rst(dev);
    i2c_ll_rxfifo_rst(dev);

    /* Enable internal open-drain mode for I2C IO lines */
    dev->ctr.sda_force_out = 0;
    dev->ctr.scl_force_out = 0;

    /* Configure I2C clock and timing parameters */
    if (!is_lp_i2c) {
        s_hp_i2c_config_clk(dev);
    } else {
        s_lp_i2c_config_clk(dev);
    }

    /* Enable SDA and SCL filtering. This configuration matches the HP I2C filter config */
    i2c_ll_master_set_filter(dev, 7);

    /* Configure the I2C master to send a NACK when the Rx FIFO count is full */
    i2c_ll_master_rx_full_ack_level(dev, 1);

    /* Synchronize the config register values to the I2C peripheral clock */
    i2c_ll_update(dev);

    return 0;
}
