#include "crc16.h"
#include <stdint.h>
#include <stddef.h>
uint16_t crc16_modbus(const uint8_t *data, size_t len){
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc; 
}