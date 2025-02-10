/**
 * @file w5100s_driver.c
 * @brief WIZnet W5100S Ethernet controller
 *
 * @section License
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2010-2025 Oryx Embedded SARL. All rights reserved.
 *
 * This file is part of CycloneTCP Open.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * @author Oryx Embedded SARL (www.oryx-embedded.com)
 * @version 2.5.0
 **/

//Switch to the appropriate trace level
#define TRACE_LEVEL NIC_TRACE_LEVEL

//Dependencies
#include "core/net.h"
#include "drivers/eth/w5100s_driver.h"
#include "debug.h"


/**
 * @brief W5100S driver
 **/

const NicDriver w5100sDriver =
{
   NIC_TYPE_ETHERNET,
   ETH_MTU,
   w5100sInit,
   w5100sTick,
   w5100sEnableIrq,
   w5100sDisableIrq,
   w5100sEventHandler,
   w5100sSendPacket,
   w5100sUpdateMacAddrFilter,
   NULL,
   NULL,
   NULL,
   TRUE,
   TRUE,
   TRUE,
   TRUE
};


/**
 * @brief W5100S controller initialization
 * @param[in] interface Underlying network interface
 * @return Error code
 **/

error_t w5100sInit(NetInterface *interface)
{
   uint_t i;
   uint8_t value;

   //Debug message
   TRACE_INFO("Initializing W5100S Ethernet controller...\r\n");

   //Initialize SPI interface
   interface->spiDriver->init();

   //Initialize external interrupt line driver
   if(interface->extIntDriver != NULL)
   {
      interface->extIntDriver->init();
   }

   //Wait for the SPI interface to be ready
   do
   {
      //Read chip version register
      value = w5100sReadReg8(interface, W5100S_VERR);

      //Check chip version
   } while(value != W5100S_VERR_DEFAULT);

   //Perform software reset
   w5100sWriteReg8(interface, W5100S_MR, W5100S_MR_RST);

   //Wait for reset completion
   do
   {
      //Read mode register
      value = w5100sReadReg8(interface, W5100S_MR);

      //The RST bit is automatically cleared after reset completion
   } while((value & W5100S_MR_RST) != 0);

   //Unlock access to network configuration registers
   w5100sWriteReg8(interface, W5100S_NETLCKR, W5100S_NETLCKR_UNLOCK);

   //Set the MAC address of the station
   w5100sWriteReg8(interface, W5100S_SHAR0, interface->macAddr.b[0]);
   w5100sWriteReg8(interface, W5100S_SHAR1, interface->macAddr.b[1]);
   w5100sWriteReg8(interface, W5100S_SHAR2, interface->macAddr.b[2]);
   w5100sWriteReg8(interface, W5100S_SHAR3, interface->macAddr.b[3]);
   w5100sWriteReg8(interface, W5100S_SHAR4, interface->macAddr.b[4]);
   w5100sWriteReg8(interface, W5100S_SHAR5, interface->macAddr.b[5]);

   //Set TX and RX buffer size for socket 0
   w5100sWriteReg8(interface, W5100S_S0_TXBUF_SIZE, W5100S_Sn_TXBUF_SIZE_8KB);
   w5100sWriteReg8(interface, W5100S_S0_RXBUF_SIZE, W5100S_Sn_RXBUF_SIZE_8KB);

   //Sockets 1 to 3 are not used
   for(i = 1; i <= 3; i++)
   {
      w5100sWriteReg8(interface, W5100S_Sn_TXBUF_SIZE(i),
         W5100S_Sn_TXBUF_SIZE_0KB);

      w5100sWriteReg8(interface, W5100S_Sn_RXBUF_SIZE(i),
         W5100S_Sn_RXBUF_SIZE_0KB);
   }

   //Configure socket 0 in MACRAW mode
   w5100sWriteReg8(interface, W5100S_S0_MR, W5100S_Sn_MR_MF |
      W5100S_Sn_MR_PROTOCOL_MACRAW);

   //Open socket 0
   w5100sWriteReg8(interface, W5100S_S0_CR, W5100S_Sn_CR_OPEN);

   //Wait for command completion
   do
   {
      //Read status register
      value = w5100sReadReg8(interface, W5100S_S0_SR);

      //Check the status of the socket
   } while(value != W5100S_Sn_SR_SOCK_MACRAW);

   //Configure socket 0 interrupts
   w5100sWriteReg8(interface, W5100S_S0_IMR, W5100S_Sn_IMR_SENDOK |
      W5100S_Sn_IMR_RECV);

   //Enable socket 0 interrupts
   w5100sWriteReg8(interface, W5100S_IMR, W5100S_IMR_S0_INT);

   //Perform custom configuration
   w5100sInitHook(interface);

   //Dump registers for debugging purpose
   w5100sDumpReg(interface);

   //Accept any packets from the upper layer
   osSetEvent(&interface->nicTxEvent);

   //Force the TCP/IP stack to poll the link state at startup
   interface->nicEvent = TRUE;
   //Notify the TCP/IP stack of the event
   osSetEvent(&netEvent);

   //Successful initialization
   return NO_ERROR;
}


/**
 * @brief W5100S custom configuration
 * @param[in] interface Underlying network interface
 **/

__weak_func void w5100sInitHook(NetInterface *interface)
{
}


/**
 * @brief W5100S timer handler
 * @param[in] interface Underlying network interface
 **/

void w5100sTick(NetInterface *interface)
{
   uint8_t value;
   bool_t linkState;

   //Read PHY status register
   value = w5100sReadReg8(interface, W5100S_PHYSR0);
   //Retrieve current link state
   linkState = (value & W5100S_PHYSR0_LINK) ? TRUE : FALSE;

   //Check link state
   if(linkState && !interface->linkState)
   {
      //Get current speed
      if((value & W5100S_PHYSR0_SPD) != 0)
      {
         interface->linkSpeed = NIC_LINK_SPEED_10MBPS;
      }
      else
      {
         interface->linkSpeed = NIC_LINK_SPEED_100MBPS;
      }

      //Determine the new duplex mode
      if((value & W5100S_PHYSR0_DPX) != 0)
      {
         interface->duplexMode = NIC_HALF_DUPLEX_MODE;
      }
      else
      {
         interface->duplexMode = NIC_FULL_DUPLEX_MODE;
      }

      //Link is up
      interface->linkState = TRUE;
      //Process link state change event
      nicNotifyLinkChange(interface);
   }
   else if(!linkState && interface->linkState)
   {
      //Link is down
      interface->linkState = FALSE;
      //Process link state change event
      nicNotifyLinkChange(interface);
   }
   else
   {
      //No link change detected
   }
}


/**
 * @brief Enable interrupts
 * @param[in] interface Underlying network interface
 **/

void w5100sEnableIrq(NetInterface *interface)
{
   //Enable interrupts
   if(interface->extIntDriver != NULL)
   {
      interface->extIntDriver->enableIrq();
   }
}


/**
 * @brief Disable interrupts
 * @param[in] interface Underlying network interface
 **/

void w5100sDisableIrq(NetInterface *interface)
{
   //Disable interrupts
   if(interface->extIntDriver != NULL)
   {
      interface->extIntDriver->disableIrq();
   }
}


/**
 * @brief W5100S interrupt service routine
 * @param[in] interface Underlying network interface
 * @return TRUE if a higher priority task must be woken. Else FALSE is returned
 **/

bool_t w5100sIrqHandler(NetInterface *interface)
{
   bool_t flag;
   uint16_t n;
   uint8_t isr;

   //This flag will be set if a higher priority task must be woken
   flag = FALSE;

   //Read socket interrupt register
   isr = w5100sReadReg8(interface, W5100S_IR);
   //Disable interrupts to release the interrupt line
   w5100sWriteReg8(interface, W5100S_IMR, 0);

   //Socket 0 interrupt?
   if((isr & W5100S_IR_S0_INT) != 0)
   {
      //Read socket 0 interrupt register
      isr = w5100sReadReg8(interface, W5100S_S0_IR);

      //Packet transmission complete?
      if((isr & W5100S_Sn_IR_SENDOK) != 0)
      {
         //Get the amount of free memory available in the TX buffer
         n = w5100sReadReg16(interface, W5100S_S0_TX_FSR0);

         //Check whether the TX buffer is available for writing
         if(n >= ETH_MAX_FRAME_SIZE)
         {
            //The transmitter can accept another packet
            osSetEvent(&interface->nicTxEvent);
         }
      }

      //Packet received?
      if((isr & W5100S_Sn_IR_RECV) != 0)
      {
         //Set event flag
         interface->nicEvent = TRUE;
         //Notify the TCP/IP stack of the event
         flag |= osSetEventFromIsr(&netEvent);
      }

      //Clear interrupt flags
      w5100sWriteReg8(interface, W5100S_S0_IR, isr &
         (W5100S_Sn_IR_SENDOK | W5100S_Sn_IR_RECV));
   }

   //Re-enable interrupts once the interrupt has been serviced
   w5100sWriteReg8(interface, W5100S_IMR, W5100S_IMR_S0_INT);

   //A higher priority task must be woken?
   return flag;
}


/**
 * @brief W5100S event handler
 * @param[in] interface Underlying network interface
 **/

void w5100sEventHandler(NetInterface *interface)
{
   error_t error;

   //Process all pending packets
   do
   {
      //Read incoming packet
      error = w5100sReceivePacket(interface);

      //No more data in the receive buffer?
   } while(error != ERROR_BUFFER_EMPTY);
}


/**
 * @brief Send a packet
 * @param[in] interface Underlying network interface
 * @param[in] buffer Multi-part buffer containing the data to send
 * @param[in] offset Offset to the first data byte
 * @param[in] ancillary Additional options passed to the stack along with
 *   the packet
 * @return Error code
 **/

error_t w5100sSendPacket(NetInterface *interface,
   const NetBuffer *buffer, size_t offset, NetTxAncillary *ancillary)
{
   static uint8_t temp[W5100S_ETH_TX_BUFFER_SIZE];
   uint16_t n;
   size_t length;

   //Retrieve the length of the packet
   length = netBufferGetLength(buffer) - offset;

   //Check the frame length
   if(length > ETH_MAX_FRAME_SIZE)
   {
      //The transmitter can accept another packet
      osSetEvent(&interface->nicTxEvent);
      //Report an error
      return ERROR_INVALID_LENGTH;
   }

   //Get the amount of free memory available in the TX buffer
   n = w5100sReadReg16(interface, W5100S_S0_TX_FSR0);

   //Make sure the TX buffer is available for writing
   if(n < length)
      return ERROR_FAILURE;

   //Copy user data
   netBufferRead(temp, buffer, offset, length);

   //Write packet data
   w5100sWriteData(interface, temp, length);

   //Get the amount of free memory available in the TX buffer
   n = w5100sReadReg16(interface, W5100S_S0_TX_FSR0);

   //Check whether the TX buffer is available for writing
   if(n >= ETH_MAX_FRAME_SIZE)
   {
      //The transmitter can accept another packet
      osSetEvent(&interface->nicTxEvent);
   }

   //Successful processing
   return NO_ERROR;
}


/**
 * @brief Receive a packet
 * @param[in] interface Underlying network interface
 * @return Error code
 **/

error_t w5100sReceivePacket(NetInterface *interface)
{
   static uint8_t temp[W5100S_ETH_RX_BUFFER_SIZE];
   error_t error;
   size_t length;

   //Get the amount of data in the RX buffer
   length = w5100sReadReg16(interface, W5100S_S0_RX_RSR0);

   //Any packet pending in the receive buffer?
   if(length > 0)
   {
      //Read packet header
      w5100sReadData(interface, temp, 2);

      //Retrieve the length of the received packet
      length = LOAD16BE(temp);

      //Ensure the packet size is acceptable
      if(length >= 2 && length <= (ETH_MAX_FRAME_SIZE + 2))
      {
         //Read packet data
         w5100sReadData(interface, temp, length - 2);
         //Successful processing
         error = NO_ERROR;
      }
      else
      {
         //The packet length is not valid
         error = ERROR_INVALID_LENGTH;
      }
   }
   else
   {
      //No more data in the receive buffer
      error = ERROR_BUFFER_EMPTY;
   }

   //Check whether a valid packet has been received
   if(!error)
   {
      NetRxAncillary ancillary;

      //Additional options can be passed to the stack along with the packet
      ancillary = NET_DEFAULT_RX_ANCILLARY;

      //Pass the packet to the upper layer
      nicProcessPacket(interface, temp, length, &ancillary);
   }

   //Return status code
   return error;
}


/**
 * @brief Configure MAC address filtering
 * @param[in] interface Underlying network interface
 * @return Error code
 **/

error_t w5100sUpdateMacAddrFilter(NetInterface *interface)
{
   //Not implemented
   return NO_ERROR;
}


/**
 * @brief Write 8-bit register
 * @param[in] interface Underlying network interface
 * @param[in] address Register address
 * @param[in] data Register value
 **/

void w5100sWriteReg8(NetInterface *interface, uint16_t address, uint8_t data)
{
   //Pull the CS pin low
   interface->spiDriver->assertCs();

   //Control phase
   interface->spiDriver->transfer(W5100S_CTRL_WRITE);

   //Address phase
   interface->spiDriver->transfer(MSB(address));
   interface->spiDriver->transfer(LSB(address));

   //Data phase
   interface->spiDriver->transfer(data);

   //Terminate the operation by raising the CS pin
   interface->spiDriver->deassertCs();
}


/**
 * @brief Read 8-bit register
 * @param[in] interface Underlying network interface
 * @param[in] address Register address
 * @return Register value
 **/

uint8_t w5100sReadReg8(NetInterface *interface, uint16_t address)
{
   uint8_t data;

   //Pull the CS pin low
   interface->spiDriver->assertCs();

   //Control phase
   interface->spiDriver->transfer(W5100S_CTRL_READ);

   //Address phase
   interface->spiDriver->transfer(MSB(address));
   interface->spiDriver->transfer(LSB(address));

   //Data phase
   data = interface->spiDriver->transfer(0x00);

   //Terminate the operation by raising the CS pin
   interface->spiDriver->deassertCs();

   //Return register value
   return data;
}


/**
 * @brief Write 16-bit register
 * @param[in] interface Underlying network interface
 * @param[in] address Register address
 * @param[in] data Register value
 **/

void w5100sWriteReg16(NetInterface *interface, uint16_t address, uint16_t data)
{
   //Pull the CS pin low
   interface->spiDriver->assertCs();

   //Control phase
   interface->spiDriver->transfer(W5100S_CTRL_WRITE);

   //Address phase
   interface->spiDriver->transfer(MSB(address));
   interface->spiDriver->transfer(LSB(address));

   //Data phase
   interface->spiDriver->transfer(MSB(data));
   interface->spiDriver->transfer(LSB(data));

   //Terminate the operation by raising the CS pin
   interface->spiDriver->deassertCs();
}


/**
 * @brief Read 16-bit register
 * @param[in] interface Underlying network interface
 * @param[in] address Register address
 * @return Register value
 **/

uint16_t w5100sReadReg16(NetInterface *interface, uint16_t address)
{
   uint16_t data;

   //Pull the CS pin low
   interface->spiDriver->assertCs();

   //Control phase
   interface->spiDriver->transfer(W5100S_CTRL_READ);

   //Address phase
   interface->spiDriver->transfer(MSB(address));
   interface->spiDriver->transfer(LSB(address));

   //Data phase
   data = interface->spiDriver->transfer(0x00) << 8;
   data |= interface->spiDriver->transfer(0x00);

   //Terminate the operation by raising the CS pin
   interface->spiDriver->deassertCs();

   //Return register value
   return data;
}


/**
 * @brief Write data
 * @param[in] interface Underlying network interface
 * @param[in] data Pointer to the data being written
 * @param[in] length Number of data to write
 **/

void w5100sWriteData(NetInterface *interface, const uint8_t *data,
   size_t length)
{
   size_t p;
   size_t size;
   size_t offset;

   //Get TX buffer size
   size = w5100sReadReg8(interface, W5100S_S0_TXBUF_SIZE) * 1024;
   //Get TX write pointer
   p = w5100sReadReg16(interface, W5100S_S0_TX_WR0);

   //Retrieve current offset
   offset = p & (size - 1);

   //Check whether the data crosses buffer boundaries
   if((offset + length) < size)
   {
      //Write data
      w5100sWriteBuffer(interface, W5100S_TX_BUFFER + offset, data, length);
   }
   else
   {
      //Write the first part of the data
      w5100sWriteBuffer(interface, W5100S_TX_BUFFER + offset, data,
         size - offset);

      //Wrap around to the beginning of the circular buffer
      w5100sWriteBuffer(interface, W5100S_TX_BUFFER,
         data + size - offset, offset + length - size);
   }

   //Increment TX write pointer
   w5100sWriteReg16(interface, W5100S_S0_TX_WR0, p + length);

   //Start packet transmission
   w5100sWriteReg8(interface, W5100S_S0_CR, W5100S_Sn_CR_SEND);
}


/**
 * @brief Read data
 * @param[in] interface Underlying network interface
 * @param[out] data Buffer where to store the incoming data
 * @param[in] length Number of data to read
 **/

void w5100sReadData(NetInterface *interface, uint8_t *data, size_t length)
{
   size_t p;
   size_t size;
   size_t offset;

   //Get RX buffer size
   size = w5100sReadReg8(interface, W5100S_S0_RXBUF_SIZE) * 1024;
   //Get RX read pointer
   p = w5100sReadReg16(interface, W5100S_S0_RX_RD0);

   //Retrieve current offset
   offset = p & (size - 1);

   //Check whether the data crosses buffer boundaries
   if((offset + length) < size)
   {
      //Read data
      w5100sReadBuffer(interface, W5100S_RX_BUFFER + offset, data, length);
   }
   else
   {
      //Read the first part of the data
      w5100sReadBuffer(interface, W5100S_RX_BUFFER + offset, data,
         size - offset);

      //Wrap around to the beginning of the circular buffer
      w5100sReadBuffer(interface, W5100S_RX_BUFFER,
         data + size - offset, offset + length - size);
   }

   //Increment RX read pointer
   w5100sWriteReg16(interface, W5100S_S0_RX_RD0, p + length);

   //Complete the processing of the receive data
   w5100sWriteReg8(interface, W5100S_S0_CR, W5100S_Sn_CR_RECV);
}


/**
 * @brief Write TX buffer
 * @param[in] interface Underlying network interface
 * @param[in] address Buffer address
 * @param[in] data Pointer to the data being written
 * @param[in] length Number of data to write
 **/

void w5100sWriteBuffer(NetInterface *interface, uint16_t address,
   const uint8_t *data, size_t length)
{
   size_t i;

   //Pull the CS pin low
   interface->spiDriver->assertCs();

   //Control phase
   interface->spiDriver->transfer(W5100S_CTRL_WRITE);

   //Address phase
   interface->spiDriver->transfer(MSB(address));
   interface->spiDriver->transfer(LSB(address));

   //Data phase
   for(i = 0; i < length; i++)
   {
      interface->spiDriver->transfer(data[i]);
   }

   //Terminate the operation by raising the CS pin
   interface->spiDriver->deassertCs();
}


/**
 * @brief Read RX buffer
 * @param[in] interface Underlying network interface
 * @param[in] address Buffer address
 * @param[out] data Buffer where to store the incoming data
 * @param[in] length Number of data to read
 **/

void w5100sReadBuffer(NetInterface *interface, uint16_t address, uint8_t *data,
   size_t length)
{
   size_t i;

   //Pull the CS pin low
   interface->spiDriver->assertCs();

   //Control phase
   interface->spiDriver->transfer(W5100S_CTRL_READ);

   //Address phase
   interface->spiDriver->transfer(MSB(address));
   interface->spiDriver->transfer(LSB(address));

   //Data phase
   for(i = 0; i < length; i++)
   {
      data[i] = interface->spiDriver->transfer(0x00);
   }

   //Terminate the operation by raising the CS pin
   interface->spiDriver->deassertCs();
}


/**
 * @brief Dump registers for debugging purpose
 * @param[in] interface Underlying network interface
 **/

void w5100sDumpReg(NetInterface *interface)
{
   uint16_t i;

   //Loop through registers
   for(i = 0; i < 64; i++)
   {
      //Display current host MAC register
      TRACE_DEBUG("%02" PRIX16 ": 0x%02" PRIX8 "\r\n", i,
         w5100sReadReg8(interface, i));
   }

   //Terminate with a line feed
   TRACE_DEBUG("\r\n");
}
