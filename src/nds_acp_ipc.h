#pragma once
#include <stdint.h>

// Shared ARM9<->ARM7 ACP offload protocol over PXI user channel.
// Packet uses PXI immediate payload (26-bit).

enum NdsAcpPxiConstants {
    NDS_ACP_PXI_CHANNEL = 23, // PxiChannel_User0
};

enum NdsAcpPxiCommand {
    NDS_ACP_CMD_RAM_WRITE = 0, // arg: 12-bit ACP RAM address, value: byte
    NDS_ACP_CMD_REG_WRITE = 1, // arg: ACP register index (address & 7), value: byte
    NDS_ACP_CMD_CONTROL   = 2, // arg: control opcode, value: byte
};

enum NdsAcpControlOpcode {
    NDS_ACP_CTRL_VOLUME = 1, // value: 0..255
    NDS_ACP_CTRL_MUTE   = 2, // value: 0/1
    NDS_ACP_CTRL_PAUSE  = 3, // value: 0/1
};

// [25:20] command, [19:8] arg, [7:0] value
static inline uint32_t ndsAcpPackMsg(uint32_t cmd, uint32_t arg, uint32_t value) {
    return ((cmd & 0x3F) << 20) | ((arg & 0xFFF) << 8) | (value & 0xFF);
}

static inline uint32_t ndsAcpMsgCmd(uint32_t msg) {
    return (msg >> 20) & 0x3F;
}

static inline uint32_t ndsAcpMsgArg(uint32_t msg) {
    return (msg >> 8) & 0xFFF;
}

static inline uint32_t ndsAcpMsgValue(uint32_t msg) {
    return msg & 0xFF;
}

