#include "ICM_20948_C.h"
#include "ICM_20948_REGISTERS.h"
#include "AK09916_REGISTERS.h"

const ICM_20948_Serif_t NullSerif = {
	NULL, // write
	NULL, // read
	NULL, // user
};

// Private function prototypes

// Function definitions
ICM_20948_Status_e ICM_20948_link_serif(ICM_20948_Device_t *pdev, const ICM_20948_Serif_t *s)
{
	if (s == NULL)
	{
		return ICM_20948_Stat_ParamErr;
	}
	if (pdev == NULL)
	{
		return ICM_20948_Stat_ParamErr;
	}
	pdev->_serif = s;
	return ICM_20948_Stat_Ok;
}

ICM_20948_Status_e ICM_20948_execute_w(ICM_20948_Device_t *pdev, uint8_t regaddr, uint8_t *pdata, uint32_t len)
{
	if (pdev->_serif->write == NULL)
	{
		return ICM_20948_Stat_NotImpl;
	}
	return (*pdev->_serif->write)(regaddr, pdata, len, pdev->_serif->user);
}

ICM_20948_Status_e ICM_20948_execute_r(ICM_20948_Device_t *pdev, uint8_t regaddr, uint8_t *pdata, uint32_t len)
{
	if (pdev->_serif->read == NULL)
	{
		return ICM_20948_Stat_NotImpl;
	}
	return (*pdev->_serif->read)(regaddr, pdata, len, pdev->_serif->user);
}

//Transact directly with an I2C device, one byte at a time
//Used to configure a device before it is setup into a normal 0-3 peripheral slot
ICM_20948_Status_e ICM_20948_i2c_controller_periph4_txn(ICM_20948_Device_t *pdev, uint8_t addr, uint8_t reg, uint8_t *data, uint8_t len, bool Rw, bool send_reg_addr)
{
	// Thanks MikeFair! // https://github.com/kriswiner/MPU9250/issues/86
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;

	addr = (((Rw) ? 0x80 : 0x00) | addr);

	retval = ICM_20948_set_bank(pdev, 3);
	retval = ICM_20948_execute_w(pdev, AGB3_REG_I2C_PERIPH4_ADDR, (uint8_t *)&addr, 1);
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	retval = ICM_20948_set_bank(pdev, 3);
	retval = ICM_20948_execute_w(pdev, AGB3_REG_I2C_PERIPH4_REG, (uint8_t *)&reg, 1);
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	ICM_20948_I2C_PERIPH4_CTRL_t ctrl;
	ctrl.EN = 1;
	ctrl.INT_EN = false;
	ctrl.DLY = 0;
	ctrl.REG_DIS = !send_reg_addr;

	ICM_20948_I2C_MST_STATUS_t i2c_mst_status;
	bool txn_failed = false;
	uint16_t nByte = 0;

	while (nByte < len)
	{
		if (!Rw)
		{
			retval = ICM_20948_set_bank(pdev, 3);
			retval = ICM_20948_execute_w(pdev, AGB3_REG_I2C_PERIPH4_DO, (uint8_t *)&(data[nByte]), 1);
			if (retval != ICM_20948_Stat_Ok)
			{
				return retval;
			}
		}

		// Kick off txn
		retval = ICM_20948_set_bank(pdev, 3);
		retval = ICM_20948_execute_w(pdev, AGB3_REG_I2C_PERIPH4_CTRL, (uint8_t *)&ctrl, sizeof(ICM_20948_I2C_PERIPH4_CTRL_t));
		if (retval != ICM_20948_Stat_Ok)
		{
			return retval;
		}

		// long tsTimeout = millis() + 3000;  // Emergency timeout for txn (hard coded to 3 secs)
		uint32_t max_cycles = 1000;
		uint32_t count = 0;
		bool peripheral4Done = false;
		while (!peripheral4Done)
		{
			retval = ICM_20948_set_bank(pdev, 0);
			retval = ICM_20948_execute_r(pdev, AGB0_REG_I2C_MST_STATUS, (uint8_t *)&i2c_mst_status, 1);

			peripheral4Done = (i2c_mst_status.I2C_PERIPH4_DONE /*| (millis() > tsTimeout) */); //Avoid forever-loops
			peripheral4Done |= (count >= max_cycles);
			count++;
		}
		txn_failed = (i2c_mst_status.I2C_PERIPH4_NACK /*| (millis() > tsTimeout) */);
		txn_failed |= (count >= max_cycles);
		if (txn_failed)
			break;

		if (Rw)
		{
			retval = ICM_20948_set_bank(pdev, 3);
			retval = ICM_20948_execute_r(pdev, AGB3_REG_I2C_PERIPH4_DI, &data[nByte], 1);
		}

		nByte++;
	}

	if (txn_failed)
	{
		//We often fail here if mag is stuck
		return ICM_20948_Stat_Err;
	}

	return retval;
}

ICM_20948_Status_e ICM_20948_i2c_master_single_w(ICM_20948_Device_t *pdev, uint8_t addr, uint8_t reg, uint8_t *data)
{
	return ICM_20948_i2c_controller_periph4_txn(pdev, addr, reg, data, 1, false, true);
}

ICM_20948_Status_e ICM_20948_i2c_master_single_r(ICM_20948_Device_t *pdev, uint8_t addr, uint8_t reg, uint8_t *data)
{
	return ICM_20948_i2c_controller_periph4_txn(pdev, addr, reg, data, 1, true, true);
}

ICM_20948_Status_e ICM_20948_set_bank(ICM_20948_Device_t *pdev, uint8_t bank)
{
	if (bank > 3)
	{
		return ICM_20948_Stat_ParamErr;
	}						   // Only 4 possible banks

	if (bank == pdev->_last_bank) // Do we need to change bank?
			return ICM_20948_Stat_Ok; // Bail if we don't need to change bank to avoid unnecessary bus traffic

	pdev->_last_bank = bank; // Store the requested bank (before we bit-shift)
	bank = (bank << 4) & 0x30; // bits 5:4 of REG_BANK_SEL
	return ICM_20948_execute_w(pdev, REG_BANK_SEL, &bank, 1);
}

ICM_20948_Status_e ICM_20948_sw_reset(ICM_20948_Device_t *pdev)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;
	ICM_20948_PWR_MGMT_1_t reg;

	ICM_20948_set_bank(pdev, 0); // Must be in the right bank

	retval = ICM_20948_execute_r(pdev, AGB0_REG_PWR_MGMT_1, (uint8_t *)&reg, sizeof(ICM_20948_PWR_MGMT_1_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	reg.DEVICE_RESET = 1;

	retval = ICM_20948_execute_w(pdev, AGB0_REG_PWR_MGMT_1, (uint8_t *)&reg, sizeof(ICM_20948_PWR_MGMT_1_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}
	return retval;
}

ICM_20948_Status_e ICM_20948_sleep(ICM_20948_Device_t *pdev, bool on)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;
	ICM_20948_PWR_MGMT_1_t reg;

	ICM_20948_set_bank(pdev, 0); // Must be in the right bank

	retval = ICM_20948_execute_r(pdev, AGB0_REG_PWR_MGMT_1, (uint8_t *)&reg, sizeof(ICM_20948_PWR_MGMT_1_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	if (on)
	{
		reg.SLEEP = 1;
	}
	else
	{
		reg.SLEEP = 0;
	}

	retval = ICM_20948_execute_w(pdev, AGB0_REG_PWR_MGMT_1, (uint8_t *)&reg, sizeof(ICM_20948_PWR_MGMT_1_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}
	return retval;
}

ICM_20948_Status_e ICM_20948_low_power(ICM_20948_Device_t *pdev, bool on)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;
	ICM_20948_PWR_MGMT_1_t reg;

	ICM_20948_set_bank(pdev, 0); // Must be in the right bank

	retval = ICM_20948_execute_r(pdev, AGB0_REG_PWR_MGMT_1, (uint8_t *)&reg, sizeof(ICM_20948_PWR_MGMT_1_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	if (on)
	{
		reg.LP_EN = 1;
	}
	else
	{
		reg.LP_EN = 0;
	}

	retval = ICM_20948_execute_w(pdev, AGB0_REG_PWR_MGMT_1, (uint8_t *)&reg, sizeof(ICM_20948_PWR_MGMT_1_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}
	return retval;
}

ICM_20948_Status_e ICM_20948_set_clock_source(ICM_20948_Device_t *pdev, ICM_20948_PWR_MGMT_1_CLKSEL_e source)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;
	ICM_20948_PWR_MGMT_1_t reg;

	ICM_20948_set_bank(pdev, 0); // Must be in the right bank

	retval = ICM_20948_execute_r(pdev, AGB0_REG_PWR_MGMT_1, (uint8_t *)&reg, sizeof(ICM_20948_PWR_MGMT_1_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	reg.CLKSEL = source;

	retval = ICM_20948_execute_w(pdev, AGB0_REG_PWR_MGMT_1, (uint8_t *)&reg, sizeof(ICM_20948_PWR_MGMT_1_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}
	return retval;
}

ICM_20948_Status_e ICM_20948_get_who_am_i(ICM_20948_Device_t *pdev, uint8_t *whoami)
{
	if (whoami == NULL)
	{
		return ICM_20948_Stat_ParamErr;
	}
	ICM_20948_set_bank(pdev, 0); // Must be in the right bank
	return ICM_20948_execute_r(pdev, AGB0_REG_WHO_AM_I, whoami, 1);
}

ICM_20948_Status_e ICM_20948_check_id(ICM_20948_Device_t *pdev)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;
	uint8_t whoami = 0x00;
	retval = ICM_20948_get_who_am_i(pdev, &whoami);
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}
	if (whoami != ICM_20948_WHOAMI)
	{
		return ICM_20948_Stat_WrongID;
	}
	return retval;
}

ICM_20948_Status_e ICM_20948_data_ready(ICM_20948_Device_t *pdev)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;
	ICM_20948_INT_STATUS_1_t reg;
	retval = ICM_20948_set_bank(pdev, 0); // Must be in the right bank
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}
	retval = ICM_20948_execute_r(pdev, AGB0_REG_INT_STATUS_1, (uint8_t *)&reg, sizeof(ICM_20948_INT_STATUS_1_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}
	if (!reg.RAW_DATA_0_RDY_INT)
	{
		retval = ICM_20948_Stat_NoData;
	}
	return retval;
}

// Interrupt Configuration
ICM_20948_Status_e ICM_20948_int_pin_cfg(ICM_20948_Device_t *pdev, ICM_20948_INT_PIN_CFG_t *write, ICM_20948_INT_PIN_CFG_t *read)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;
	retval = ICM_20948_set_bank(pdev, 0); // Must be in the right bank
	if (write != NULL)
	{ // write first, if available
		retval = ICM_20948_execute_w(pdev, AGB0_REG_INT_PIN_CONFIG, (uint8_t *)write, sizeof(ICM_20948_INT_PIN_CFG_t));
		if (retval != ICM_20948_Stat_Ok)
		{
			return retval;
		}
	}
	if (read != NULL)
	{ // then read, to allow for verification
		retval = ICM_20948_execute_r(pdev, AGB0_REG_INT_PIN_CONFIG, (uint8_t *)read, sizeof(ICM_20948_INT_PIN_CFG_t));
		if (retval != ICM_20948_Stat_Ok)
		{
			return retval;
		}
	}
	return retval;
}

ICM_20948_Status_e ICM_20948_int_enable(ICM_20948_Device_t *pdev, ICM_20948_INT_enable_t *write, ICM_20948_INT_enable_t *read)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;

	ICM_20948_INT_ENABLE_t en_0;
	ICM_20948_INT_ENABLE_1_t en_1;
	ICM_20948_INT_ENABLE_2_t en_2;
	ICM_20948_INT_ENABLE_3_t en_3;

	retval = ICM_20948_set_bank(pdev, 0); // Must be in the right bank

	if (write != NULL)
	{ // If the write pointer is not NULL then write to the registers BEFORE reading
		en_0.I2C_MST_INT_EN = write->I2C_MST_INT_EN;
		en_0.DMP_INT1_EN = write->DMP_INT1_EN;
		en_0.PLL_READY_EN = write->PLL_RDY_EN;
		en_0.WOM_INT_EN = write->WOM_INT_EN;
		en_0.REG_WOF_EN = write->REG_WOF_EN;
		en_1.RAW_DATA_0_RDY_EN = write->RAW_DATA_0_RDY_EN;
		en_2.individual.FIFO_OVERFLOW_EN_4 = write->FIFO_OVERFLOW_EN_4;
		en_2.individual.FIFO_OVERFLOW_EN_3 = write->FIFO_OVERFLOW_EN_3;
		en_2.individual.FIFO_OVERFLOW_EN_2 = write->FIFO_OVERFLOW_EN_2;
		en_2.individual.FIFO_OVERFLOW_EN_1 = write->FIFO_OVERFLOW_EN_1;
		en_2.individual.FIFO_OVERFLOW_EN_0 = write->FIFO_OVERFLOW_EN_0;
		en_3.individual.FIFO_WM_EN_4 = write->FIFO_WM_EN_4;
		en_3.individual.FIFO_WM_EN_3 = write->FIFO_WM_EN_3;
		en_3.individual.FIFO_WM_EN_2 = write->FIFO_WM_EN_2;
		en_3.individual.FIFO_WM_EN_1 = write->FIFO_WM_EN_1;
		en_3.individual.FIFO_WM_EN_0 = write->FIFO_WM_EN_0;

		retval = ICM_20948_execute_w(pdev, AGB0_REG_INT_ENABLE, (uint8_t *)&en_0, sizeof(ICM_20948_INT_ENABLE_t));
		if (retval != ICM_20948_Stat_Ok)
		{
			return retval;
		}
		retval = ICM_20948_execute_w(pdev, AGB0_REG_INT_ENABLE_1, (uint8_t *)&en_1, sizeof(ICM_20948_INT_ENABLE_1_t));
		if (retval != ICM_20948_Stat_Ok)
		{
			return retval;
		}
		retval = ICM_20948_execute_w(pdev, AGB0_REG_INT_ENABLE_2, (uint8_t *)&en_2, sizeof(ICM_20948_INT_ENABLE_2_t));
		if (retval != ICM_20948_Stat_Ok)
		{
			return retval;
		}
		retval = ICM_20948_execute_w(pdev, AGB0_REG_INT_ENABLE_3, (uint8_t *)&en_3, sizeof(ICM_20948_INT_ENABLE_3_t));
		if (retval != ICM_20948_Stat_Ok)
		{
			return retval;
		}
	}

	if (read != NULL)
	{ // If read pointer is not NULL then read the registers (if write is not NULL then this should read back the results of write into read)
		retval = ICM_20948_execute_r(pdev, AGB0_REG_INT_ENABLE, (uint8_t *)&en_0, sizeof(ICM_20948_INT_ENABLE_t));
		if (retval != ICM_20948_Stat_Ok)
		{
			return retval;
		}
		retval = ICM_20948_execute_r(pdev, AGB0_REG_INT_ENABLE_1, (uint8_t *)&en_1, sizeof(ICM_20948_INT_ENABLE_1_t));
		if (retval != ICM_20948_Stat_Ok)
		{
			return retval;
		}
		retval = ICM_20948_execute_r(pdev, AGB0_REG_INT_ENABLE_2, (uint8_t *)&en_2, sizeof(ICM_20948_INT_ENABLE_2_t));
		if (retval != ICM_20948_Stat_Ok)
		{
			return retval;
		}
		retval = ICM_20948_execute_r(pdev, AGB0_REG_INT_ENABLE_3, (uint8_t *)&en_3, sizeof(ICM_20948_INT_ENABLE_3_t));
		if (retval != ICM_20948_Stat_Ok)
		{
			return retval;
		}

		read->I2C_MST_INT_EN = en_0.I2C_MST_INT_EN;
		read->DMP_INT1_EN = en_0.DMP_INT1_EN;
		read->PLL_RDY_EN = en_0.PLL_READY_EN;
		read->WOM_INT_EN = en_0.WOM_INT_EN;
		read->REG_WOF_EN = en_0.REG_WOF_EN;
		read->RAW_DATA_0_RDY_EN = en_1.RAW_DATA_0_RDY_EN;
		read->FIFO_OVERFLOW_EN_4 = en_2.individual.FIFO_OVERFLOW_EN_4;
		read->FIFO_OVERFLOW_EN_3 = en_2.individual.FIFO_OVERFLOW_EN_3;
		read->FIFO_OVERFLOW_EN_2 = en_2.individual.FIFO_OVERFLOW_EN_2;
		read->FIFO_OVERFLOW_EN_1 = en_2.individual.FIFO_OVERFLOW_EN_1;
		read->FIFO_OVERFLOW_EN_0 = en_2.individual.FIFO_OVERFLOW_EN_0;
		read->FIFO_WM_EN_4 = en_3.individual.FIFO_WM_EN_4;
		read->FIFO_WM_EN_3 = en_3.individual.FIFO_WM_EN_3;
		read->FIFO_WM_EN_2 = en_3.individual.FIFO_WM_EN_2;
		read->FIFO_WM_EN_1 = en_3.individual.FIFO_WM_EN_1;
		read->FIFO_WM_EN_0 = en_3.individual.FIFO_WM_EN_0;
	}

	return retval;
}

ICM_20948_Status_e ICM_20948_wom_threshold(ICM_20948_Device_t *pdev, ICM_20948_ACCEL_WOM_THR_t *write, ICM_20948_ACCEL_WOM_THR_t *read)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;

	ICM_20948_ACCEL_WOM_THR_t thr;

	retval = ICM_20948_set_bank(pdev, 2); // Must be in the right bank

	if (write != NULL)
	{ // If the write pointer is not NULL then write to the registers BEFORE reading
		thr.WOM_THRESHOLD = write->WOM_THRESHOLD;

		retval = ICM_20948_execute_w(pdev, AGB2_REG_ACCEL_WOM_THR, (uint8_t *)&thr, sizeof(ICM_20948_ACCEL_WOM_THR_t));
		if (retval != ICM_20948_Stat_Ok)
		{
			return retval;
		}
	}

	if (read != NULL)
	{ // If read pointer is not NULL then read the registers (if write is not NULL then this should read back the results of write into read)
		retval = ICM_20948_execute_r(pdev, AGB2_REG_ACCEL_WOM_THR, (uint8_t *)&thr, sizeof(ICM_20948_ACCEL_WOM_THR_t));
		if (retval != ICM_20948_Stat_Ok)
		{
			return retval;
		}

		read->WOM_THRESHOLD = thr.WOM_THRESHOLD;
		}

	return retval;
}

ICM_20948_Status_e ICM_20948_set_sample_mode(ICM_20948_Device_t *pdev, ICM_20948_InternalSensorID_bm sensors, ICM_20948_LP_CONFIG_CYCLE_e mode)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;
	ICM_20948_LP_CONFIG_t reg;

	if (!(sensors & (ICM_20948_Internal_Acc | ICM_20948_Internal_Gyr | ICM_20948_Internal_Mst)))
	{
		return ICM_20948_Stat_SensorNotSupported;
	}

	retval = ICM_20948_set_bank(pdev, 0); // Must be in the right bank
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}
	retval = ICM_20948_execute_r(pdev, AGB0_REG_LP_CONFIG, (uint8_t *)&reg, sizeof(ICM_20948_LP_CONFIG_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	if (sensors & ICM_20948_Internal_Acc)
	{
		reg.ACCEL_CYCLE = mode;
	} // Set all desired sensors to this setting
	if (sensors & ICM_20948_Internal_Gyr)
	{
		reg.GYRO_CYCLE = mode;
	}
	if (sensors & ICM_20948_Internal_Mst)
	{
		reg.I2C_MST_CYCLE = mode;
	}

	retval = ICM_20948_execute_w(pdev, AGB0_REG_LP_CONFIG, (uint8_t *)&reg, sizeof(ICM_20948_LP_CONFIG_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}
	return retval;
}

ICM_20948_Status_e ICM_20948_set_full_scale(ICM_20948_Device_t *pdev, ICM_20948_InternalSensorID_bm sensors, ICM_20948_fss_t fss)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;

	if (!(sensors & (ICM_20948_Internal_Acc | ICM_20948_Internal_Gyr)))
	{
		return ICM_20948_Stat_SensorNotSupported;
	}

	if (sensors & ICM_20948_Internal_Acc)
	{
		ICM_20948_ACCEL_CONFIG_t reg;
		retval |= ICM_20948_set_bank(pdev, 2); // Must be in the right bank
		retval |= ICM_20948_execute_r(pdev, AGB2_REG_ACCEL_CONFIG, (uint8_t *)&reg, sizeof(ICM_20948_ACCEL_CONFIG_t));
		reg.ACCEL_FS_SEL = fss.a;
		retval |= ICM_20948_execute_w(pdev, AGB2_REG_ACCEL_CONFIG, (uint8_t *)&reg, sizeof(ICM_20948_ACCEL_CONFIG_t));
	}
	if (sensors & ICM_20948_Internal_Gyr)
	{
		ICM_20948_GYRO_CONFIG_1_t reg;
		retval |= ICM_20948_set_bank(pdev, 2); // Must be in the right bank
		retval |= ICM_20948_execute_r(pdev, AGB2_REG_GYRO_CONFIG_1, (uint8_t *)&reg, sizeof(ICM_20948_GYRO_CONFIG_1_t));
		reg.GYRO_FS_SEL = fss.g;
		retval |= ICM_20948_execute_w(pdev, AGB2_REG_GYRO_CONFIG_1, (uint8_t *)&reg, sizeof(ICM_20948_GYRO_CONFIG_1_t));
	}
	return retval;
}

ICM_20948_Status_e ICM_20948_set_dlpf_cfg(ICM_20948_Device_t *pdev, ICM_20948_InternalSensorID_bm sensors, ICM_20948_dlpcfg_t cfg)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;

	if (!(sensors & (ICM_20948_Internal_Acc | ICM_20948_Internal_Gyr)))
	{
		return ICM_20948_Stat_SensorNotSupported;
	}

	if (sensors & ICM_20948_Internal_Acc)
	{
		ICM_20948_ACCEL_CONFIG_t reg;
		retval |= ICM_20948_set_bank(pdev, 2); // Must be in the right bank
		retval |= ICM_20948_execute_r(pdev, AGB2_REG_ACCEL_CONFIG, (uint8_t *)&reg, sizeof(ICM_20948_ACCEL_CONFIG_t));
		reg.ACCEL_DLPFCFG = cfg.a;
		retval |= ICM_20948_execute_w(pdev, AGB2_REG_ACCEL_CONFIG, (uint8_t *)&reg, sizeof(ICM_20948_ACCEL_CONFIG_t));
	}
	if (sensors & ICM_20948_Internal_Gyr)
	{
		ICM_20948_GYRO_CONFIG_1_t reg;
		retval |= ICM_20948_set_bank(pdev, 2); // Must be in the right bank
		retval |= ICM_20948_execute_r(pdev, AGB2_REG_GYRO_CONFIG_1, (uint8_t *)&reg, sizeof(ICM_20948_GYRO_CONFIG_1_t));
		reg.GYRO_DLPFCFG = cfg.g;
		retval |= ICM_20948_execute_w(pdev, AGB2_REG_GYRO_CONFIG_1, (uint8_t *)&reg, sizeof(ICM_20948_GYRO_CONFIG_1_t));
	}
	return retval;
}

ICM_20948_Status_e ICM_20948_enable_dlpf(ICM_20948_Device_t *pdev, ICM_20948_InternalSensorID_bm sensors, bool enable)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;

	if (!(sensors & (ICM_20948_Internal_Acc | ICM_20948_Internal_Gyr)))
	{
		return ICM_20948_Stat_SensorNotSupported;
	}

	if (sensors & ICM_20948_Internal_Acc)
	{
		ICM_20948_ACCEL_CONFIG_t reg;
		retval |= ICM_20948_set_bank(pdev, 2); // Must be in the right bank
		retval |= ICM_20948_execute_r(pdev, AGB2_REG_ACCEL_CONFIG, (uint8_t *)&reg, sizeof(ICM_20948_ACCEL_CONFIG_t));
		if (enable)
		{
			reg.ACCEL_FCHOICE = 1;
		}
		else
		{
			reg.ACCEL_FCHOICE = 0;
		}
		retval |= ICM_20948_execute_w(pdev, AGB2_REG_ACCEL_CONFIG, (uint8_t *)&reg, sizeof(ICM_20948_ACCEL_CONFIG_t));
	}
	if (sensors & ICM_20948_Internal_Gyr)
	{
		ICM_20948_GYRO_CONFIG_1_t reg;
		retval |= ICM_20948_set_bank(pdev, 2); // Must be in the right bank
		retval |= ICM_20948_execute_r(pdev, AGB2_REG_GYRO_CONFIG_1, (uint8_t *)&reg, sizeof(ICM_20948_GYRO_CONFIG_1_t));
		if (enable)
		{
			reg.GYRO_FCHOICE = 1;
		}
		else
		{
			reg.GYRO_FCHOICE = 0;
		}
		retval |= ICM_20948_execute_w(pdev, AGB2_REG_GYRO_CONFIG_1, (uint8_t *)&reg, sizeof(ICM_20948_GYRO_CONFIG_1_t));
	}
	return retval;
}

ICM_20948_Status_e ICM_20948_set_sample_rate(ICM_20948_Device_t *pdev, ICM_20948_InternalSensorID_bm sensors, ICM_20948_smplrt_t smplrt)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;

	if (!(sensors & (ICM_20948_Internal_Acc | ICM_20948_Internal_Gyr)))
	{
		return ICM_20948_Stat_SensorNotSupported;
	}

	if (sensors & ICM_20948_Internal_Acc)
	{
		retval |= ICM_20948_set_bank(pdev, 2); // Must be in the right bank
		uint8_t div1 = (smplrt.a << 8);
		uint8_t div2 = (smplrt.a & 0xFF);
		retval |= ICM_20948_execute_w(pdev, AGB2_REG_ACCEL_SMPLRT_DIV_1, &div1, 1);
		retval |= ICM_20948_execute_w(pdev, AGB2_REG_ACCEL_SMPLRT_DIV_2, &div2, 1);
	}
	if (sensors & ICM_20948_Internal_Gyr)
	{
		retval |= ICM_20948_set_bank(pdev, 2); // Must be in the right bank
		uint8_t div = (smplrt.g);
		retval |= ICM_20948_execute_w(pdev, AGB2_REG_GYRO_SMPLRT_DIV, &div, 1);
	}
	return retval;
}

// Interface Things
ICM_20948_Status_e ICM_20948_i2c_master_passthrough(ICM_20948_Device_t *pdev, bool passthrough)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;

	ICM_20948_INT_PIN_CFG_t reg;
	retval = ICM_20948_set_bank(pdev, 0);
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}
	retval = ICM_20948_execute_r(pdev, AGB0_REG_INT_PIN_CONFIG, (uint8_t *)&reg, sizeof(ICM_20948_INT_PIN_CFG_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}
	reg.BYPASS_EN = passthrough;
	retval = ICM_20948_execute_w(pdev, AGB0_REG_INT_PIN_CONFIG, (uint8_t *)&reg, sizeof(ICM_20948_INT_PIN_CFG_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	return retval;
}

ICM_20948_Status_e ICM_20948_i2c_master_enable(ICM_20948_Device_t *pdev, bool enable)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;

	// Disable BYPASS_EN
	retval = ICM_20948_i2c_master_passthrough(pdev, false);
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	ICM_20948_I2C_MST_CTRL_t ctrl;
	retval = ICM_20948_set_bank(pdev, 3);
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}
	retval = ICM_20948_execute_r(pdev, AGB3_REG_I2C_MST_CTRL, (uint8_t *)&ctrl, sizeof(ICM_20948_I2C_MST_CTRL_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}
	ctrl.I2C_MST_CLK = 0x07; // corresponds to 345.6 kHz, good for up to 400 kHz
	ctrl.I2C_MST_P_NSR = 1;
	retval = ICM_20948_execute_w(pdev, AGB3_REG_I2C_MST_CTRL, (uint8_t *)&ctrl, sizeof(ICM_20948_I2C_MST_CTRL_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	ICM_20948_USER_CTRL_t reg;
	retval = ICM_20948_set_bank(pdev, 0);
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}
	retval = ICM_20948_execute_r(pdev, AGB0_REG_USER_CTRL, (uint8_t *)&reg, sizeof(ICM_20948_USER_CTRL_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}
	if (enable)
	{
		reg.I2C_MST_EN = 1;
	}
	else
	{
		reg.I2C_MST_EN = 0;
	}
	retval = ICM_20948_execute_w(pdev, AGB0_REG_USER_CTRL, (uint8_t *)&reg, sizeof(ICM_20948_USER_CTRL_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	return retval;
}

ICM_20948_Status_e ICM_20948_i2c_master_reset(ICM_20948_Device_t *pdev)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;

	ICM_20948_USER_CTRL_t ctrl;
	retval = ICM_20948_set_bank(pdev, 0);
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	retval = ICM_20948_execute_r(pdev, AGB0_REG_USER_CTRL, (uint8_t *)&ctrl, sizeof(ICM_20948_USER_CTRL_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	ctrl.I2C_MST_RST = 1; //Reset!

	retval = ICM_20948_execute_w(pdev, AGB0_REG_USER_CTRL, (uint8_t *)&ctrl, sizeof(ICM_20948_USER_CTRL_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}
	return retval;
}

ICM_20948_Status_e ICM_20948_i2c_controller_configure_peripheral(ICM_20948_Device_t *pdev, uint8_t peripheral, uint8_t addr, uint8_t reg, uint8_t len, bool Rw, bool enable, bool data_only, bool grp, bool swap)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;

	uint8_t periph_addr_reg;
	uint8_t periph_reg_reg;
	uint8_t periph_ctrl_reg;

	switch (peripheral)
	{
	case 0:
		periph_addr_reg = AGB3_REG_I2C_PERIPH0_ADDR;
		periph_reg_reg = AGB3_REG_I2C_PERIPH0_REG;
		periph_ctrl_reg = AGB3_REG_I2C_PERIPH0_CTRL;
		break;
	case 1:
		periph_addr_reg = AGB3_REG_I2C_PERIPH1_ADDR;
		periph_reg_reg = AGB3_REG_I2C_PERIPH1_REG;
		periph_ctrl_reg = AGB3_REG_I2C_PERIPH1_CTRL;
		break;
	case 2:
		periph_addr_reg = AGB3_REG_I2C_PERIPH2_ADDR;
		periph_reg_reg = AGB3_REG_I2C_PERIPH2_REG;
		periph_ctrl_reg = AGB3_REG_I2C_PERIPH2_CTRL;
		break;
	case 3:
		periph_addr_reg = AGB3_REG_I2C_PERIPH3_ADDR;
		periph_reg_reg = AGB3_REG_I2C_PERIPH3_REG;
		periph_ctrl_reg = AGB3_REG_I2C_PERIPH3_CTRL;
		break;
	default:
		return ICM_20948_Stat_ParamErr;
	}

	retval = ICM_20948_set_bank(pdev, 3);
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	// Set the peripheral address and the Rw flag
	ICM_20948_I2C_PERIPHX_ADDR_t address;
	address.ID = addr;
	if (Rw)
	{
		address.RNW = 1;
	}
	retval = ICM_20948_execute_w(pdev, periph_addr_reg, (uint8_t *)&address, sizeof(ICM_20948_I2C_PERIPHX_ADDR_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	// Set the peripheral sub-address (reg)
	ICM_20948_I2C_PERIPHX_REG_t subaddress;
	subaddress.REG = reg;
	retval = ICM_20948_execute_w(pdev, periph_reg_reg, (uint8_t *)&subaddress, sizeof(ICM_20948_I2C_PERIPHX_REG_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	// Set up the control info
	ICM_20948_I2C_PERIPHX_CTRL_t ctrl;
	ctrl.LENG = len;
	ctrl.EN = enable;
	ctrl.REG_DIS = data_only;
	ctrl.GRP = grp;
	ctrl.BYTE_SW = swap;
	retval = ICM_20948_execute_w(pdev, periph_ctrl_reg, (uint8_t *)&ctrl, sizeof(ICM_20948_I2C_PERIPHX_CTRL_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	return retval;
}

// Higher Level
ICM_20948_Status_e ICM_20948_get_agmt(ICM_20948_Device_t *pdev, ICM_20948_AGMT_t *pagmt)
{
	if (pagmt == NULL)
	{
		return ICM_20948_Stat_ParamErr;
	}

	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;
	const uint8_t numbytes = 14 + 9; //Read Accel, gyro, temp, and 9 bytes of mag
	uint8_t buff[numbytes];

	// Get readings
	retval |= ICM_20948_set_bank(pdev, 0);
	retval |= ICM_20948_execute_r(pdev, (uint8_t)AGB0_REG_ACCEL_XOUT_H, buff, numbytes);

	pagmt->acc.axes.x = ((buff[0] << 8) | (buff[1] & 0xFF));
	pagmt->acc.axes.y = ((buff[2] << 8) | (buff[3] & 0xFF));
	pagmt->acc.axes.z = ((buff[4] << 8) | (buff[5] & 0xFF));

	pagmt->gyr.axes.x = ((buff[6] << 8) | (buff[7] & 0xFF));
	pagmt->gyr.axes.y = ((buff[8] << 8) | (buff[9] & 0xFF));
	pagmt->gyr.axes.z = ((buff[10] << 8) | (buff[11] & 0xFF));

	pagmt->tmp.val = ((buff[12] << 8) | (buff[13] & 0xFF));

	pagmt->magStat1 = buff[14];
	pagmt->mag.axes.x = ((buff[16] << 8) | (buff[15] & 0xFF)); //Mag data is read little endian
	pagmt->mag.axes.y = ((buff[18] << 8) | (buff[17] & 0xFF));
	pagmt->mag.axes.z = ((buff[20] << 8) | (buff[19] & 0xFF));
	pagmt->magStat2 = buff[22];

	// Get settings to be able to compute scaled values
	retval |= ICM_20948_set_bank(pdev, 2);
	ICM_20948_ACCEL_CONFIG_t acfg;
	retval |= ICM_20948_execute_r(pdev, (uint8_t)AGB2_REG_ACCEL_CONFIG, (uint8_t *)&acfg, 1 * sizeof(acfg));
	pagmt->fss.a = acfg.ACCEL_FS_SEL; // Worth noting that without explicitly setting the FS range of the accelerometer it was showing the register value for +/- 2g but the reported values were actually scaled to the +/- 16g range
									  // Wait a minute... now it seems like this problem actually comes from the digital low-pass filter. When enabled the value is 1/8 what it should be...
	retval |= ICM_20948_set_bank(pdev, 2);
	ICM_20948_GYRO_CONFIG_1_t gcfg1;
	retval |= ICM_20948_execute_r(pdev, (uint8_t)AGB2_REG_GYRO_CONFIG_1, (uint8_t *)&gcfg1, 1 * sizeof(gcfg1));
	pagmt->fss.g = gcfg1.GYRO_FS_SEL;
	ICM_20948_ACCEL_CONFIG_2_t acfg2;
	retval |= ICM_20948_execute_r(pdev, (uint8_t)AGB2_REG_ACCEL_CONFIG_2, (uint8_t *)&acfg2, 1 * sizeof(acfg2));

	return retval;
}

// FIFO

ICM_20948_Status_e ICM_20948_enable_FIFO(ICM_20948_Device_t *pdev, bool enable)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;

	ICM_20948_USER_CTRL_t ctrl;
	retval = ICM_20948_set_bank(pdev, 0);
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	retval = ICM_20948_execute_r(pdev, AGB0_REG_USER_CTRL, (uint8_t *)&ctrl, sizeof(ICM_20948_USER_CTRL_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	if (enable)
			ctrl.FIFO_EN = 1;
	else
			ctrl.FIFO_EN = 0;

	retval = ICM_20948_execute_w(pdev, AGB0_REG_USER_CTRL, (uint8_t *)&ctrl, sizeof(ICM_20948_USER_CTRL_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}
	return retval;
}

ICM_20948_Status_e ICM_20948_reset_FIFO(ICM_20948_Device_t *pdev)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;

	ICM_20948_FIFO_RST_t ctrl;
	retval = ICM_20948_set_bank(pdev, 0);
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	retval = ICM_20948_execute_r(pdev, AGB0_REG_FIFO_RST, (uint8_t *)&ctrl, sizeof(ICM_20948_FIFO_RST_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	ctrl.FIFO_RESET = 0x1F; // Datasheet says "FIFO_RESET[4:0]"

	retval = ICM_20948_execute_w(pdev, AGB0_REG_FIFO_RST, (uint8_t *)&ctrl, sizeof(ICM_20948_FIFO_RST_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	//delay ???

	ctrl.FIFO_RESET = 0x1E; // The InvenSense Nucleo examples write 0x1F followed by 0x1E

	retval = ICM_20948_execute_w(pdev, AGB0_REG_FIFO_RST, (uint8_t *)&ctrl, sizeof(ICM_20948_FIFO_RST_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	return retval;
}

ICM_20948_Status_e ICM_20948_set_FIFO_mode(ICM_20948_Device_t *pdev, bool snapshot)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;

	ICM_20948_FIFO_MODE_t ctrl;
	retval = ICM_20948_set_bank(pdev, 0);
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	retval = ICM_20948_execute_r(pdev, AGB0_REG_FIFO_MODE, (uint8_t *)&ctrl, sizeof(ICM_20948_FIFO_MODE_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	if (snapshot)
			ctrl.FIFO_MODE = 0x1F; // Datasheet says "FIFO_MODE[4:0]"
	else
			ctrl.FIFO_MODE = 0;

	retval = ICM_20948_execute_w(pdev, AGB0_REG_FIFO_MODE, (uint8_t *)&ctrl, sizeof(ICM_20948_FIFO_MODE_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}
	return retval;
}

ICM_20948_Status_e ICM_20948_get_FIFO_count(ICM_20948_Device_t *pdev, uint16_t *count)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;

	ICM_20948_FIFO_COUNTH_t ctrlh;
	ICM_20948_FIFO_COUNTL_t ctrll;
	retval = ICM_20948_set_bank(pdev, 0);
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	retval = ICM_20948_execute_r(pdev, AGB0_REG_FIFO_COUNT_H, (uint8_t *)&ctrlh, sizeof(ICM_20948_FIFO_COUNTH_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	ctrlh.FIFO_COUNTH &= 0x1F; // Datasheet says "FIFO_CNT[12:8]"

	retval = ICM_20948_execute_r(pdev, AGB0_REG_FIFO_COUNT_L, (uint8_t *)&ctrll, sizeof(ICM_20948_FIFO_COUNTL_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	*count = (((uint16_t)ctrlh.FIFO_COUNTH) << 8) | (uint16_t)ctrll.FIFO_COUNTL;

	return retval;
}

ICM_20948_Status_e ICM_20948_read_FIFO(ICM_20948_Device_t *pdev, uint8_t *data, uint8_t len)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;

	retval = ICM_20948_set_bank(pdev, 0);
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	retval = ICM_20948_execute_r(pdev, AGB0_REG_FIFO_R_W, data, len);
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	return retval;
}

// DMP

ICM_20948_Status_e ICM_20948_enable_DMP(ICM_20948_Device_t *pdev, bool enable)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;

	ICM_20948_USER_CTRL_t ctrl;
	retval = ICM_20948_set_bank(pdev, 0);
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	retval = ICM_20948_execute_r(pdev, AGB0_REG_USER_CTRL, (uint8_t *)&ctrl, sizeof(ICM_20948_USER_CTRL_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	if (enable)
			ctrl.DMP_EN = 1;
	else
			ctrl.DMP_EN = 0;

	retval = ICM_20948_execute_w(pdev, AGB0_REG_USER_CTRL, (uint8_t *)&ctrl, sizeof(ICM_20948_USER_CTRL_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}
	return retval;
}

ICM_20948_Status_e ICM_20948_reset_DMP(ICM_20948_Device_t *pdev)
{
	ICM_20948_Status_e retval = ICM_20948_Stat_Ok;

	ICM_20948_USER_CTRL_t ctrl;
	retval = ICM_20948_set_bank(pdev, 0);
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	retval = ICM_20948_execute_r(pdev, AGB0_REG_USER_CTRL, (uint8_t *)&ctrl, sizeof(ICM_20948_USER_CTRL_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}

	ctrl.DMP_RST = 1;

	retval = ICM_20948_execute_w(pdev, AGB0_REG_USER_CTRL, (uint8_t *)&ctrl, sizeof(ICM_20948_USER_CTRL_t));
	if (retval != ICM_20948_Stat_Ok)
	{
		return retval;
	}
	return retval;
}

ICM_20948_Status_e ICM_20948_firmware_load(ICM_20948_Device_t *pdev)
{
#if defined(ICM_20948_USE_DMP)
    return (inv_icm20948_firmware_load(pdev, dmp3_image, sizeof(dmp3_image), DMP_LOAD_START));
#else
    return ICM_20948_Stat_DMPNotSupported;
#endif
}

/** @brief Loads the DMP firmware from SRAM
* @param[in] data  pointer where the image
* @param[in] size  size if the image
* @param[in] load_addr  address to loading the image
* @return 0 in case of success, -1 for any error
*/
ICM_20948_Status_e inv_icm20948_firmware_load(ICM_20948_Device_t *pdev, const unsigned char *data_start, unsigned short size_start, unsigned short load_addr)
{
    int write_size;
		ICM_20948_Status_e result = ICM_20948_Stat_Ok;
    unsigned short memaddr;
    const unsigned char *data;
    unsigned short size;
    unsigned char data_cmp[INV_MAX_SERIAL_READ];
    int flag = 0;

		if (pdev->_dmp_firmware_available == false)
				return ICM_20948_Stat_DMPNotSupported;

		if(pdev->_firmware_loaded)
			return ICM_20948_Stat_Ok; // Bail with no error if firmware is already loaded

		result = ICM_20948_sleep(pdev, false); // Make sure chip is awake
		if (result != ICM_20948_Stat_Ok)
		{
				return result;
		}

		result = ICM_20948_low_power(pdev, false); // Make sure chip is not in low power state
		if (result != ICM_20948_Stat_Ok)
		{
				return result;
		}

    // Write DMP memory

    data = data_start;
    size = size_start;
    memaddr = load_addr;
    while (size > 0) {
        //write_size = min(size, INV_MAX_SERIAL_WRITE); // Write in chunks of INV_MAX_SERIAL_WRITE
				if(size <= INV_MAX_SERIAL_WRITE) // Write in chunks of INV_MAX_SERIAL_WRITE
						write_size = size;
				else
						write_size = INV_MAX_SERIAL_WRITE;
        if ((memaddr & 0xff) + write_size > 0x100) {
            // Moved across a bank
            write_size = (memaddr & 0xff) + write_size - 0x100;
        }
        result = inv_icm20948_write_mems(pdev, memaddr, write_size, (unsigned char *)data);
        if (result != ICM_20948_Stat_Ok)
            return result;
        data += write_size;
        size -= write_size;
        memaddr += write_size;
    }

    // Verify DMP memory

    data = data_start;
    size = size_start;
    memaddr = load_addr;
    while (size > 0) {
        //write_size = min(size, INV_MAX_SERIAL_READ); // Read in chunks of INV_MAX_SERIAL_READ
				if(size <= INV_MAX_SERIAL_READ) // Read in chunks of INV_MAX_SERIAL_READ
						write_size = size;
				else
						write_size = INV_MAX_SERIAL_READ;
        if ((memaddr & 0xff) + write_size > 0x100) {
            // Moved across a bank
            write_size = (memaddr & 0xff) + write_size - 0x100;
        }
        result = inv_icm20948_read_mems(pdev, memaddr, write_size, data_cmp);
        if (result != ICM_20948_Stat_Ok)
            flag++; // Error, DMP not written correctly
        if (memcmp(data_cmp, data, write_size)) // Compare the data
            return ICM_20948_Stat_DMPVerifyFail;
        data += write_size;
        size -= write_size;
        memaddr += write_size;
    }

		//Enable LP_EN since we disabled it at begining of this function.

		// result = ICM_20948_low_power(pdev, true); // Put chip into low power state
		// if (result != ICM_20948_Stat_Ok)
		// 		return result;

    if(!flag)
		{
    		//Serial.println("DMP Firmware was updated successfully..");
				pdev->_firmware_loaded = true;
		}

    return result;
}

ICM_20948_Status_e ICM_20948_set_dmp_start_address(ICM_20948_Device_t *pdev, unsigned short address)
{
		ICM_20948_Status_e result = ICM_20948_Stat_Ok;

		if (pdev->_dmp_firmware_available == false)
				return ICM_20948_Stat_DMPNotSupported;

		unsigned char start_address[2];

    start_address[0] = (unsigned char)(address >> 8);
    start_address[1] = (unsigned char)(address & 0xff);

		// result = ICM_20948_sleep(pdev, false); // Make sure chip is awake
		// if (result != ICM_20948_Stat_Ok)
		// {
		// 		return result;
		// }
		//
		// result = ICM_20948_low_power(pdev, false); // Make sure chip is not in low power state
		// if (result != ICM_20948_Stat_Ok)
		// {
		// 		return result;
		// }

		result = ICM_20948_set_bank(pdev, 2); // Set bank 2
		if (result != ICM_20948_Stat_Ok)
		{
				return result;
		}

		// Write the sensor control bits into memory address AGB2_REG_PRGM_START_ADDRH
		result = ICM_20948_execute_w(pdev, AGB2_REG_PRGM_START_ADDRH, (uint8_t *)start_address, 2);

		return result;
}

/**
*  @brief       Write data to a register in DMP memory
*  @param[in]   DMP memory address
*  @param[in]   number of byte to be written
*  @param[out]  output data from the register
*  @return     0 if successful.
*/
ICM_20948_Status_e inv_icm20948_write_mems(ICM_20948_Device_t *pdev, unsigned short reg, unsigned int length, const unsigned char *data)
{
    ICM_20948_Status_e result = ICM_20948_Stat_Ok;
    unsigned int bytesWritten = 0;
    unsigned int thisLen;
    unsigned char lBankSelected;
    unsigned char lStartAddrSelected;

    if(!data)
		{
        return ICM_20948_Stat_NoData;
		}

		result = ICM_20948_set_bank(pdev, 0); // Set bank 0
		if (result != ICM_20948_Stat_Ok)
		{
				return result;
		}

    lBankSelected = (reg >> 8);

		if (lBankSelected != pdev->_last_mems_bank)
		{
				pdev->_last_mems_bank = lBankSelected;
				result = ICM_20948_execute_w(pdev, AGB0_REG_MEM_BANK_SEL, &lBankSelected, 1);
				if (result != ICM_20948_Stat_Ok)
				{
						return result;
				}
		}

    while (bytesWritten < length)
    {
        lStartAddrSelected = (reg & 0xff);

        /* Sets the starting read or write address for the selected memory, inside of the selected page (see MEM_SEL Register).
           Contents are changed after read or write of the selected memory.
           This register must be written prior to each access to initialize the register to the proper starting address.
           The address will auto increment during burst transactions.  Two consecutive bursts without re-initializing the start address would skip one address. */

				result = ICM_20948_execute_w(pdev, AGB0_REG_MEM_START_ADDR, &lStartAddrSelected, 1);
				if (result != ICM_20948_Stat_Ok)
				{
						return result;
				}

				if (length-bytesWritten <= INV_MAX_SERIAL_WRITE)
						thisLen = length-bytesWritten;
				else
						thisLen = INV_MAX_SERIAL_WRITE;

        /* Write data */

				result = ICM_20948_execute_w(pdev, AGB0_REG_MEM_R_W, (uint8_t *)&data[bytesWritten], thisLen);
				if (result != ICM_20948_Stat_Ok)
				{
						return result;
				}

        bytesWritten += thisLen;
        reg += thisLen;
    }

    return result;
}

/**
*  @brief      Read data from a register in DMP memory
*  @param[in]  DMP memory address
*  @param[in]  number of byte to be read
*  @param[in]  input data from the register
*  @return     0 if successful.
*/
ICM_20948_Status_e inv_icm20948_read_mems(ICM_20948_Device_t *pdev, unsigned short reg, unsigned int length, unsigned char *data)
{
	ICM_20948_Status_e result = ICM_20948_Stat_Ok;
	unsigned int bytesWritten = 0;
	unsigned int thisLen;
	unsigned char lBankSelected;
	unsigned char lStartAddrSelected;

	if(!data)
	{
			return ICM_20948_Stat_NoData;
	}

	result = ICM_20948_set_bank(pdev, 0); // Set bank 0
	if (result != ICM_20948_Stat_Ok)
	{
			return result;
	}

	lBankSelected = (reg >> 8);

	result = ICM_20948_execute_w(pdev, AGB0_REG_MEM_BANK_SEL, &lBankSelected, 1);
	if (result != ICM_20948_Stat_Ok)
	{
			return result;
	}

	while (bytesWritten < length)
	{
		lStartAddrSelected = (reg & 0xff);

		/* Sets the starting read or write address for the selected memory, inside of the selected page (see MEM_SEL Register).
		   Contents are changed after read or write of the selected memory.
		   This register must be written prior to each access to initialize the register to the proper starting address.
		   The address will auto increment during burst transactions.  Two consecutive bursts without re-initializing the start address would skip one address. */

		result = ICM_20948_execute_w(pdev, AGB0_REG_MEM_START_ADDR, &lStartAddrSelected, 1);
		if (result != ICM_20948_Stat_Ok)
		{
				return result;
		}

		if (length-bytesWritten <= INV_MAX_SERIAL_READ)
				thisLen = length-bytesWritten;
		else
				thisLen = INV_MAX_SERIAL_READ;

		/* Read data */

		result = ICM_20948_execute_r(pdev, AGB0_REG_MEM_R_W, &data[bytesWritten], thisLen);
		if (result != ICM_20948_Stat_Ok)
		{
				return result;
		}

		bytesWritten += thisLen;
		reg += thisLen;
	}

	return result;
}

ICM_20948_Status_e inv_icm20948_set_dmp_sensor_period(ICM_20948_Device_t *pdev, enum DMP_ODR_Registers odr_reg, uint16_t interval)
{
	// Set the ODR registersand clear the ODR counter

	// In order to set an ODR for a given sensor data, write 2-byte value to DMP using key defined above for a particular sensor.
	// Setting value can be calculated as follows:
	// Value = (DMP running rate (225Hz) / ODR ) - 1
	// E.g. For a 25Hz ODR rate, value= (225/25) -1 = 8.

	// During run-time, if an ODR is changed, the corresponding rate counter must be reset.
	// To reset, write 2-byte {0,0} to DMP using keys below for a particular sensor:

	ICM_20948_Status_e result = ICM_20948_Stat_Ok;
	ICM_20948_Status_e result2 = ICM_20948_Stat_Ok;

	if (pdev->_dmp_firmware_available == false)
			return ICM_20948_Stat_DMPNotSupported;

	unsigned char odr_reg_val[2];
	odr_reg_val[0] = (unsigned char)(interval >> 8);
	odr_reg_val[1] = (unsigned char)(interval & 0xff);

	unsigned char odr_count_zero[2] = {0x00, 0x00};

	result = ICM_20948_sleep(pdev, false); // Make sure chip is awake
	if (result != ICM_20948_Stat_Ok)
	{
			return result;
	}

	result = ICM_20948_low_power(pdev, false); // Make sure chip is not in low power state
	if (result != ICM_20948_Stat_Ok)
	{
			return result;
	}

	switch (odr_reg)
	{
			case DMP_ODR_Reg_Cpass_Calibr:
			{
					result = inv_icm20948_write_mems(pdev, ODR_CPASS_CALIBR, 2, (const unsigned char *)&odr_reg_val);
					result2 = inv_icm20948_write_mems(pdev, ODR_CNTR_CPASS_CALIBR, 2, (const unsigned char *)&odr_count_zero);
			}
			break;
			case DMP_ODR_Reg_Gyro_Calibr:
			{
					result = inv_icm20948_write_mems(pdev, ODR_GYRO_CALIBR, 2, (const unsigned char *)&odr_reg_val);
					result2 = inv_icm20948_write_mems(pdev, ODR_CNTR_GYRO_CALIBR, 2, (const unsigned char *)&odr_count_zero);
			}
			break;
			case DMP_ODR_Reg_Pressure:
			{
					result = inv_icm20948_write_mems(pdev, ODR_PRESSURE, 2, (const unsigned char *)&odr_reg_val);
					result2 = inv_icm20948_write_mems(pdev, ODR_CNTR_PRESSURE, 2, (const unsigned char *)&odr_count_zero);
			}
			break;
			case DMP_ODR_Reg_Geomag:
			{
					result = inv_icm20948_write_mems(pdev, ODR_GEOMAG, 2, (const unsigned char *)&odr_reg_val);
					result2 = inv_icm20948_write_mems(pdev, ODR_CNTR_GEOMAG, 2, (const unsigned char *)&odr_count_zero);
			}
			break;
			case DMP_ODR_Reg_PQuat6:
			{
					result = inv_icm20948_write_mems(pdev, ODR_PQUAT6, 2, (const unsigned char *)&odr_reg_val);
					result2 = inv_icm20948_write_mems(pdev, ODR_CNTR_PQUAT6, 2, (const unsigned char *)&odr_count_zero);
			}
			break;
			case DMP_ODR_Reg_Quat9:
			{
					result = inv_icm20948_write_mems(pdev, ODR_QUAT9, 2, (const unsigned char *)&odr_reg_val);
					result2 = inv_icm20948_write_mems(pdev, ODR_CNTR_QUAT9, 2, (const unsigned char *)&odr_count_zero);
			}
			break;
			case DMP_ODR_Reg_Quat6:
			{
					result = inv_icm20948_write_mems(pdev, ODR_QUAT6, 2, (const unsigned char *)&odr_reg_val);
					result2 = inv_icm20948_write_mems(pdev, ODR_CNTR_QUAT6, 2, (const unsigned char *)&odr_count_zero);
			}
			break;
			case DMP_ODR_Reg_ALS:
			{
					result = inv_icm20948_write_mems(pdev, ODR_ALS, 2, (const unsigned char *)&odr_reg_val);
					result2 = inv_icm20948_write_mems(pdev, ODR_CNTR_ALS, 2, (const unsigned char *)&odr_count_zero);
			}
			break;
			case DMP_ODR_Reg_Cpass:
			{
					result = inv_icm20948_write_mems(pdev, ODR_CPASS, 2, (const unsigned char *)&odr_reg_val);
					result2 = inv_icm20948_write_mems(pdev, ODR_CNTR_CPASS, 2, (const unsigned char *)&odr_count_zero);
			}
			break;
			case DMP_ODR_Reg_Gyro:
			{
					result = inv_icm20948_write_mems(pdev, ODR_GYRO, 2, (const unsigned char *)&odr_reg_val);
					result2 = inv_icm20948_write_mems(pdev, ODR_CNTR_GYRO, 2, (const unsigned char *)&odr_count_zero);
			}
			break;
			case DMP_ODR_Reg_Accel:
			{
					result = inv_icm20948_write_mems(pdev, ODR_ACCEL, 2, (const unsigned char *)&odr_reg_val);
					result2 = inv_icm20948_write_mems(pdev, ODR_CNTR_ACCEL, 2, (const unsigned char *)&odr_count_zero);
			}
			break;
			default:
					result = ICM_20948_Stat_InvalDMPRegister;
			break;
	}

	// result = ICM_20948_low_power(pdev, true); // Put chip into low power state
	// if (result != ICM_20948_Stat_Ok)
	// 		return result;

	if (result2 > result)
			result = result2; // Return the highest error

	return result;
}

ICM_20948_Status_e inv_icm20948_enable_dmp_sensor(ICM_20948_Device_t *pdev, enum inv_icm20948_sensor sensor, int state)
{
		// TO DO: figure out how to disable the sensor if state is 0

		ICM_20948_Status_e result = ICM_20948_Stat_Ok;

		uint16_t inv_event_control = 0;
		uint16_t data_rdy_status = 0;

		if (pdev->_dmp_firmware_available == false)
				return ICM_20948_Stat_DMPNotSupported;

		uint8_t androidSensor = sensor_type_2_android_sensor(sensor);

		if (androidSensor > ANDROID_SENSOR_NUM_MAX)
				return ICM_20948_Stat_SensorNotSupported;

		uint16_t delta = inv_androidSensor_to_control_bits[androidSensor];

		if (delta == 0xFFFF)
				return ICM_20948_Stat_SensorNotSupported;

		result = ICM_20948_sleep(pdev, false); // Make sure chip is awake
		if (result != ICM_20948_Stat_Ok)
		{
				return result;
		}

		result = ICM_20948_low_power(pdev, false); // Make sure chip is not in low power state
		if (result != ICM_20948_Stat_Ok)
		{
				return result;
		}

		// Check if Accel, Gyro/Gyro_Calibr or Compass_Calibr/Quat9/GeoMag/Compass were enabled. If they were then we need to request the accuracy data via header2.
		uint16_t delta2 = 0;
		if ((delta & DMP_Data_Output_Control_1_Accel) > 0)
		{
				delta2 |= DMP_Data_Output_Control_2_Accel_Accuracy;
		}
		if (((delta & DMP_Data_Output_Control_1_Gyro_Calibr) > 0) || ((delta & DMP_Data_Output_Control_1_Gyro) > 0))
		{
				delta2 |= DMP_Data_Output_Control_2_Gyro_Accuracy;
		}
		if (((delta & DMP_Data_Output_Control_1_Compass_Calibr) > 0) || ((delta & DMP_Data_Output_Control_1_Compass) > 0)
				|| ((delta & DMP_Data_Output_Control_1_Quat9) > 0) || ((delta & DMP_Data_Output_Control_1_Geomag) > 0))
		{
				delta2 |= DMP_Data_Output_Control_2_Compass_Accuracy;
		}
		// TO DO: Add DMP_Data_Output_Control_2_Pickup etc. if required

		// Write the sensor control bits into memory address DATA_OUT_CTL1
		unsigned char data_output_control_reg[2];
    data_output_control_reg[0] = (unsigned char)(delta >> 8);
    data_output_control_reg[1] = (unsigned char)(delta & 0xff);
		result = inv_icm20948_write_mems(pdev, DATA_OUT_CTL1, 2, (const unsigned char *)&data_output_control_reg);
		if (result != ICM_20948_Stat_Ok)
		{
				return result;
		}

		// Write the 'header2' sensor control bits into memory address DATA_OUT_CTL2
		data_output_control_reg[0] = (unsigned char)(delta2 >> 8);
    data_output_control_reg[1] = (unsigned char)(delta2 & 0xff);
		result = inv_icm20948_write_mems(pdev, DATA_OUT_CTL2, 2, (const unsigned char *)&data_output_control_reg);
		if (result != ICM_20948_Stat_Ok)
		{
				return result;
		}

		// Check which bits need to be set in the Data Ready Status and Motion Event Control registers
		// Convert androidSensor into a bit mask and compare to INV_NEEDS_ACCEL_MASK, INV_NEEDS_GYRO_MASK and INV_NEEDS_COMPASS_MASK
		unsigned long androidSensorAsBitMask;
		if (androidSensor < 32)
		{
				androidSensorAsBitMask = 1L << androidSensor;
				if ((androidSensorAsBitMask & INV_NEEDS_ACCEL_MASK) > 0)
				{
						data_rdy_status |= DMP_Data_ready_Accel;
						inv_event_control |= DMP_Motion_Event_Control_Accel_Calibr;
				}
				if ((androidSensorAsBitMask & INV_NEEDS_GYRO_MASK) > 0)
				{
						data_rdy_status |= DMP_Data_ready_Gyro;
						inv_event_control |= DMP_Motion_Event_Control_Gyro_Calibr;
				}
				if ((androidSensorAsBitMask & INV_NEEDS_COMPASS_MASK) > 0)
				{
						data_rdy_status |= DMP_Data_ready_Secondary_Compass;
						inv_event_control |= DMP_Motion_Event_Control_Compass_Calibr;
				}
		}
		else
		{
				androidSensorAsBitMask = 1L << (androidSensor - 32);
				if ((androidSensorAsBitMask & INV_NEEDS_ACCEL_MASK1) > 0)
				{
						data_rdy_status |= DMP_Data_ready_Accel;
						inv_event_control |= DMP_Motion_Event_Control_Accel_Calibr;
				}
				if ((androidSensorAsBitMask & INV_NEEDS_GYRO_MASK1) > 0)
				{
						data_rdy_status |= DMP_Data_ready_Gyro;
						inv_event_control |= DMP_Motion_Event_Control_Gyro_Calibr;
				}
				if ((androidSensorAsBitMask & INV_NEEDS_COMPASS_MASK1) > 0)
				{
						data_rdy_status |= DMP_Data_ready_Secondary_Compass;
						inv_event_control |= DMP_Motion_Event_Control_Compass_Calibr;
				}
		}
		data_output_control_reg[0] = (unsigned char)(data_rdy_status >> 8);
    data_output_control_reg[1] = (unsigned char)(data_rdy_status & 0xff);
		result = inv_icm20948_write_mems(pdev, DATA_RDY_STATUS, 2, (const unsigned char *)&data_output_control_reg);
		if (result != ICM_20948_Stat_Ok)
		{
				return result;
		}

		// Check which extra bits need to be set in the Motion Event Control register
		if ((delta & DMP_Data_Output_Control_1_Quat9) > 0)
		{
				inv_event_control |= DMP_Motion_Event_Control_9axis;
		}
		if (((delta & DMP_Data_Output_Control_1_Step_Detector) > 0) || ((delta & DMP_Data_Output_Control_1_Step_Ind_0) > 0)
				 || ((delta & DMP_Data_Output_Control_1_Step_Ind_1) > 0) || ((delta & DMP_Data_Output_Control_1_Step_Ind_2) > 0))
		{
				inv_event_control |= DMP_Motion_Event_Control_Pedometer_Interrupt;
		}
		if ((delta & DMP_Data_Output_Control_1_Geomag) > 0)
		{
				inv_event_control |= DMP_Motion_Event_Control_Geomag;
		}
		data_output_control_reg[0] = (unsigned char)(inv_event_control >> 8);
    data_output_control_reg[1] = (unsigned char)(inv_event_control & 0xff);
		result = inv_icm20948_write_mems(pdev, MOTION_EVENT_CTL, 2, (const unsigned char *)&data_output_control_reg);
		if (result != ICM_20948_Stat_Ok)
		{
				return result;
		}

		// result = ICM_20948_low_power(pdev, true); // Put chip into low power state
		// if (result != ICM_20948_Stat_Ok)
		// 		return result;

		return result;
}

ICM_20948_Status_e inv_icm20948_enable_dmp_sensor_int(ICM_20948_Device_t *pdev, enum inv_icm20948_sensor sensor, int state)
{
		ICM_20948_Status_e result = ICM_20948_Stat_Ok;

		if (pdev->_dmp_firmware_available == false)
				return ICM_20948_Stat_DMPNotSupported;

		uint8_t androidSensor = sensor_type_2_android_sensor(sensor);

		if (androidSensor > ANDROID_SENSOR_NUM_MAX)
				return ICM_20948_Stat_SensorNotSupported;

		uint16_t delta = inv_androidSensor_to_control_bits[androidSensor];

		if (delta == 0xFFFF)
				return ICM_20948_Stat_SensorNotSupported;

		if (state == 0)
				delta = 0;

		unsigned char data_intr_ctl[2];

    data_intr_ctl[0] = (unsigned char)(delta >> 8);
    data_intr_ctl[1] = (unsigned char)(delta & 0xff);

		result = ICM_20948_sleep(pdev, false); // Make sure chip is awake
		if (result != ICM_20948_Stat_Ok)
		{
				return result;
		}

		result = ICM_20948_low_power(pdev, false); // Make sure chip is not in low power state
		if (result != ICM_20948_Stat_Ok)
		{
				return result;
		}

		// Write the sensor control bits into memory address DATA_OUT_CTL1
		result = inv_icm20948_write_mems(pdev, DATA_INTR_CTL, 2, (const unsigned char *)&data_intr_ctl);

		// result = ICM_20948_low_power(pdev, true); // Put chip into low power state
		// if (result != ICM_20948_Stat_Ok)
		// 		return result;

		return result;
}

ICM_20948_Status_e inv_icm20948_read_dmp_data(ICM_20948_Device_t *pdev, icm_20948_DMP_data_t *data)
{
	ICM_20948_Status_e result = ICM_20948_Stat_Ok;
	uint8_t fifoBytes[icm_20948_DMP_Maximum_Bytes]; // Interim storage for the FIFO data

	if (pdev->_dmp_firmware_available == false)
			return ICM_20948_Stat_DMPNotSupported;

	// Check how much data is in the FIFO
	uint16_t fifo_count;
	result = ICM_20948_get_FIFO_count(pdev, &fifo_count);
	if (result != ICM_20948_Stat_Ok)
			return result;

	if (fifo_count < icm_20948_DMP_Header_Bytes) // Has a 2-byte header arrived?
			return ICM_20948_Stat_FIFONoDataAvail; // Bail if no header is available

	// Read the header (2 bytes)
	data->header = 0; // Clear the existing header
	uint16_t aShort = 0;
	result = ICM_20948_read_FIFO(pdev, &fifoBytes[0], icm_20948_DMP_Header_Bytes);
	if (result != ICM_20948_Stat_Ok)
			return result;
	for (int i = 0; i < icm_20948_DMP_Header_Bytes; i++)
	{
			aShort |= ((uint16_t)fifoBytes[i]) << (8 - (i * 8));
	}
	data->header = aShort; // Store the header in data->header
	fifo_count -= icm_20948_DMP_Header_Bytes; // Decrement the count

	// If the header indicates a header2 is present then read that now
	data->header2 = 0; // Clear the existing header2
	if ((data->header & DMP_header_bitmap_Header2) > 0) // If the header2 bit is set
	{
			if (fifo_count < icm_20948_DMP_Header2_Bytes) // Check if we need to read the FIFO count again
			{
					result = ICM_20948_get_FIFO_count(pdev, &fifo_count);
					if (result != ICM_20948_Stat_Ok)
							return result;
			}
			if (fifo_count < icm_20948_DMP_Header2_Bytes)
					return ICM_20948_Stat_FIFONoDataAvail; // Bail if no header2 is available
			// Read the header (2 bytes)
			aShort = 0;
			result = ICM_20948_read_FIFO(pdev, &fifoBytes[0], icm_20948_DMP_Header2_Bytes);
			if (result != ICM_20948_Stat_Ok)
					return result;
			for (int i = 0; i < icm_20948_DMP_Header2_Bytes; i++)
			{
					aShort |= ((uint16_t)fifoBytes[i]) << (8 - (i * 8));
			}
			data->header2 = aShort; // Store the header2 in data->header2
			fifo_count -= icm_20948_DMP_Header2_Bytes; // Decrement the count
	}

	if ((data->header & DMP_header_bitmap_Accel) > 0) // case DMP_header_bitmap_Accel:
	{
			if (fifo_count < icm_20948_DMP_Raw_Accel_Bytes) // Check if we need to read the FIFO count again
			{
					result = ICM_20948_get_FIFO_count(pdev, &fifo_count);
					if (result != ICM_20948_Stat_Ok)
							return result;
			}
			if (fifo_count < icm_20948_DMP_Raw_Accel_Bytes)
					return ICM_20948_Stat_FIFONoDataAvail; // Bail if not enough data is available
			result = ICM_20948_read_FIFO(pdev, &fifoBytes[0], icm_20948_DMP_Raw_Accel_Bytes);
			if (result != ICM_20948_Stat_Ok)
					return result;
			for (int i = 0; i < icm_20948_DMP_Raw_Accel_Bytes; i++)
			{
					data->Raw_Accel.Bytes[DMP_PQuat6_Byte_Ordering[i]] = fifoBytes[i]; // Correct the byte order (map big endian to little endian)
			}
			fifo_count -= icm_20948_DMP_Raw_Accel_Bytes; // Decrement the count
	}

	if ((data->header & DMP_header_bitmap_Gyro) > 0) // case DMP_header_bitmap_Gyro:
	{
			if (fifo_count < (icm_20948_DMP_Raw_Gyro_Bytes + icm_20948_DMP_Gyro_Bias_Bytes)) // Check if we need to read the FIFO count again
			{
					result = ICM_20948_get_FIFO_count(pdev, &fifo_count);
					if (result != ICM_20948_Stat_Ok)
							return result;
			}
			if (fifo_count < (icm_20948_DMP_Raw_Gyro_Bytes + icm_20948_DMP_Gyro_Bias_Bytes))
					return ICM_20948_Stat_FIFONoDataAvail; // Bail if not enough data is available
			result = ICM_20948_read_FIFO(pdev, &fifoBytes[0], (icm_20948_DMP_Raw_Gyro_Bytes + icm_20948_DMP_Gyro_Bias_Bytes));
			if (result != ICM_20948_Stat_Ok)
					return result;
			for (int i = 0; i < (icm_20948_DMP_Raw_Gyro_Bytes + icm_20948_DMP_Gyro_Bias_Bytes); i++)
			{
					data->Raw_Gyro.Bytes[DMP_Raw_Gyro_Byte_Ordering[i]] = fifoBytes[i]; // Correct the byte order (map big endian to little endian)
			}
			fifo_count -= (icm_20948_DMP_Raw_Gyro_Bytes + icm_20948_DMP_Gyro_Bias_Bytes); // Decrement the count
	}

	if ((data->header & DMP_header_bitmap_Compass) > 0) // case DMP_header_bitmap_Compass:
	{
			if (fifo_count < icm_20948_DMP_Compass_Bytes) // Check if we need to read the FIFO count again
			{
					result = ICM_20948_get_FIFO_count(pdev, &fifo_count);
					if (result != ICM_20948_Stat_Ok)
							return result;
			}
			if (fifo_count < icm_20948_DMP_Compass_Bytes)
					return ICM_20948_Stat_FIFONoDataAvail; // Bail if not enough data is available
			result = ICM_20948_read_FIFO(pdev, &fifoBytes[0], icm_20948_DMP_Compass_Bytes);
			if (result != ICM_20948_Stat_Ok)
					return result;
			for (int i = 0; i < icm_20948_DMP_Compass_Bytes; i++)
			{
					data->Compass.Bytes[DMP_PQuat6_Byte_Ordering[i]] = fifoBytes[i]; // Correct the byte order (map big endian to little endian)
			}
			fifo_count -= icm_20948_DMP_Compass_Bytes; // Decrement the count
	}

	if ((data->header & DMP_header_bitmap_ALS) > 0) // case DMP_header_bitmap_ALS:
	{
			if (fifo_count < icm_20948_DMP_ALS_Bytes) // Check if we need to read the FIFO count again
			{
					result = ICM_20948_get_FIFO_count(pdev, &fifo_count);
					if (result != ICM_20948_Stat_Ok)
							return result;
			}
			if (fifo_count < icm_20948_DMP_ALS_Bytes)
					return ICM_20948_Stat_FIFONoDataAvail; // Bail if not enough data is available
			result = ICM_20948_read_FIFO(pdev, &fifoBytes[0], icm_20948_DMP_ALS_Bytes);
			if (result != ICM_20948_Stat_Ok)
					return result;
			for (int i = 0; i < icm_20948_DMP_ALS_Bytes; i++)
			{
					data->ALS[i] = fifoBytes[i];
			}
			fifo_count -= icm_20948_DMP_ALS_Bytes; // Decrement the count
	}

	if ((data->header & DMP_header_bitmap_Quat6) > 0) // case DMP_header_bitmap_Quat6:
	{
			if (fifo_count < icm_20948_DMP_Quat6_Bytes) // Check if we need to read the FIFO count again
			{
					result = ICM_20948_get_FIFO_count(pdev, &fifo_count);
					if (result != ICM_20948_Stat_Ok)
							return result;
			}
			if (fifo_count < icm_20948_DMP_Quat6_Bytes)
					return ICM_20948_Stat_FIFONoDataAvail; // Bail if not enough data is available
			result = ICM_20948_read_FIFO(pdev, &fifoBytes[0], icm_20948_DMP_Quat6_Bytes);
			if (result != ICM_20948_Stat_Ok)
					return result;
			for (int i = 0; i < icm_20948_DMP_Quat6_Bytes; i++)
			{
					data->Quat6.Bytes[DMP_Quat6_Byte_Ordering[i]] = fifoBytes[i]; // Correct the byte order (map big endian to little endian)
			}
			fifo_count -= icm_20948_DMP_Quat6_Bytes; // Decrement the count
	}

	if ((data->header & DMP_header_bitmap_Quat9) > 0) // case DMP_header_bitmap_Quat9:
	{
			if (fifo_count < icm_20948_DMP_Quat9_Bytes) // Check if we need to read the FIFO count again
			{
					result = ICM_20948_get_FIFO_count(pdev, &fifo_count);
					if (result != ICM_20948_Stat_Ok)
							return result;
			}
			if (fifo_count < icm_20948_DMP_Quat9_Bytes)
					return ICM_20948_Stat_FIFONoDataAvail; // Bail if not enough data is available
			result = ICM_20948_read_FIFO(pdev, &fifoBytes[0], icm_20948_DMP_Quat9_Bytes);
			if (result != ICM_20948_Stat_Ok)
					return result;
			for (int i = 0; i < icm_20948_DMP_Quat9_Bytes; i++)
			{
					data->Quat9.Bytes[DMP_Quat9_Byte_Ordering[i]] = fifoBytes[i]; // Correct the byte order (map big endian to little endian)
			}
			fifo_count -= icm_20948_DMP_Quat9_Bytes; // Decrement the count
	}

	if ((data->header & DMP_header_bitmap_PQuat6) > 0) // case DMP_header_bitmap_PQuat6:
	{
			if (fifo_count < icm_20948_DMP_PQuat6_Bytes) // Check if we need to read the FIFO count again
			{
					result = ICM_20948_get_FIFO_count(pdev, &fifo_count);
					if (result != ICM_20948_Stat_Ok)
							return result;
			}
			if (fifo_count < icm_20948_DMP_PQuat6_Bytes)
					return ICM_20948_Stat_FIFONoDataAvail; // Bail if not enough data is available
			result = ICM_20948_read_FIFO(pdev, &fifoBytes[0], icm_20948_DMP_PQuat6_Bytes);
			if (result != ICM_20948_Stat_Ok)
					return result;
			for (int i = 0; i < icm_20948_DMP_PQuat6_Bytes; i++)
			{
					data->PQuat6.Bytes[DMP_PQuat6_Byte_Ordering[i]] = fifoBytes[i]; // Correct the byte order (map big endian to little endian)
			}
			fifo_count -= icm_20948_DMP_PQuat6_Bytes; // Decrement the count
	}

	if ((data->header & DMP_header_bitmap_Geomag) > 0) // case DMP_header_bitmap_Geomag:
	{
			if (fifo_count < icm_20948_DMP_Geomag_Bytes) // Check if we need to read the FIFO count again
			{
					result = ICM_20948_get_FIFO_count(pdev, &fifo_count);
					if (result != ICM_20948_Stat_Ok)
							return result;
			}
			if (fifo_count < icm_20948_DMP_Geomag_Bytes)
					return ICM_20948_Stat_FIFONoDataAvail; // Bail if not enough data is available
			result = ICM_20948_read_FIFO(pdev, &fifoBytes[0], icm_20948_DMP_Geomag_Bytes);
			if (result != ICM_20948_Stat_Ok)
					return result;
			for (int i = 0; i < icm_20948_DMP_Geomag_Bytes; i++)
			{
					data->Geomag.Bytes[DMP_Quat9_Byte_Ordering[i]] = fifoBytes[i]; // Correct the byte order (map big endian to little endian)
			}
			fifo_count -= icm_20948_DMP_Geomag_Bytes; // Decrement the count
	}

	if ((data->header & DMP_header_bitmap_Pressure) > 0) // case DMP_header_bitmap_Pressure:
	{
			if (fifo_count < icm_20948_DMP_Pressure_Bytes) // Check if we need to read the FIFO count again
			{
					result = ICM_20948_get_FIFO_count(pdev, &fifo_count);
					if (result != ICM_20948_Stat_Ok)
							return result;
			}
			if (fifo_count < icm_20948_DMP_Pressure_Bytes)
					return ICM_20948_Stat_FIFONoDataAvail; // Bail if not enough data is available
			result = ICM_20948_read_FIFO(pdev, &fifoBytes[0], icm_20948_DMP_Pressure_Bytes);
			if (result != ICM_20948_Stat_Ok)
					return result;
			for (int i = 0; i < icm_20948_DMP_Pressure_Bytes; i++)
			{
					data->Pressure[i] = fifoBytes[i];
			}
			fifo_count -= icm_20948_DMP_Pressure_Bytes; // Decrement the count
	}

	if ((data->header & DMP_header_bitmap_Gyro_Calibr) > 0) // case DMP_header_bitmap_Gyro_Calibr:
	{
			if (fifo_count < icm_20948_DMP_Gyro_Calibr_Bytes) // Check if we need to read the FIFO count again
			{
					result = ICM_20948_get_FIFO_count(pdev, &fifo_count);
					if (result != ICM_20948_Stat_Ok)
							return result;
			}
			if (fifo_count < icm_20948_DMP_Gyro_Calibr_Bytes)
					return ICM_20948_Stat_FIFONoDataAvail; // Bail if not enough data is available
			result = ICM_20948_read_FIFO(pdev, &fifoBytes[0], icm_20948_DMP_Gyro_Calibr_Bytes);
			if (result != ICM_20948_Stat_Ok)
					return result;
			for (int i = 0; i < icm_20948_DMP_Gyro_Calibr_Bytes; i++)
			{
					data->Gyro_Calibr.Bytes[DMP_Quat6_Byte_Ordering[i]] = fifoBytes[i]; // Correct the byte order (map big endian to little endian)
			}
			fifo_count -= icm_20948_DMP_Gyro_Calibr_Bytes; // Decrement the count
	}

	if ((data->header & DMP_header_bitmap_Compass_Calibr) > 0) // case DMP_header_bitmap_Compass_Calibr:
	{
			if (fifo_count < icm_20948_DMP_Compass_Calibr_Bytes) // Check if we need to read the FIFO count again
			{
					result = ICM_20948_get_FIFO_count(pdev, &fifo_count);
					if (result != ICM_20948_Stat_Ok)
							return result;
			}
			if (fifo_count < icm_20948_DMP_Compass_Calibr_Bytes)
					return ICM_20948_Stat_FIFONoDataAvail; // Bail if not enough data is available
			result = ICM_20948_read_FIFO(pdev, &fifoBytes[0], icm_20948_DMP_Compass_Calibr_Bytes);
			if (result != ICM_20948_Stat_Ok)
					return result;
			for (int i = 0; i < icm_20948_DMP_Compass_Calibr_Bytes; i++)
			{
					data->Compass_Calibr.Bytes[DMP_Quat6_Byte_Ordering[i]] = fifoBytes[i]; // Correct the byte order (map big endian to little endian)
			}
			fifo_count -= icm_20948_DMP_Compass_Calibr_Bytes; // Decrement the count
	}

	if ((data->header & DMP_header_bitmap_Step_Detector) > 0) // case DMP_header_bitmap_Step_Detector:
	{
			if (fifo_count < icm_20948_DMP_Step_Detector_Bytes) // Check if we need to read the FIFO count again
			{
					result = ICM_20948_get_FIFO_count(pdev, &fifo_count);
					if (result != ICM_20948_Stat_Ok)
							return result;
			}
			if (fifo_count < icm_20948_DMP_Step_Detector_Bytes)
					return ICM_20948_Stat_FIFONoDataAvail; // Bail if not enough data is available
			result = ICM_20948_read_FIFO(pdev, &fifoBytes[0], icm_20948_DMP_Step_Detector_Bytes);
			if (result != ICM_20948_Stat_Ok)
					return result;
			uint32_t aWord = 0;
			for (int i = 0; i < icm_20948_DMP_Step_Detector_Bytes; i++)
			{
					aWord |= ((uint32_t)fifoBytes[i]) << (24 - (i * 8));
			}
			data->Pedometer_Timestamp = aWord;
			fifo_count -= icm_20948_DMP_Step_Detector_Bytes; // Decrement the count
	}

	// Now check for header2 features

	if ((data->header2 & DMP_header2_bitmap_Accel_Accuracy) > 0) // case DMP_header2_bitmap_Accel_Accuracy:
	{
			if (fifo_count < icm_20948_DMP_Accel_Accuracy_Bytes) // Check if we need to read the FIFO count again
			{
					result = ICM_20948_get_FIFO_count(pdev, &fifo_count);
					if (result != ICM_20948_Stat_Ok)
							return result;
			}
			if (fifo_count < icm_20948_DMP_Accel_Accuracy_Bytes)
					return ICM_20948_Stat_FIFONoDataAvail; // Bail if not enough data is available
			aShort = 0;
			result = ICM_20948_read_FIFO(pdev, &fifoBytes[0], icm_20948_DMP_Accel_Accuracy_Bytes);
			if (result != ICM_20948_Stat_Ok)
					return result;
			for (int i = 0; i < icm_20948_DMP_Accel_Accuracy_Bytes; i++)
			{
					aShort |= ((uint16_t)fifoBytes[i]) << (8 - (i * 8));
			}
			data->Accel_Accuracy = aShort;
			fifo_count -= icm_20948_DMP_Accel_Accuracy_Bytes; // Decrement the count
	}

	if ((data->header2 & DMP_header2_bitmap_Gyro_Accuracy) > 0) // case DMP_header2_bitmap_Gyro_Accuracy:
	{
			if (fifo_count < icm_20948_DMP_Gyro_Accuracy_Bytes) // Check if we need to read the FIFO count again
			{
					result = ICM_20948_get_FIFO_count(pdev, &fifo_count);
					if (result != ICM_20948_Stat_Ok)
							return result;
			}
			if (fifo_count < icm_20948_DMP_Gyro_Accuracy_Bytes)
					return ICM_20948_Stat_FIFONoDataAvail; // Bail if not enough data is available
			aShort = 0;
			result = ICM_20948_read_FIFO(pdev, &fifoBytes[0], icm_20948_DMP_Gyro_Accuracy_Bytes);
			if (result != ICM_20948_Stat_Ok)
					return result;
			for (int i = 0; i < icm_20948_DMP_Gyro_Accuracy_Bytes; i++)
			{
					aShort |= ((uint16_t)fifoBytes[i]) << (8 - (i * 8));
			}
			data->Gyro_Accuracy = aShort;
			fifo_count -= icm_20948_DMP_Gyro_Accuracy_Bytes; // Decrement the count
	}

	if ((data->header2 & DMP_header2_bitmap_Compass_Accuracy) > 0) // case DMP_header2_bitmap_Compass_Accuracy:
	{
			if (fifo_count < icm_20948_DMP_Compass_Accuracy_Bytes) // Check if we need to read the FIFO count again
			{
					result = ICM_20948_get_FIFO_count(pdev, &fifo_count);
					if (result != ICM_20948_Stat_Ok)
							return result;
			}
			if (fifo_count < icm_20948_DMP_Compass_Accuracy_Bytes)
					return ICM_20948_Stat_FIFONoDataAvail; // Bail if not enough data is available
			aShort = 0;
			result = ICM_20948_read_FIFO(pdev, &fifoBytes[0], icm_20948_DMP_Compass_Accuracy_Bytes);
			if (result != ICM_20948_Stat_Ok)
					return result;
			for (int i = 0; i < icm_20948_DMP_Compass_Accuracy_Bytes; i++)
			{
					aShort |= ((uint16_t)fifoBytes[i]) << (8 - (i * 8));
			}
			data->Compass_Accuracy = aShort;
			fifo_count -= icm_20948_DMP_Compass_Accuracy_Bytes; // Decrement the count
	}

	if ((data->header2 & DMP_header2_bitmap_Fsync) > 0) // case DMP_header2_bitmap_Fsync:
	{
			if (fifo_count < icm_20948_DMP_Fsync_Detection_Bytes) // Check if we need to read the FIFO count again
			{
					result = ICM_20948_get_FIFO_count(pdev, &fifo_count);
					if (result != ICM_20948_Stat_Ok)
							return result;
			}
			if (fifo_count < icm_20948_DMP_Fsync_Detection_Bytes)
					return ICM_20948_Stat_FIFONoDataAvail; // Bail if not enough data is available
			aShort = 0;
			result = ICM_20948_read_FIFO(pdev, &fifoBytes[0], icm_20948_DMP_Fsync_Detection_Bytes);
			if (result != ICM_20948_Stat_Ok)
					return result;
			for (int i = 0; i < icm_20948_DMP_Fsync_Detection_Bytes; i++)
			{
					aShort |= ((uint16_t)fifoBytes[i]) << (8 - (i * 8));
			}
			data->Fsync_Delay_Time = aShort;
			fifo_count -= icm_20948_DMP_Fsync_Detection_Bytes; // Decrement the count
	}

	if ((data->header2 & DMP_header2_bitmap_Pickup) > 0) // case DMP_header2_bitmap_Pickup:
	{
			if (fifo_count < icm_20948_DMP_Pickup_Bytes) // Check if we need to read the FIFO count again
			{
					result = ICM_20948_get_FIFO_count(pdev, &fifo_count);
					if (result != ICM_20948_Stat_Ok)
							return result;
			}
			if (fifo_count < icm_20948_DMP_Pickup_Bytes)
					return ICM_20948_Stat_FIFONoDataAvail; // Bail if not enough data is available
			aShort = 0;
			result = ICM_20948_read_FIFO(pdev, &fifoBytes[0], icm_20948_DMP_Pickup_Bytes);
			if (result != ICM_20948_Stat_Ok)
					return result;
			for (int i = 0; i < icm_20948_DMP_Pickup_Bytes; i++)
			{
					aShort |= ((uint16_t)fifoBytes[i]) << (8 - (i * 8));
			}
			data->Pickup = aShort;
			fifo_count -= icm_20948_DMP_Pickup_Bytes; // Decrement the count
	}

	if ((data->header2 & DMP_header2_bitmap_Activity_Recog) > 0) // case DMP_header2_bitmap_Activity_Recog:
	{
		if (fifo_count < icm_20948_DMP_Activity_Recognition_Bytes) // Check if we need to read the FIFO count again
		{
				result = ICM_20948_get_FIFO_count(pdev, &fifo_count);
				if (result != ICM_20948_Stat_Ok)
						return result;
		}
		if (fifo_count < icm_20948_DMP_Activity_Recognition_Bytes)
				return ICM_20948_Stat_FIFONoDataAvail; // Bail if not enough data is available
		result = ICM_20948_read_FIFO(pdev, &fifoBytes[0], icm_20948_DMP_Activity_Recognition_Bytes);
		if (result != ICM_20948_Stat_Ok)
				return result;
		for (int i = 0; i < icm_20948_DMP_Activity_Recognition_Bytes; i++)
		{
				data->Activity_Recognition.Bytes[DMP_Activity_Recognition_Byte_Ordering[i]] = fifoBytes[i];
		}
		fifo_count -= icm_20948_DMP_Activity_Recognition_Bytes; // Decrement the count
	}

	if ((data->header2 & DMP_header2_bitmap_Secondary_On_Off) > 0) // case DMP_header2_bitmap_Secondary_On_Off:
	{
			if (fifo_count < icm_20948_DMP_Secondary_On_Off_Bytes) // Check if we need to read the FIFO count again
			{
					result = ICM_20948_get_FIFO_count(pdev, &fifo_count);
					if (result != ICM_20948_Stat_Ok)
							return result;
			}
			if (fifo_count < icm_20948_DMP_Secondary_On_Off_Bytes)
					return ICM_20948_Stat_FIFONoDataAvail; // Bail if not enough data is available
			result = ICM_20948_read_FIFO(pdev, &fifoBytes[0], icm_20948_DMP_Secondary_On_Off_Bytes);
			if (result != ICM_20948_Stat_Ok)
					return result;
			for (int i = 0; i < icm_20948_DMP_Secondary_On_Off_Bytes; i++)
			{
					data->Secondary_On_Off.Bytes[DMP_Secondary_On_Off_Byte_Ordering[i]] = fifoBytes[i];
			}
			fifo_count -= icm_20948_DMP_Secondary_On_Off_Bytes; // Decrement the count
	}

	// Finally, extract the footer (gyro count)
	if (fifo_count < icm_20948_DMP_Footer_Bytes) // Check if we need to read the FIFO count again
	{
			result = ICM_20948_get_FIFO_count(pdev, &fifo_count);
			if (result != ICM_20948_Stat_Ok)
					return result;
	}
	if (fifo_count < icm_20948_DMP_Footer_Bytes)
			return ICM_20948_Stat_FIFONoDataAvail; // Bail if not enough data is available
	aShort = 0;
	result = ICM_20948_read_FIFO(pdev, &fifoBytes[0], icm_20948_DMP_Footer_Bytes);
	if (result != ICM_20948_Stat_Ok)
			return result;
	for (int i = 0; i < icm_20948_DMP_Footer_Bytes; i++)
	{
			aShort |= ((uint16_t)fifoBytes[i]) << (8 - (i * 8));
	}
	data->Footer = aShort;
	fifo_count -= icm_20948_DMP_Footer_Bytes; // Decrement the count

	if (fifo_count > 0) // Check if there is still data waiting to be read
		return ICM_20948_Stat_FIFOMoreDataAvail;

	return result;
}

static uint8_t sensor_type_2_android_sensor(enum inv_icm20948_sensor sensor)
{
	switch(sensor) {
	case INV_ICM20948_SENSOR_ACCELEROMETER:                 return ANDROID_SENSOR_ACCELEROMETER; // 1
	case INV_ICM20948_SENSOR_GYROSCOPE:                     return ANDROID_SENSOR_GYROSCOPE; // 4
	case INV_ICM20948_SENSOR_RAW_ACCELEROMETER:             return ANDROID_SENSOR_RAW_ACCELEROMETER; // 42
	case INV_ICM20948_SENSOR_RAW_GYROSCOPE:                 return ANDROID_SENSOR_RAW_GYROSCOPE; // 43
	case INV_ICM20948_SENSOR_MAGNETIC_FIELD_UNCALIBRATED:   return ANDROID_SENSOR_MAGNETIC_FIELD_UNCALIBRATED; // 14
	case INV_ICM20948_SENSOR_GYROSCOPE_UNCALIBRATED:        return ANDROID_SENSOR_GYROSCOPE_UNCALIBRATED; // 16
	case INV_ICM20948_SENSOR_ACTIVITY_CLASSIFICATON:        return ANDROID_SENSOR_ACTIVITY_CLASSIFICATON; // 47
	case INV_ICM20948_SENSOR_STEP_DETECTOR:                 return ANDROID_SENSOR_STEP_DETECTOR; // 18
	case INV_ICM20948_SENSOR_STEP_COUNTER:                  return ANDROID_SENSOR_STEP_COUNTER; // 19
	case INV_ICM20948_SENSOR_GAME_ROTATION_VECTOR:          return ANDROID_SENSOR_GAME_ROTATION_VECTOR; // 15
	case INV_ICM20948_SENSOR_ROTATION_VECTOR:               return ANDROID_SENSOR_ROTATION_VECTOR; // 11
	case INV_ICM20948_SENSOR_GEOMAGNETIC_ROTATION_VECTOR:   return ANDROID_SENSOR_GEOMAGNETIC_ROTATION_VECTOR; // 20
	case INV_ICM20948_SENSOR_GEOMAGNETIC_FIELD:             return ANDROID_SENSOR_GEOMAGNETIC_FIELD; // 2
	case INV_ICM20948_SENSOR_WAKEUP_SIGNIFICANT_MOTION:     return ANDROID_SENSOR_WAKEUP_SIGNIFICANT_MOTION; // 17
	case INV_ICM20948_SENSOR_FLIP_PICKUP:                   return ANDROID_SENSOR_FLIP_PICKUP; // 46
	case INV_ICM20948_SENSOR_WAKEUP_TILT_DETECTOR:          return ANDROID_SENSOR_WAKEUP_TILT_DETECTOR; // 41
	case INV_ICM20948_SENSOR_GRAVITY:                       return ANDROID_SENSOR_GRAVITY; // 9
	case INV_ICM20948_SENSOR_LINEAR_ACCELERATION:           return ANDROID_SENSOR_LINEAR_ACCELERATION; // 10
	case INV_ICM20948_SENSOR_ORIENTATION:                   return ANDROID_SENSOR_ORIENTATION; // 3
	case INV_ICM20948_SENSOR_B2S:                           return ANDROID_SENSOR_B2S; // 45
	default:                                                return ANDROID_SENSOR_NUM_MAX;
	}
}

enum inv_icm20948_sensor inv_icm20948_sensor_android_2_sensor_type(int sensor)
{
	switch(sensor) {
	case ANDROID_SENSOR_ACCELEROMETER:                    return INV_ICM20948_SENSOR_ACCELEROMETER;
	case ANDROID_SENSOR_GYROSCOPE:                        return INV_ICM20948_SENSOR_GYROSCOPE;
	case ANDROID_SENSOR_RAW_ACCELEROMETER:                return INV_ICM20948_SENSOR_RAW_ACCELEROMETER;
	case ANDROID_SENSOR_RAW_GYROSCOPE:                    return INV_ICM20948_SENSOR_RAW_GYROSCOPE;
	case ANDROID_SENSOR_MAGNETIC_FIELD_UNCALIBRATED:      return INV_ICM20948_SENSOR_MAGNETIC_FIELD_UNCALIBRATED;
	case ANDROID_SENSOR_GYROSCOPE_UNCALIBRATED:           return INV_ICM20948_SENSOR_GYROSCOPE_UNCALIBRATED;
	case ANDROID_SENSOR_ACTIVITY_CLASSIFICATON:           return INV_ICM20948_SENSOR_ACTIVITY_CLASSIFICATON;
	case ANDROID_SENSOR_STEP_DETECTOR:                    return INV_ICM20948_SENSOR_STEP_DETECTOR;
	case ANDROID_SENSOR_STEP_COUNTER:                     return INV_ICM20948_SENSOR_STEP_COUNTER;
	case ANDROID_SENSOR_GAME_ROTATION_VECTOR:             return INV_ICM20948_SENSOR_GAME_ROTATION_VECTOR;
	case ANDROID_SENSOR_ROTATION_VECTOR:                  return INV_ICM20948_SENSOR_ROTATION_VECTOR;
	case ANDROID_SENSOR_GEOMAGNETIC_ROTATION_VECTOR:      return INV_ICM20948_SENSOR_GEOMAGNETIC_ROTATION_VECTOR;
	case ANDROID_SENSOR_GEOMAGNETIC_FIELD:                return INV_ICM20948_SENSOR_GEOMAGNETIC_FIELD;
	case ANDROID_SENSOR_WAKEUP_SIGNIFICANT_MOTION:        return INV_ICM20948_SENSOR_WAKEUP_SIGNIFICANT_MOTION;
	case ANDROID_SENSOR_FLIP_PICKUP:                      return INV_ICM20948_SENSOR_FLIP_PICKUP;
	case ANDROID_SENSOR_WAKEUP_TILT_DETECTOR:             return INV_ICM20948_SENSOR_WAKEUP_TILT_DETECTOR;
	case ANDROID_SENSOR_GRAVITY:                          return INV_ICM20948_SENSOR_GRAVITY;
	case ANDROID_SENSOR_LINEAR_ACCELERATION:              return INV_ICM20948_SENSOR_LINEAR_ACCELERATION;
	case ANDROID_SENSOR_ORIENTATION:                      return INV_ICM20948_SENSOR_ORIENTATION;
	case ANDROID_SENSOR_B2S:                              return INV_ICM20948_SENSOR_B2S;
	default:                                              return INV_ICM20948_SENSOR_MAX;
	}
}
