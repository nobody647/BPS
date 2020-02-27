#ifndef PIN_DEFS_H
#define PIN_DEFS_H

#include <stdint.h>
#include <stddef.h>


// baud rate used for can
const uint16_t CAN_BAUD = 125000;

// bitmask. just a bunch of ones
const uint16_t CAN_MASK = 0x07FF;

const uint16_t BMS_CAN_BASE =  0x600;   

const uint8_t PACK_STATUS =  0x0B0;  // Battery pack high voltage status:

const uint8_t HILO_VOLTS = 0x0B1;    // Voltage measurement status of all individual modules

const uint8_t TEMP_DTC =  0x0B2;     // Temperature status and Diagnostic Troubleshoot Codes of BMS:

const uint16_t BPS_CAN_BASE =  0x700;

const uint8_t VEHICLE_FAULT = 0x00F; // Sends a message to alert that there is a battery pack or Power Distribution fault on either High Voltage or DC-DC


// pins
const uint8_t PRECHARGE_SWITCH = 11;
const uint8_t PRECHARGE_LED = 52;
const uint8_t FAN_CONTROL_PIN = 50;
const uint8_t RELAY1_LOW_SIDE = 48;  
const uint8_t RELAY2_HIGH_SIDE = 46;
const uint8_t RELAY3_PRECHARGE = 44;
const uint8_t RELAY4_MPPT = 42;

// measurements
const size_t DATA_POINTS_TO_STORE = 5;
const size_t MAX_ACCEPTABLE_BAD_VALUES = 1;
// Absolute maximum ratings. TODO: FIll these out
const uint16_t CURRENT_LIMIT = 0;
const uint16_t HIGH_VOLTAGE_LIMIT = 0;
const uint16_t LOW_VOLTAGE_LIMIT = 0;
const uint16_t TEMP_LIMIT = 0;
const uint32_t AGE_LIMIT = 0;  // no data can be older than this (in ms)

// data from pack_status frame
struct pack_t {
	volatile uint16_t current;
	volatile uint16_t voltage;
	volatile uint8_t soc;
	volatile uint16_t relay;
	volatile uint32_t timestamp;
};

struct voltage_t {
	volatile uint16_t voltage;
	volatile uint8_t module_id;
	volatile uint32_t timestamp;
};

struct temp_t {
	volatile uint8_t temp;
	volatile uint8_t therm_id;
    volatile uint32_t timestamp;
};

#endif