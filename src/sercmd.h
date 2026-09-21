// Copyright (C) 2024-2026 StevenCellist (https://github.com/StevenCellist)
// Licensed under GPL-3.0

/*
 * sercmd.h - line-oriented command console on the USB serial port.
 *
 * Exists so a SensorBox can be provisioned, configured and inspected over the
 * same cable that flashes and monitors it - no WiFi, no BLE, no phone app, and
 * no working network needed. A browser page speaking WebSerial is enough.
 *
 * TWO CONSOLES, ONE PORT
 * This module owns the serial reader and feeds two command languages:
 *
 *   '+something'   the legacy interactive console. Echoed as you type, with
 *                  backspace, and answered by execCommand() exactly as before.
 *                  Unchanged, unauthenticated, and meant for a human at a
 *                  terminal.
 *
 *   anything else   the machine protocol described in serial-provisioning.md:
 *                  plain-text command lines, every reply line prefixed with
 *                  '@', exactly one terminator per command.
 *
 * Which one a line belongs to is decided by its first character, so the two
 * never collide. Protocol lines are not echoed - that is what keeps an
 * 'AUTH <password>' out of the terminal scrollback and out of the monitor log.
 *
 * WHERE IT RUNS
 * From loop(), not from a task of its own. Everything a command touches - the
 * config, WiFi, the LoRaWAN node, the device state machine - already belongs
 * to the main loop, and reaching into it from a second task would need locks
 * that nothing else in this firmware has. The cost is that a command is
 * answered only as fast as the loop turns: during an uplink (SENDRECEIVE) a
 * reply can be several seconds late, which is why commands that would disturb
 * one answer EBUSY instead.
 *
 * SECURITY
 * Every protocol command apart from PING, HELP, AUTH and LOGOUT needs an
 * authenticated session, established by AUTH against the device password -
 * the same one the web dashboard uses (its default is the eight-character
 * device code, replaced by whatever the Security page sets). Wrong guesses
 * cost an escalating delay and sessions expire on idle.
 *
 * That gate is worth having against a port reached without seeing the device -
 * one bridged onto a network, a shared hub, a cable left plugged in. It is not
 * a boundary against someone standing at the SensorBox: '+id' prints the
 * default password, the legacy console is open, and this is the port that
 * flashes the chip. Do not route this interface over anything that is not
 * itself physically trusted.
 */

#ifndef _SERCMD_H
#define _SERCMD_H

#include <Arduino.h>

// Console log verbosity, as set by the LOG command. Only output routed through
// the PRINTF macro in config.h obeys it; plenty of the firmware still writes to
// Serial directly, which is why the protocol makes a client ignore every line
// that does not start with '@' rather than promising a silent port.
enum SerialLogLevel {
  SERLOG_NONE = 0,
  SERLOG_ERROR = 1,
  SERLOG_WARN = 2,
  SERLOG_INFO = 3,
  SERLOG_DEBUG = 4,
  SERLOG_VERBOSE = 5
};

extern uint8_t serialLogLevel;

// Announce the console (emits '@EVT READY'). Call once, after Serial is up.
void sercmdBegin();

// Drain whatever has arrived and run any complete line. Call from loop().
void sercmdPoll();

// ============= Device hooks =============
// Implemented in main.cpp, which owns the state machine these reach into.

// An uplink is in flight; commands that would disturb it answer EBUSY.
bool sercmdDeviceBusy();

// Bring an uplink forward, and rejoin the network from scratch.
void sercmdRequestUplink();
void sercmdRequestJoin();

// Power the unit down (the same thing the legacy '+sleep' does).
void sercmdRequestSleep();

// Drop the stored join nonces and session, forcing a fresh OTAA join.
void sercmdWipeLoRaWAN();

// Current LoRaWAN session, for the LORA command.
uint32_t sercmdDevAddr();
bool sercmdActivated();

// Bring WiFi and the dashboard up / down, as the Connections menu does.
void sercmdWifiUp();
void sercmdWifiDown();

// Battery rail, as the loop last sampled it. Reported by STATUS, because the
// first thing anyone wonders about a box that will not stay up is its battery.
uint16_t sercmdBatteryMillivolts();

#endif
