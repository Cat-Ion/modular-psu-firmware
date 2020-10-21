/*
 * EEZ Modular Firmware
 * Copyright (C) 2015-present, Envox d.o.o.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <new>
#include <assert.h>

#include <eez/modules/dib-dcp405/dib-dcp405.h>
#include <eez/modules/dib-dcp405/adc.h>
#include <eez/modules/dib-dcp405/dac.h>
#include <eez/modules/dib-dcp405/ioexp.h>

#include <eez/modules/psu/psu.h>
#include <eez/modules/psu/debug.h>
#include <eez/modules/psu/profile.h>
#include <eez/modules/psu/channel_dispatcher.h>

#include <eez/scpi/regs.h>

#include <eez/system.h>
#include <eez/index.h>

#include <eez/gui/gui.h>

#if defined(EEZ_PLATFORM_STM32)
#include <eez/drivers/tmp1075.h>
#include <eez/drivers/tc77.h>
#endif

/// ADC conversion should be finished after ADC_CONVERSION_MAX_TIME_MS milliseconds.
#define CONF_ADC_CONVERSION_MAX_TIME_MS 10

#define CONF_FALLING_EDGE_OVP_PERCENTAGE 2.0f

#define CONF_FALLING_EDGE_HW_OVP_DELAY_MS 2
#define CONF_FALLING_EDGE_SW_OVP_DELAY_MS 5

#define CONF_OVP_SW_OVP_AT_START_DURATION_MS 5
#define CONF_OVP_SW_OVP_AT_START_U_SET_THRESHOLD 1.2f
#define CONF_OVP_SW_OVP_AT_START_U_PROTECTION_LEVEL 1.55f

namespace eez {

using namespace gui;
using namespace psu;

namespace dcp405 {

bool isHwOvpEnabled(Channel &channel) {
	return channel.prot_conf.flags.u_state && channel.prot_conf.flags.u_type;
}

struct DcpChannel : public Channel {
	AnalogDigitalConverter adc;
	DigitalAnalogConverter dac;
	IOExpander ioexp;

	TestResult tempSensorTestResult = TEST_OK;

    bool delayed_dp_off;
	uint32_t delayed_dp_off_start;
	bool dpOn;
	uint32_t dpNegMonitoringTime;

    float uSet = 0;

	float uBeforeBalancing = NAN;
	float iBeforeBalancing = NAN;

	bool fallingEdge;
	uint32_t fallingEdgeTimeout;
	float fallingEdgePreviousUMonAdc;

	float U_CAL_POINTS[2];
	float I_CAL_POINTS[2];
	float I_LOW_RANGE_CAL_POINTS[2];

    DcpChannel(uint8_t slotIndex, uint8_t channelIndex, uint8_t subchannelIndex)
        : Channel(slotIndex, channelIndex, subchannelIndex)
    {
		ioexp.slotIndex = slotIndex;
		ioexp.channelIndex = channelIndex;
		
		adc.slotIndex = slotIndex;
		adc.channelIndex = channelIndex;
		
		dac.slotIndex = slotIndex;
		dac.channelIndex = channelIndex;

		channelHistory = new ChannelHistory(*this);
    }
	
	void getParams(uint16_t moduleRevision) override {
		params.U_MIN = 0.0f;
		params.U_DEF = 0.0f;
		params.U_MAX = 40.0f;
		params.U_CAL_NUM_POINTS = 2;
		params.U_CAL_POINTS = U_CAL_POINTS;

		params.U_MIN_STEP = 0.01f;
		params.U_DEF_STEP = 0.1f;
		params.U_MAX_STEP = 5.0f;

		U_CAL_POINTS[0] = 0.15f;
		U_CAL_POINTS[1] = 38.0f;
		params.U_CAL_NUM_POINTS = 2;
		params.U_CAL_POINTS = U_CAL_POINTS;
		params.U_CAL_I_SET = 0.1f;

		params.I_MIN = 0.0f;
		params.I_DEF = 0.0f;
		params.I_MAX = 5.0f;

		params.I_MON_MIN = -5.0f;
		
		params.I_MIN_STEP = 0.01f;
		params.I_DEF_STEP = 0.01f;
		params.I_MAX_STEP = 1.0f; 
		
		I_CAL_POINTS[0] = 0.05f;
		I_CAL_POINTS[1] = 4.8f;
		params.I_CAL_NUM_POINTS = 2;
		params.I_CAL_POINTS = I_CAL_POINTS;
		params.I_CAL_U_SET = 30.0f;

		I_LOW_RANGE_CAL_POINTS[0] = I_CAL_POINTS[0] / 100;
		I_LOW_RANGE_CAL_POINTS[1] = I_CAL_POINTS[1] / 100;
		params.I_LOW_RANGE_CAL_NUM_POINTS = 2;
		params.I_LOW_RANGE_CAL_POINTS = I_LOW_RANGE_CAL_POINTS;
		params.I_LOW_RANGE_CAL_U_SET = 30.0f;

		params.OVP_DEFAULT_STATE = false;
		params.OVP_MIN_DELAY = 0.0f;
		params.OVP_DEFAULT_DELAY = 0.0f;
		params.OVP_MAX_DELAY = 10.0f;

		params.OCP_DEFAULT_STATE = false;
		params.OCP_MIN_DELAY = 0.0f;
		params.OCP_DEFAULT_DELAY = 0.02f;
		params.OCP_MAX_DELAY = 10.0f;

		params.OPP_DEFAULT_STATE = false;
		params.OPP_MIN_DELAY = 1.0f;
		params.OPP_DEFAULT_DELAY = 10.0f;
		params.OPP_MAX_DELAY = 300.0f;
		params.OPP_MIN_LEVEL = 0.0f;
		params.OPP_DEFAULT_LEVEL = 155.0f;

		params.PTOT = 155.0f;

		params.U_RESOLUTION = 0.005f;
		params.U_RESOLUTION_DURING_CALIBRATION = 0.0001f;
		params.I_RESOLUTION = 0.0005f;
		params.I_RESOLUTION_DURING_CALIBRATION = 0.00001f;
		params.I_LOW_RESOLUTION = 0.000005f;
		params.I_LOW_RESOLUTION_DURING_CALIBRATION = 0.0000001f;
		params.P_RESOLUTION = 0.001f;

		params.VOLTAGE_GND_OFFSET = 0.86f;
		params.CURRENT_GND_OFFSET = 0.11f;

		params.CALIBRATION_DATA_TOLERANCE_PERCENT = 10.0f;

		params.CALIBRATION_MID_TOLERANCE_PERCENT = 1.0f;

		params.features = CH_FEATURE_VOLT | CH_FEATURE_CURRENT | CH_FEATURE_POWER | CH_FEATURE_OE | 
			CH_FEATURE_DPROG | CH_FEATURE_RPROG | CH_FEATURE_RPOL | 
			CH_FEATURE_CURRENT_DUAL_RANGE | CH_FEATURE_HW_OVP | CH_FEATURE_COUPLING;

		params.MON_REFRESH_RATE_MS = 250;

		params.DAC_MAX = DigitalAnalogConverter::DAC_MAX;
		params.ADC_MAX = AnalogDigitalConverter::ADC_MAX;

		params.U_RAMP_DURATION_MIN_VALUE = moduleRevision <= MODULE_REVISION_DCP405_R2B11 ? 0.004f : 0.002f;
	}

	void init() override {
        if (!g_slots[slotIndex]->enabled) {
            return;
        }

		ioexp.init();
		adc.init();
		dac.init();
	}

	void reset() override {
		Channel::reset();

		uSet = 0;
        dpOn = false;
		uBeforeBalancing = NAN;
		iBeforeBalancing = NAN;
	}

	bool test() override {
        if (!g_slots[slotIndex]->enabled) {
            return true;
        }

        flags.powerOk = 0;

        init();

        doRemoteSensingEnable(false);
        doRemoteProgrammingEnable(false);

		ioexp.test();
		adc.test();
		dac.test(ioexp, adc);

		tempSensorTestResult = TEST_OK;
		if (!temp_sensor::sensors[temp_sensor::CH1 + channelIndex].test()) {
			tempSensorTestResult = TEST_FAILED;
		}

		return isOk();
	}

	TestResult getTestResult() override {
        if (!g_slots[slotIndex]->enabled) {
            return TEST_SKIPPED;
        }
        
        if (ioexp.testResult == TEST_NONE || adc.testResult == TEST_NONE || dac.testResult == TEST_NONE || tempSensorTestResult == TEST_NONE) {
			return TEST_NONE;
		}

		if (ioexp.testResult == TEST_OK && adc.testResult == TEST_OK && dac.testResult == TEST_OK && tempSensorTestResult == TEST_OK) {
			return TEST_OK;
		}

		return TEST_FAILED;
	}

	AdcDataType getNextAdcDataType(AdcDataType adcDataType) {
		if (adcDataType == ADC_DATA_TYPE_U_MON) {
			return ADC_DATA_TYPE_I_MON;
		}

		if (adcDataType == ADC_DATA_TYPE_I_MON && isRemoteProgrammingEnabled()) {
			return ADC_DATA_TYPE_U_MON_DAC;
		}

		return ADC_DATA_TYPE_U_MON;
	}

	void tickSpecific(uint32_t tickCount) override {
		if (isDacTesting()) {
			return;
		}

		ioexp.tick(tickCount);

#if !CONF_SKIP_PWRGOOD_TEST
		if (!ioexp.testBit(IOExpander::IO_BIT_IN_PWRGOOD)) {
			channel_dispatcher::setVoltage(*this, 0);
			channel_dispatcher::outputEnable(*this, false);

			DebugTrace("Ch%d PWRGOOD bit changed to 0, gpio=%d\n", channelIndex + 1, (int)ioexp.gpio);
			flags.powerOk = 0;
			ioexp.testResult = TEST_FAILED;
			generateChannelError(SCPI_ERROR_CH1_FAULT_DETECTED, channelIndex);
			return;
		}
#endif

		if (isOutputEnabled() && ioexp.isAdcReady()) {
			auto adcDataType = adc.adcDataType;
			float value = adc.read();
			adc.start(getNextAdcDataType(adcDataType));
			onAdcData(adcDataType, value);
#ifdef DEBUG
			psu::debug::g_adcCounter.inc();
#endif
		}

		if (flags.dprogState == DPROG_STATE_ON) {
			// turn off DP after delay
			if (delayed_dp_off && (millis() - delayed_dp_off_start) >= DP_OFF_DELAY_PERIOD) {
				delayed_dp_off = false;
				setDpEnable(false);
			}
		}
		
		/// Output power is monitored and if its go below DP_NEG_LEV
		/// that is negative value in Watts (default -1 W),
		/// and that condition lasts more then DP_NEG_DELAY seconds (default 5 s),
		/// down-programmer circuit has to be switched off.
		if (isOutputEnabled()) {
			if (u.mon_last * i.mon_last >= DP_NEG_LEV || tickCount < dpNegMonitoringTime) {
				dpNegMonitoringTime = tickCount;
			} else {
				if (tickCount - dpNegMonitoringTime > DP_NEG_DELAY * 1000000UL) {
					if (dpOn) {
						// DebugTrace("CH%d, neg. P, DP off: %f", channelIndex + 1, u.mon_last * i.mon_last);
						dpNegMonitoringTime = tickCount;
						generateChannelError(SCPI_ERROR_CH1_DOWN_PROGRAMMER_SWITCHED_OFF, channelIndex);
						setDpEnable(false);
					} else {
						// DebugTrace("CH%d, neg. P, output off: %f", channelIndex + 1, u.mon_last * i.mon_last);
						generateChannelError(SCPI_ERROR_CH1_OUTPUT_FAULT_DETECTED, channelIndex);
						channel_dispatcher::outputEnable(*this, false);
					}
				} else if (tickCount - dpNegMonitoringTime > 500 * 1000UL) {
					if (dpOn && channelIndex < 2) {
						if (channel_dispatcher::getCouplingType() == channel_dispatcher::COUPLING_TYPE_SERIES) {
							psu::Channel &channel2 = psu::Channel::get(channelIndex == 0 ? 1 : 0);
							voltageBalancing(channel2);
							dpNegMonitoringTime = tickCount;
						} else if (channel_dispatcher::getCouplingType() == channel_dispatcher::COUPLING_TYPE_PARALLEL) {
							psu::Channel &channel2 = psu::Channel::get(channelIndex == 0 ? 1 : 0);
							currentBalancing(channel2);
							dpNegMonitoringTime = tickCount;
						}
					}
				}
			}
		}

        if (channelIndex < 2 && channel_dispatcher::getCouplingType() == channel_dispatcher::COUPLING_TYPE_SERIES) {
            unsigned cvMode = isInCvMode() ? 1 : 0;
            if (cvMode != flags.cvMode) {
                restoreVoltageToValueBeforeBalancing(psu::Channel::get(channelIndex == 0 ? 1 : 0));
            }
        }

        if (channelIndex < 2 && channel_dispatcher::getCouplingType() == channel_dispatcher::COUPLING_TYPE_PARALLEL) {
            unsigned ccMode = isInCcMode() ? 1 : 0;
		    if (ccMode != flags.ccMode) {
				restoreCurrentToValueBeforeBalancing(psu::Channel::get(channelIndex == 0 ? 1 : 0));
			}
		}

		if (fallingEdge) {
			fallingEdge =
				(int32_t(fallingEdgeTimeout - millis()) > 0) ||
				(fallingEdgePreviousUMonAdc > u.mon_adc) ||
				(u.mon_adc > uSet * (1.0f + CONF_FALLING_EDGE_OVP_PERCENTAGE / 100.0f));

			if (fallingEdge) {
				fallingEdgePreviousUMonAdc = u.mon_adc;
			}
		}

		// HW OVP handling
		if (ioexp.testBit(IOExpander::IO_BIT_OUT_OUTPUT_ENABLE)) {
			if (!fallingEdge && isHwOvpEnabled(*this) && !ioexp.testBit(IOExpander::IO_BIT_OUT_OVP_ENABLE)) {
				if (dac.isOverHwOvpThreshold()) {
					// activate HW OVP
					prot_conf.flags.u_hwOvpDeactivated = 0;
					ioexp.changeBit(IOExpander::IO_BIT_OUT_OVP_ENABLE, true);
				} else {
					prot_conf.flags.u_hwOvpDeactivated = 1;
				}
			} else if ((fallingEdge || !isHwOvpEnabled(*this)) && ioexp.testBit(IOExpander::IO_BIT_OUT_OVP_ENABLE)) {
				// deactivate HW OVP
				prot_conf.flags.u_hwOvpDeactivated = fallingEdge ? 0 : 1;
				ioexp.changeBit(IOExpander::IO_BIT_OUT_OVP_ENABLE, false);
			}
		}
	}

	unsigned getRPol() override {
		return !ioexp.testBit(IOExpander::IO_BIT_IN_RPOL);
	}

	bool isInCcMode() override {
		return ioexp.testBit(IOExpander::IO_BIT_IN_CC_ACTIVE);
	}

	bool isInCvMode() override {
		return ioexp.testBit(IOExpander::IO_BIT_IN_CV_ACTIVE);
	}

	void waitConversionEnd() {
#if defined(EEZ_PLATFORM_STM32)
        for (int i = 0; i < CONF_ADC_CONVERSION_MAX_TIME_MS; i++) {
            ioexp.tick(micros());
            if (ioexp.isAdcReady()) {
				break;
			}
			delay(1);
		}

        WATCHDOG_RESET();
#endif
    }

	void adcMeasureUMon() override {
		adc.start(ADC_DATA_TYPE_U_MON);
		waitConversionEnd();
		onAdcData(ADC_DATA_TYPE_U_MON, adc.read());

		if (isOutputEnabled()) {
			adc.start(ADC_DATA_TYPE_U_MON);
		}
	}

	void adcMeasureIMon() override {
		adc.start(ADC_DATA_TYPE_U_MON);
		waitConversionEnd();
		onAdcData(ADC_DATA_TYPE_U_MON, adc.read());

		if (isOutputEnabled()) {
			adc.start(ADC_DATA_TYPE_U_MON);
		}
	}

	void adcMeasureMonDac() override {
		if (isOk() || isDacTesting()) {
			adc.start(ADC_DATA_TYPE_U_MON_DAC);
			waitConversionEnd();
			onAdcData(ADC_DATA_TYPE_U_MON_DAC, adc.read());

			adc.start(ADC_DATA_TYPE_I_MON_DAC);
			waitConversionEnd();
			onAdcData(ADC_DATA_TYPE_I_MON_DAC, adc.read());

			if (isOutputEnabled()) {
				adc.start(ADC_DATA_TYPE_U_MON);
			}
		}
	}

	void adcMeasureAll() override {
		adc.start(ADC_DATA_TYPE_U_MON);
		waitConversionEnd();
		onAdcData(ADC_DATA_TYPE_U_MON, adc.read());

		adc.start(ADC_DATA_TYPE_I_MON);
		waitConversionEnd();
		onAdcData(ADC_DATA_TYPE_I_MON, adc.read());

		adc.start(ADC_DATA_TYPE_U_MON_DAC);
		waitConversionEnd();
		onAdcData(ADC_DATA_TYPE_U_MON_DAC, adc.read());

		adc.start(ADC_DATA_TYPE_I_MON_DAC);
		waitConversionEnd();
		onAdcData(ADC_DATA_TYPE_I_MON_DAC, adc.read());

		if (isOutputEnabled()) {
			adc.start(ADC_DATA_TYPE_U_MON);
		}
	}

	bool shouldDisableDP() {
		// disable DP if low current range and current of 10 mA or less is set
		if (flags.currentCurrentRange == 1 && i.set <= 10E-3f) {
			return true;
		}

		// in parallel coupling, only DP on channel 1 could be enabled
		if (channelIndex == 1 && channel_dispatcher::getCouplingType() == channel_dispatcher::COUPLING_TYPE_PARALLEL) {
			return true;
		}

		return false;
	}

	void setDpEnable(bool enable) {
		if (enable && shouldDisableDP()) {
			return;
		}

		// DP bit is active low
		ioexp.changeBit(IOExpander::IO_BIT_OUT_DP_ENABLE, !enable);

		setOperBits(OPER_ISUM_DP_OFF, !enable);
		dpOn = enable;

		if (enable) {
			dpNegMonitoringTime = micros();
		}
	}

	void setOutputEnable(bool enable, uint16_t tasks) override {
		if (enable) {
			// OE
			if (tasks & OUTPUT_ENABLE_TASK_OE) {
				ioexp.changeBit(IOExpander::IO_BIT_OUT_OUTPUT_ENABLE, true);
			}

			// DAC
			if (tasks & OUTPUT_ENABLE_TASK_DAC) {
				dac.setVoltage(uSet, DigitalAnalogConverter::WITH_RAMP);
			}

			// Current range
			if (tasks & OUTPUT_ENABLE_TASK_CURRENT_RANGE) {
				doSetCurrentRange();
			}

			// OVP
			if (tasks & OUTPUT_ENABLE_TASK_OVP) {
				if (isHwOvpEnabled(*this)) {
					if (dac.isOverHwOvpThreshold()) {
						// OVP has to be enabled after OE activation
						prot_conf.flags.u_hwOvpDeactivated = 0;
						ioexp.changeBit(IOExpander::IO_BIT_OUT_OVP_ENABLE, true);
					}
				}
			}

			// DP
			if (tasks & OUTPUT_ENABLE_TASK_DP) {
				if (flags.dprogState == DPROG_STATE_ON) {
					// enable DP
					dpNegMonitoringTime = micros();
					delayed_dp_off = false;
					setDpEnable(true);
				}
			}

			if (tasks & OUTPUT_ENABLE_TASK_ADC_START) {
				adc.start(ADC_DATA_TYPE_U_MON);
			}
		} else {
			// OVP
			if (tasks & OUTPUT_ENABLE_TASK_OVP) {
				if (isHwOvpEnabled(*this)) {
					// OVP has to be disabled before OE deactivation
					prot_conf.flags.u_hwOvpDeactivated = 1;
					ioexp.changeBit(IOExpander::IO_BIT_OUT_OVP_ENABLE, false);
				}
			}

			// DAC
			if (tasks & OUTPUT_ENABLE_TASK_DAC) {
				dac.setDacVoltage(0);
			}

			// OE
			if (tasks & OUTPUT_ENABLE_TASK_OE) {
				ioexp.changeBit(IOExpander::IO_BIT_OUT_OUTPUT_ENABLE, false);

				u.resetMonValues();
				i.resetMonValues();
			}

			// Current range
			if (tasks & OUTPUT_ENABLE_TASK_CURRENT_RANGE) {
				doSetCurrentRange();
			}

			// DP
			if (tasks & OUTPUT_ENABLE_TASK_DP) {
				if (flags.dprogState == DPROG_STATE_ON) {
					// turn off DP after some delay
					delayed_dp_off = true;
					delayed_dp_off_start = millis();
				}
			}
		}

		if (tasks & OUTPUT_ENABLE_TASK_FINALIZE) {
			if (channelIndex == 0 && channel_dispatcher::getCouplingType() == channel_dispatcher::COUPLING_TYPE_PARALLEL) {
				ioexp.changeBit(IOExpander::IO_BIT_OUT_OE_UNCOUPLED_LED, false);
				ioexp.changeBit(IOExpander::IO_BIT_OUT_OE_COUPLED_LED, enable);
			} else if (channelIndex == 1 && channel_dispatcher::getCouplingType() == channel_dispatcher::COUPLING_TYPE_PARALLEL) {
				ioexp.changeBit(IOExpander::IO_BIT_OUT_OE_UNCOUPLED_LED, false);
				ioexp.changeBit(IOExpander::IO_BIT_OUT_OE_COUPLED_LED, false);
			} else if (channelIndex == 0 && channel_dispatcher::getCouplingType() == channel_dispatcher::COUPLING_TYPE_SERIES) {
				ioexp.changeBit(IOExpander::IO_BIT_OUT_OE_UNCOUPLED_LED, false);
				ioexp.changeBit(IOExpander::IO_BIT_OUT_OE_COUPLED_LED, enable);
			} else if (channelIndex == 1 && channel_dispatcher::getCouplingType() == channel_dispatcher::COUPLING_TYPE_SERIES) {
				ioexp.changeBit(IOExpander::IO_BIT_OUT_OE_UNCOUPLED_LED, false);
				ioexp.changeBit(IOExpander::IO_BIT_OUT_OE_COUPLED_LED, false);
			} else if (channelIndex < 2 && channel_dispatcher::getCouplingType() == channel_dispatcher::COUPLING_TYPE_SPLIT_RAILS) {
				ioexp.changeBit(IOExpander::IO_BIT_OUT_OE_UNCOUPLED_LED, false);
				ioexp.changeBit(IOExpander::IO_BIT_OUT_OE_COUPLED_LED, enable);
			} else if (channel_dispatcher::getCouplingType() == channel_dispatcher::COUPLING_TYPE_COMMON_GND) {
				ioexp.changeBit(IOExpander::IO_BIT_OUT_OE_UNCOUPLED_LED, false);
				ioexp.changeBit(IOExpander::IO_BIT_OUT_OE_COUPLED_LED, enable);
			} else {
				ioexp.changeBit(IOExpander::IO_BIT_OUT_OE_UNCOUPLED_LED, enable);
				ioexp.changeBit(IOExpander::IO_BIT_OUT_OE_COUPLED_LED, false);
			}

			restoreVoltageToValueBeforeBalancing(*this);
			restoreCurrentToValueBeforeBalancing(*this);
		}
	}

    void setDprogState(DprogState dprogState) override {
		if (!isPsuThread()) {
			sendMessageToPsu(PSU_MESSAGE_SET_DPROG_STATE, (channelIndex << 8) | dprogState);
		} else {
			Channel::setDprogState(dprogState);

			if (dprogState == DPROG_STATE_OFF) {
				setDpEnable(false);
			} else {
				setDpEnable(isOk() && ioexp.testBit(IOExpander::IO_BIT_OUT_OUTPUT_ENABLE));
			}
			delayed_dp_off = false;
		}
    }
    
    void setRemoteSense(bool enable) override {
		ioexp.changeBit(IOExpander::IO_BIT_OUT_REMOTE_SENSE, enable);
	}

	void setRemoteProgramming(bool enable) override {
		ioexp.changeBit(IOExpander::IO_BIT_OUT_REMOTE_PROGRAMMING, enable);
	}

	void setDacVoltage(uint16_t value) override {
		dac.setDacVoltage(value);

        uBeforeBalancing = NAN;
        restoreCurrentToValueBeforeBalancing(*this);
    }

	void setDacVoltageFloat(float value) override {
		float previousUSet = uSet;

		uSet = value;

		if (isOutputEnabled()) {
			bool belowThreshold = !dac.isOverHwOvpThreshold();

			if (value < previousUSet) {
				fallingEdge = true;
				fallingEdgePreviousUMonAdc = u.mon_adc;
				fallingEdgeTimeout = millis() + (belowThreshold ? CONF_FALLING_EDGE_SW_OVP_DELAY_MS : CONF_FALLING_EDGE_HW_OVP_DELAY_MS);
				if (isHwOvpEnabled(*this)) {
					// deactivate HW OVP
					prot_conf.flags.u_hwOvpDeactivated = 0; // this flag should be 0 while fallingEdge is true
					ioexp.changeBit(IOExpander::IO_BIT_OUT_OVP_ENABLE, false);
				}
			} else if (belowThreshold) {
				if (isHwOvpEnabled(*this)) {
					// deactivate HW OVP
					prot_conf.flags.u_hwOvpDeactivated = 1;
					ioexp.changeBit(IOExpander::IO_BIT_OUT_OVP_ENABLE, false);
				}
			}
		}

        if (ioexp.testBit(IOExpander::IO_BIT_OUT_OUTPUT_ENABLE) || isDacTesting()) {
			dac.setVoltage(value);
		} else {
			dac.setDacVoltage(0);
		}

		uBeforeBalancing = NAN;
		restoreCurrentToValueBeforeBalancing(*this);
	}

	void setDacCurrent(uint16_t value) override {
		dac.setDacCurrent(value);

		iBeforeBalancing = NAN;
		restoreVoltageToValueBeforeBalancing(*this);
	}

	void setDacCurrentFloat(float value) override {
		if (isOk() && ioexp.testBit(IOExpander::IO_BIT_OUT_OUTPUT_ENABLE) && flags.dprogState == DPROG_STATE_ON) {
			if (dpOn) {
				if (shouldDisableDP()) {
					setDpEnable(false);
				}
			} else {
				if (!shouldDisableDP()) {
					setDpEnable(true);
				}
			}
		}

		dac.setCurrent(value);
		iBeforeBalancing = NAN;
		restoreVoltageToValueBeforeBalancing(*this);
	}

	bool isDacTesting() override {
		return dac.isTesting();
	}

	void doSetCurrentRange() override {
		auto &slot = *g_slots[slotIndex];

		if (!hasSupportForCurrentDualRange()) {
			return;
		}

		if (slot.moduleRevision == MODULE_REVISION_DCP405_R1B1) {
			ioexp.changeBit(IOExpander::IO_BIT_OUT_CURRENT_RANGE_500MA, false);
		}

		if (isOutputEnabled()) {
			if (flags.currentCurrentRange == 0 || dac.isTesting()) {
				// 5A
				// DebugTrace("CH%d: Switched to 5A range", channelIndex + 1);
				ioexp.changeBit(IOExpander::IO_BIT_OUT_CURRENT_RANGE_5A, true);
				ioexp.changeBit(slot.moduleRevision == MODULE_REVISION_DCP405_R1B1 ?
					IOExpander::IO_BIT_OUT_CURRENT_RANGE_50MA :
					IOExpander::R2B5_IO_BIT_OUT_CURRENT_RANGE_50MA, false);
				// calculateNegligibleAdcDiffForCurrent();
			} else {
				// 50mA
				// DebugTrace("CH%d: Switched to 50mA range", channelIndex + 1);
				ioexp.changeBit(slot.moduleRevision == MODULE_REVISION_DCP405_R1B1 ?
					IOExpander::IO_BIT_OUT_CURRENT_RANGE_50MA :
					IOExpander::R2B5_IO_BIT_OUT_CURRENT_RANGE_50MA, true);
				ioexp.changeBit(IOExpander::IO_BIT_OUT_CURRENT_RANGE_5A, false);
				// calculateNegligibleAdcDiffForCurrent();
			}
		} else {
			ioexp.changeBit(IOExpander::IO_BIT_OUT_CURRENT_RANGE_5A, true);
			ioexp.changeBit(slot.moduleRevision == MODULE_REVISION_DCP405_R1B1 ?
				IOExpander::IO_BIT_OUT_CURRENT_RANGE_50MA :
				IOExpander::R2B5_IO_BIT_OUT_CURRENT_RANGE_50MA, false);
		}
	}

    bool isVoltageBalanced() override {
        return !isNaN(uBeforeBalancing);
    }

    bool isCurrentBalanced() override {
        return !isNaN(iBeforeBalancing);
    }

    float getUSetUnbalanced() override {
        return isVoltageBalanced() ? uBeforeBalancing : u.set;
    }

    float getISetUnbalanced() override {
        return isCurrentBalanced() ? iBeforeBalancing : i.set;
    }

	void readAllRegisters(uint8_t ioexpRegisters[], uint8_t adcRegisters[]) override {
		ioexp.readAllRegisters(ioexpRegisters);
		adc.readAllRegisters(adcRegisters);
	}

#if defined(DEBUG) && defined(EEZ_PLATFORM_STM32)
	int getIoExpBitDirection(int io_bit) {
		return ioexp.getBitDirection(io_bit);
	}

	bool testIoExpBit(int io_bit) {
		return ioexp.testBit(io_bit);
	}

	void changeIoExpBit(int io_bit, bool set) {
		ioexp.changeBit(io_bit, set);
	}
#endif

    void voltageBalancing(psu::Channel &channel) {
        // DebugTrace("Channel voltage balancing: CH1_Umon=%f, CH2_Umon=%f",
        // Channel::get(0).u.mon_last, Channel::get(1).u.mon_last);
        if (isNaN(uBeforeBalancing)) {
            uBeforeBalancing = channel.u.set;
        }
        channel.doSetVoltage((psu::Channel::get(0).u.mon_last + psu::Channel::get(1).u.mon_last) / 2);
    }

    void currentBalancing(psu::Channel &channel) {
        // DebugTrace("CH%d channel current balancing: CH1_Imon=%f, CH2_Imon=%f", index,
        // Channel::get(0).i.mon_last, Channel::get(1).i.mon_last);
        if (isNaN(iBeforeBalancing)) {
            iBeforeBalancing = channel.i.set;
        }
        channel.doSetCurrent((psu::Channel::get(0).i.mon_last + psu::Channel::get(1).i.mon_last) / 2);
    }

    void restoreVoltageToValueBeforeBalancing(psu::Channel &channel) {
        if (!isNaN(uBeforeBalancing)) {
            // DebugTrace("Restore voltage to value before balancing: %f", uBeforeBalancing);
            channel.setVoltage(uBeforeBalancing);
            uBeforeBalancing = NAN;
        }
    }

    void restoreCurrentToValueBeforeBalancing(psu::Channel &channel) {
        if (!isNaN(iBeforeBalancing)) {
            // DebugTrace("Restore current to value before balancing: %f", index, iBeforeBalancing);
            channel.setCurrent(iBeforeBalancing);
            iBeforeBalancing = NAN;
        }
    }

#if defined(EEZ_PLATFORM_STM32)
	void onSpiIrq() {
		uint8_t intcap = ioexp.readIntcapRegister();
		// DebugTrace("CH%d INTCAP 0x%02X\n", (int)(channelIndex + 1), (int)intcap);
		if (!(intcap & (1 << IOExpander::R2B5_IO_BIT_IN_OVP_FAULT))) {
			if (isOutputEnabled() && isHwOvpEnabled(*this)) {
				protectionEnter(ovp, true);
			}
		}
	}
#endif

    void getVoltageStepValues(StepValues *stepValues, bool calibrationMode) override {
        static float values[] = { 1.0f, 0.1f, 0.01f, 0.005f };
		static float calibrationModeValues[] = { 1.0f, 0.1f, 0.01f, 0.001f };
        stepValues->values = calibrationMode ? calibrationModeValues : values;
        stepValues->count = sizeof(values) / sizeof(float);
		stepValues->unit = UNIT_VOLT;
	}
    
	void getCurrentStepValues(StepValues *stepValues, bool calibrationMode) override {
        static float lowRangeValues[] = { 0.001f, 0.0001f, 0.00001f, 0.000005f };
		static float calibrationModeLowRangeValues[] = { 0.001f, 0.0001f, 0.00001f, 0.000001f };
        static float highRangeValues[] = { 0.1f, 0.01f, 0.001f, 0.0005f }; 
		static float calibrationModeHighRangeValues[] = { 0.1f, 0.01f, 0.001f, 0.0001f }; 
		if (flags.currentRangeSelectionMode == CURRENT_RANGE_SELECTION_ALWAYS_LOW) {
        	stepValues->values = calibrationMode ? calibrationModeLowRangeValues : lowRangeValues;
        	stepValues->count = sizeof(lowRangeValues) / sizeof(float);
		} else {
        	stepValues->values = calibrationMode ? calibrationModeHighRangeValues : highRangeValues;
			stepValues->count = sizeof(highRangeValues) / sizeof(float);
		}
		stepValues->unit = UNIT_AMPER;
	}

    void getPowerStepValues(StepValues *stepValues) override {
        static float values[] = { 10.0f, 1.0f, 0.1f, 0.01f };
        stepValues->values = values;
        stepValues->count = sizeof(values) / sizeof(float);
		stepValues->unit = UNIT_WATT;
	}	

	bool isPowerLimitExceeded(float u, float i, int *err) override {
		if (u * i > channel_dispatcher::getPowerLimit(*this)) {
			if (err) {
				*err = SCPI_ERROR_POWER_LIMIT_EXCEEDED;
			}
			return true;
		}
		return false;
	}

	float readTemperature() override;

	int getAdvancedOptionsPageId() override {
		return gui::PAGE_ID_CH_SETTINGS_ADV_OPTIONS;
	}
};

struct DcpModule : public PsuModule {
public:
	float temperature = 0;

    DcpModule() {
        moduleType = MODULE_TYPE_DCP405;
        moduleName = "DCP405";
        moduleBrand = "Envox";
        latestModuleRevision = MODULE_REVISION_DCP405_R2B11;
        flashMethod = FLASH_METHOD_NONE;
        flashDuration = 0;
        spiBaudRatePrescaler = 0;
        spiCrcCalculationEnable = false;
        numPowerChannels = 1;
        numOtherChannels = 0;		
    }

	void boot() override {
		PsuModule::boot();

		persist_conf::loadSerialNo(slotIndex);
	}

	Module *createModule() override {
		return new DcpModule();
	}

	Channel *createPowerChannel(int slotIndex, int channelIndex, int subchannelIndex) override {
        void *buffer = malloc(sizeof(DcpChannel));
        memset(buffer, 0, sizeof(DcpChannel));
		return new (buffer) DcpChannel(slotIndex, channelIndex, subchannelIndex);
	}

#if defined(EEZ_PLATFORM_STM32)
	void onSpiIrq() {
		auto dcpChannel = (DcpChannel *)Channel::getBySlotIndex(slotIndex);
		dcpChannel->onSpiIrq();
	}
#endif

	int getSlotView(SlotViewType slotViewType, int slotIndex, int cursor) {
		int isVert = persist_conf::devConf.channelsViewMode == CHANNELS_VIEW_MODE_NUMERIC || persist_conf::devConf.channelsViewMode == CHANNELS_VIEW_MODE_VERT_BAR;
		Channel &channel = Channel::get(cursor);

		if (slotViewType == SLOT_VIEW_TYPE_DEFAULT) {
			if (channel_dispatcher::getCouplingType() == channel_dispatcher::COUPLING_TYPE_SERIES && channel.channelIndex == 1) {
				if (persist_conf::devConf.channelsViewMode == CHANNELS_VIEW_MODE_NUMERIC || persist_conf::devConf.channelsViewMode == CHANNELS_VIEW_MODE_VERT_BAR) {
					return PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_VERT_COUPLED_SERIES;
				} else {
					return PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_HORZ_COUPLED_SERIES;
				}
			} else if (channel_dispatcher::getCouplingType() == channel_dispatcher::COUPLING_TYPE_PARALLEL && channel.channelIndex == 1) {
				if (persist_conf::devConf.channelsViewMode == CHANNELS_VIEW_MODE_NUMERIC || persist_conf::devConf.channelsViewMode == CHANNELS_VIEW_MODE_VERT_BAR) {
					return PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_VERT_COUPLED_PARALLEL;
				} else {
					return PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_HORZ_COUPLED_PARALLEL;
				}
			} else if (persist_conf::devConf.channelsViewMode == CHANNELS_VIEW_MODE_NUMERIC) {
				return channel.isOutputEnabled() ? PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_NUM_ON : PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_VERT_OFF;
			} else if (persist_conf::devConf.channelsViewMode == CHANNELS_VIEW_MODE_VERT_BAR) {
				return channel.isOutputEnabled() ? PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_VBAR_ON : PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_VERT_OFF;
			} else if (persist_conf::devConf.channelsViewMode == CHANNELS_VIEW_MODE_HORZ_BAR) {
				return channel.isOutputEnabled() ? PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_HBAR_ON : PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_HORZ_OFF;
			} else if (persist_conf::devConf.channelsViewMode == CHANNELS_VIEW_MODE_YT) {
				return channel.isOutputEnabled() ? PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_YT_ON : PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_HORZ_OFF;
			} else {
				return isVert ? PAGE_ID_SLOT_DEF_VERT_ERROR : PAGE_ID_SLOT_DEF_HORZ_ERROR;
			}
		}

		if (slotViewType == SLOT_VIEW_TYPE_DEFAULT_2COL) {
			if (channel_dispatcher::getCouplingType() == channel_dispatcher::COUPLING_TYPE_SERIES && channel.channelIndex == 1) {
				if (persist_conf::devConf.channelsViewMode == CHANNELS_VIEW_MODE_NUMERIC || persist_conf::devConf.channelsViewMode == CHANNELS_VIEW_MODE_VERT_BAR) {
					return PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_VERT_COUPLED_SERIES_2COL;
				} else {
					return PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_HORZ_COUPLED_SERIES_2COL;
				}
			} else if (channel_dispatcher::getCouplingType() == channel_dispatcher::COUPLING_TYPE_PARALLEL && channel.channelIndex == 1) {
				if (persist_conf::devConf.channelsViewMode == CHANNELS_VIEW_MODE_NUMERIC || persist_conf::devConf.channelsViewMode == CHANNELS_VIEW_MODE_VERT_BAR) {
					return PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_VERT_COUPLED_PARALLEL_2COL;
				} else {
					return PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_HORZ_COUPLED_PARALLEL_2COL;
				}
			} else if (persist_conf::devConf.channelsViewMode == CHANNELS_VIEW_MODE_NUMERIC) {
				return channel.isOutputEnabled() ? PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_NUM_ON_2COL : PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_VERT_OFF_2COL;
			} else if (persist_conf::devConf.channelsViewMode == CHANNELS_VIEW_MODE_VERT_BAR) {
				return channel.isOutputEnabled() ? PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_VBAR_ON_2COL : PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_VERT_OFF_2COL;
			} else if (persist_conf::devConf.channelsViewMode == CHANNELS_VIEW_MODE_HORZ_BAR) {
				return channel.isOutputEnabled() ? PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_HBAR_ON_2COL : PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_HORZ_OFF_2COL;
			} else if (persist_conf::devConf.channelsViewMode == CHANNELS_VIEW_MODE_YT) {
				return channel.isOutputEnabled() ? PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_YT_ON_2COL : PAGE_ID_DIB_DCP405_SLOT_DEF_1CH_HORZ_OFF_2COL;
			} else {
				return isVert ? PAGE_ID_SLOT_DEF_VERT_ERROR_2COL : PAGE_ID_SLOT_DEF_HORZ_ERROR_2COL;
			}
		}

		if (slotViewType == SLOT_VIEW_TYPE_MAX) {
			if (persist_conf::devConf.channelsViewModeInMax == CHANNELS_VIEW_MODE_IN_MAX_NUMERIC) {
				return channel.isOutputEnabled() ? PAGE_ID_DIB_DCP405_SLOT_MAX_1CH_NUM_ON : PAGE_ID_DIB_DCP405_SLOT_MAX_1CH_NUM_OFF;
			} else if (persist_conf::devConf.channelsViewModeInMax == CHANNELS_VIEW_MODE_IN_MAX_HORZ_BAR) {
				return channel.isOutputEnabled() ? PAGE_ID_DIB_DCP405_SLOT_MAX_1CH_HBAR_ON : PAGE_ID_DIB_DCP405_SLOT_MAX_1CH_HBAR_OFF;
			} else if (persist_conf::devConf.channelsViewModeInMax == CHANNELS_VIEW_MODE_IN_MAX_YT) {
				return channel.isOutputEnabled() ? PAGE_ID_DIB_DCP405_SLOT_MAX_1CH_YT_ON : PAGE_ID_DIB_DCP405_SLOT_MAX_1CH_YT_OFF;
			} else {
				return PAGE_ID_SLOT_MAX_ERROR;
			}
		}

		if (slotViewType == SLOT_VIEW_TYPE_MIN) {
			if (channel_dispatcher::getCouplingType() == channel_dispatcher::COUPLING_TYPE_SERIES && channel.channelIndex == 1) {
				return PAGE_ID_DIB_DCP405_SLOT_MIN_1CH_COUPLED_SERIES;
			} else if (channel_dispatcher::getCouplingType() == channel_dispatcher::COUPLING_TYPE_PARALLEL && channel.channelIndex == 1) {
				return PAGE_ID_DIB_DCP405_SLOT_MIN_1CH_COUPLED_PARALLEL;
			} else {
				return channel.isOutputEnabled() ? PAGE_ID_DIB_DCP405_SLOT_MIN_1CH_ON : PAGE_ID_DIB_DCP405_SLOT_MIN_1CH_OFF;
			}
		}

		assert(slotViewType == SLOT_VIEW_TYPE_MICRO);
		if (channel_dispatcher::getCouplingType() == channel_dispatcher::COUPLING_TYPE_SERIES && channel.channelIndex == 1) {
			return PAGE_ID_DIB_DCP405_SLOT_MICRO_1CH_COUPLED_SERIES;
		} else if (channel_dispatcher::getCouplingType() == channel_dispatcher::COUPLING_TYPE_PARALLEL && channel.channelIndex == 1) {
			return PAGE_ID_DIB_DCP405_SLOT_MICRO_1CH_COUPLED_PARALLEL;
		} else {
			return channel.isOutputEnabled() ? PAGE_ID_DIB_DCP405_SLOT_MICRO_1CH_ON : PAGE_ID_DIB_DCP405_SLOT_MICRO_1CH_OFF;
		}
	}
};

static DcpModule g_dcpModule;
Module *g_module = &g_dcpModule;

float DcpChannel::readTemperature() {
#if defined(EEZ_PLATFORM_STM32)
	DcpModule *module = (DcpModule *)g_slots[slotIndex];

	if (!isPsuThread()) {
		sendMessageToPsu(PSU_MESSAGE_READ_CHANNEL_TEMPERATURE, channelIndex);
	} else {
		if (module->moduleRevision >= MODULE_REVISION_DCP405_R1B1) {
			module->temperature = drivers::tc77::readTemperature(slotIndex);
		} else {
			module->temperature = drivers::tmp1075::readTemperature(slotIndex);
		}
	}

	return module->temperature;
#endif
	return NAN;
}

bool isDacRampActive() {
	for (int i = 0; i < CH_NUM; i++) {
		auto &channel = Channel::get(i);
		if (g_slots[channel.slotIndex]->moduleType == MODULE_TYPE_DCP405) {
			if (((DcpChannel&)channel).dac.m_isRampActive) {
				return true;
			}
		}
	}
	return false;
}

void tickDacRamp(uint32_t tickCount) {
	for (int i = 0; i < CH_NUM; i++) {
		auto &channel = Channel::get(i);
		if (g_slots[channel.slotIndex]->moduleType == MODULE_TYPE_DCP405) {
			if (((DcpChannel&)channel).dac.m_isRampActive) {
				((DcpChannel&)channel).dac.tick(tickCount);
			}
		}
	}
}

} // namespace dcp405
} // namespace eez
