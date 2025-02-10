/**
 * @file ksz9477_driver.c
 * @brief KSZ9477 7-port Gigabit Ethernet switch driver
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
#include "core/ethernet_misc.h"
#include "drivers/switch/ksz9477_driver.h"
#include "debug.h"


/**
 * @brief KSZ9477 Ethernet switch driver
 **/

const SwitchDriver ksz9477SwitchDriver =
{
   ksz9477Init,
   ksz9477Tick,
   ksz9477EnableIrq,
   ksz9477DisableIrq,
   ksz9477EventHandler,
   ksz9477TagFrame,
   ksz9477UntagFrame,
   ksz9477GetLinkState,
   ksz9477GetLinkSpeed,
   ksz9477GetDuplexMode,
   ksz9477SetPortState,
   ksz9477GetPortState,
   ksz9477SetAgingTime,
   ksz9477EnableIgmpSnooping,
   ksz9477EnableMldSnooping,
   ksz9477EnableRsvdMcastTable,
   ksz9477AddStaticFdbEntry,
   ksz9477DeleteStaticFdbEntry,
   ksz9477GetStaticFdbEntry,
   ksz9477FlushStaticFdbTable,
   ksz9477GetDynamicFdbEntry,
   ksz9477FlushDynamicFdbTable,
   ksz9477SetUnknownMcastFwdPorts
};


/**
 * @brief Tail tag rules (host to KSZ9477)
 **/

const uint16_t ksz9477IngressTailTag[6] =
{
   HTONS(KSZ9477_TAIL_TAG_NORMAL_ADDR_LOOKUP),
   HTONS(KSZ9477_TAIL_TAG_PORT_BLOCKING_OVERRIDE | KSZ9477_TAIL_TAG_DEST_PORT1),
   HTONS(KSZ9477_TAIL_TAG_PORT_BLOCKING_OVERRIDE | KSZ9477_TAIL_TAG_DEST_PORT2),
   HTONS(KSZ9477_TAIL_TAG_PORT_BLOCKING_OVERRIDE | KSZ9477_TAIL_TAG_DEST_PORT3),
   HTONS(KSZ9477_TAIL_TAG_PORT_BLOCKING_OVERRIDE | KSZ9477_TAIL_TAG_DEST_PORT4),
   HTONS(KSZ9477_TAIL_TAG_PORT_BLOCKING_OVERRIDE | KSZ9477_TAIL_TAG_DEST_PORT5)
};


/**
 * @brief KSZ9477 Ethernet switch initialization
 * @param[in] interface Underlying network interface
 * @return Error code
 **/

error_t ksz9477Init(NetInterface *interface)
{
   uint_t port;
   uint8_t temp;

   //Debug message
   TRACE_INFO("Initializing KSZ9477...\r\n");

   //SPI slave mode?
   if(interface->spiDriver != NULL)
   {
      //Initialize SPI interface
      interface->spiDriver->init();

      //Wait for the serial interface to be ready
      do
      {
         //Read CHIP_ID1 register
         temp = ksz9477ReadSwitchReg8(interface, KSZ9477_CHIP_ID1);

         //The returned data is invalid until the serial interface is ready
      } while(temp != KSZ9477_CHIP_ID1_DEFAULT);

#if (ETH_PORT_TAGGING_SUPPORT == ENABLED)
      //Enable tail tag feature
      temp = ksz9477ReadSwitchReg8(interface, KSZ9477_PORT6_OP_CTRL0);
      temp |= KSZ9477_PORTn_OP_CTRL0_TAIL_TAG_EN;
      ksz9477WriteSwitchReg8(interface, KSZ9477_PORT6_OP_CTRL0, temp);

      //Disable frame length check (silicon errata workaround 16)
      temp = ksz9477ReadSwitchReg8(interface, KSZ9477_SWITCH_MAC_CTRL0);
      temp &= ~KSZ9477_SWITCH_MAC_CTRL0_FRAME_LEN_CHECK_EN;
      ksz9477WriteSwitchReg8(interface, KSZ9477_SWITCH_MAC_CTRL0, temp);
#else
      //Disable tail tag feature
      temp = ksz9477ReadSwitchReg8(interface, KSZ9477_PORT6_OP_CTRL0);
      temp &= ~KSZ9477_PORTn_OP_CTRL0_TAIL_TAG_EN;
      ksz9477WriteSwitchReg8(interface, KSZ9477_PORT6_OP_CTRL0, temp);

      //Enable frame length check
      temp = ksz9477ReadSwitchReg8(interface, KSZ9477_SWITCH_MAC_CTRL0);
      temp |= KSZ9477_SWITCH_MAC_CTRL0_FRAME_LEN_CHECK_EN;
      ksz9477WriteSwitchReg8(interface, KSZ9477_SWITCH_MAC_CTRL0, temp);
#endif

      //Loop through the ports
      for(port = KSZ9477_PORT1; port <= KSZ9477_PORT5; port++)
      {
#if (ETH_PORT_TAGGING_SUPPORT == ENABLED)
         //Port separation mode?
         if(interface->port != 0)
         {
            //Disable packet transmission and address learning
            ksz9477SetPortState(interface, port, SWITCH_PORT_STATE_LISTENING);
         }
         else
#endif
         {
            //Enable transmission, reception and address learning
            ksz9477SetPortState(interface, port, SWITCH_PORT_STATE_FORWARDING);
         }
      }

      //Restore default age count
      ksz9477WriteSwitchReg8(interface, KSZ9477_SWITCH_LUE_CTRL0,
         KSZ9477_SWITCH_LUE_CTRL0_AGE_COUNT_DEFAULT |
         KSZ9477_SWITCH_LUE_CTRL0_HASH_OPTION_CRC);

      //Restore default age period
      ksz9477WriteSwitchReg8(interface, KSZ9477_SWITCH_LUE_CTRL3,
         KSZ9477_SWITCH_LUE_CTRL3_AGE_PERIOD_DEFAULT);

      //Add internal delay to ingress and egress RGMII clocks
      temp = ksz9477ReadSwitchReg8(interface, KSZ9477_PORT6_XMII_CTRL1);
      temp |= KSZ9477_PORTn_XMII_CTRL1_RGMII_ID_IG;
      temp |= KSZ9477_PORTn_XMII_CTRL1_RGMII_ID_EG;
      ksz9477WriteSwitchReg8(interface, KSZ9477_PORT6_XMII_CTRL1, temp);

      //Start switch operation
      ksz9477WriteSwitchReg8(interface, KSZ9477_SWITCH_OP,
         KSZ9477_SWITCH_OP_START_SWITCH);
   }
   else if(interface->smiDriver != NULL)
   {
      //Initialize serial management interface
      interface->smiDriver->init();
   }
   else
   {
      //Just for sanity
   }

   //Loop through the ports
   for(port = KSZ9477_PORT1; port <= KSZ9477_PORT5; port++)
   {
      //Improve PHY receive performance (silicon errata workaround 1)
      ksz9477WriteMmdReg(interface, port, 0x01, 0x6F, 0xDD0B);
      ksz9477WriteMmdReg(interface, port, 0x01, 0x8F, 0x6032);
      ksz9477WriteMmdReg(interface, port, 0x01, 0x9D, 0x248C);
      ksz9477WriteMmdReg(interface, port, 0x01, 0x75, 0x0060);
      ksz9477WriteMmdReg(interface, port, 0x01, 0xD3, 0x7777);
      ksz9477WriteMmdReg(interface, port, 0x1C, 0x06, 0x3008);
      ksz9477WriteMmdReg(interface, port, 0x1C, 0x08, 0x2001);

      //Improve transmit waveform amplitude (silicon errata workaround 2)
      ksz9477WriteMmdReg(interface, port, 0x1C, 0x04, 0x00D0);

      //EEE must be manually disabled (silicon errata workaround 4)
      ksz9477WriteMmdReg(interface, port, KSZ9477_MMD_EEE_ADV, 0);

      //Adjust power supply settings (silicon errata workaround 9)
      ksz9477WriteMmdReg(interface, port, 0x1C, 0x13, 0x6EFF);
      ksz9477WriteMmdReg(interface, port, 0x1C, 0x14, 0xE6FF);
      ksz9477WriteMmdReg(interface, port, 0x1C, 0x15, 0x6EFF);
      ksz9477WriteMmdReg(interface, port, 0x1C, 0x16, 0xE6FF);
      ksz9477WriteMmdReg(interface, port, 0x1C, 0x17, 0x00FF);
      ksz9477WriteMmdReg(interface, port, 0x1C, 0x18, 0x43FF);
      ksz9477WriteMmdReg(interface, port, 0x1C, 0x19, 0xC3FF);
      ksz9477WriteMmdReg(interface, port, 0x1C, 0x1A, 0x6FFF);
      ksz9477WriteMmdReg(interface, port, 0x1C, 0x1B, 0x07FF);
      ksz9477WriteMmdReg(interface, port, 0x1C, 0x1C, 0x0FFF);
      ksz9477WriteMmdReg(interface, port, 0x1C, 0x1D, 0xE7FF);
      ksz9477WriteMmdReg(interface, port, 0x1C, 0x1E, 0xEFFF);
      ksz9477WriteMmdReg(interface, port, 0x1C, 0x20, 0xEEEE);

      //Select tri-color dual-LED mode (silicon errata workaround 19)
      ksz9477WriteMmdReg(interface, port, KSZ9477_MMD_LED_MODE,
         KSZ9477_MMD_LED_MODE_LED_MODE_TRI_COLOR_DUAL |
         KSZ9477_MMD_LED_MODE_RESERVED_DEFAULT);

      //Debug message
      TRACE_DEBUG("Port %u:\r\n", port);
      //Dump PHY registers for debugging purpose
      ksz9477DumpPhyReg(interface, port);
   }

   //Perform custom configuration
   ksz9477InitHook(interface);

   //Force the TCP/IP stack to poll the link state at startup
   interface->phyEvent = TRUE;
   //Notify the TCP/IP stack of the event
   osSetEvent(&netEvent);

   //Successful initialization
   return NO_ERROR;
}


/**
 * @brief KSZ9477 custom configuration
 * @param[in] interface Underlying network interface
 **/

__weak_func void ksz9477InitHook(NetInterface *interface)
{
}


/**
 * @brief KSZ9477 timer handler
 * @param[in] interface Underlying network interface
 **/

void ksz9477Tick(NetInterface *interface)
{
   uint_t port;
   bool_t linkState;

#if (ETH_PORT_TAGGING_SUPPORT == ENABLED)
   //Port separation mode?
   if(interface->port != 0)
   {
      uint_t i;
      NetInterface *virtualInterface;

      //Loop through network interfaces
      for(i = 0; i < NET_INTERFACE_COUNT; i++)
      {
         //Point to the current interface
         virtualInterface = &netInterface[i];

         //Check whether the current virtual interface is attached to the
         //physical interface
         if(virtualInterface == interface ||
            virtualInterface->parent == interface)
         {
            //Retrieve current link state
            linkState = ksz9477GetLinkState(interface, virtualInterface->port);

            //Link up or link down event?
            if(linkState != virtualInterface->linkState)
            {
               //Set event flag
               interface->phyEvent = TRUE;
               //Notify the TCP/IP stack of the event
               osSetEvent(&netEvent);
            }
         }
      }
   }
   else
#endif
   {
      //Initialize link state
      linkState = FALSE;

      //Loop through the ports
      for(port = KSZ9477_PORT1; port <= KSZ9477_PORT5; port++)
      {
         //Retrieve current link state
         if(ksz9477GetLinkState(interface, port))
         {
            linkState = TRUE;
         }
      }

      //Link up or link down event?
      if(linkState != interface->linkState)
      {
         //Set event flag
         interface->phyEvent = TRUE;
         //Notify the TCP/IP stack of the event
         osSetEvent(&netEvent);
      }
   }
}


/**
 * @brief Enable interrupts
 * @param[in] interface Underlying network interface
 **/

void ksz9477EnableIrq(NetInterface *interface)
{
}


/**
 * @brief Disable interrupts
 * @param[in] interface Underlying network interface
 **/

void ksz9477DisableIrq(NetInterface *interface)
{
}


/**
 * @brief KSZ9477 event handler
 * @param[in] interface Underlying network interface
 **/

void ksz9477EventHandler(NetInterface *interface)
{
   uint_t port;
   bool_t linkState;

#if (ETH_PORT_TAGGING_SUPPORT == ENABLED)
   //Port separation mode?
   if(interface->port != 0)
   {
      uint_t i;
      NetInterface *virtualInterface;

      //Loop through network interfaces
      for(i = 0; i < NET_INTERFACE_COUNT; i++)
      {
         //Point to the current interface
         virtualInterface = &netInterface[i];

         //Check whether the current virtual interface is attached to the
         //physical interface
         if(virtualInterface == interface ||
            virtualInterface->parent == interface)
         {
            //Get the port number associated with the current interface
            port = virtualInterface->port;

            //Valid port?
            if(port >= KSZ9477_PORT1 && port <= KSZ9477_PORT5)
            {
               //Retrieve current link state
               linkState = ksz9477GetLinkState(interface, port);

               //Link up event?
               if(linkState && !virtualInterface->linkState)
               {
                  //Retrieve host interface speed
                  interface->linkSpeed = ksz9477GetLinkSpeed(interface,
                     KSZ9477_PORT6);

                  //Retrieve host interface duplex mode
                  interface->duplexMode = ksz9477GetDuplexMode(interface,
                     KSZ9477_PORT6);

                  //Adjust MAC configuration parameters for proper operation
                  interface->nicDriver->updateMacConfig(interface);

                  //Check current speed
                  virtualInterface->linkSpeed = ksz9477GetLinkSpeed(interface,
                     port);

                  //Check current duplex mode
                  virtualInterface->duplexMode = ksz9477GetDuplexMode(interface,
                     port);

                  //Update link state
                  virtualInterface->linkState = TRUE;

                  //Process link state change event
                  nicNotifyLinkChange(virtualInterface);
               }
               //Link down event
               else if(!linkState && virtualInterface->linkState)
               {
                  //Update link state
                  virtualInterface->linkState = FALSE;

                  //Process link state change event
                  nicNotifyLinkChange(virtualInterface);
               }
            }
         }
      }
   }
   else
#endif
   {
      //Initialize link state
      linkState = FALSE;

      //Loop through the ports
      for(port = KSZ9477_PORT1; port <= KSZ9477_PORT5; port++)
      {
         //Retrieve current link state
         if(ksz9477GetLinkState(interface, port))
         {
            linkState = TRUE;
         }
      }

      //Link up event?
      if(linkState)
      {
         //Retrieve host interface speed
         interface->linkSpeed = ksz9477GetLinkSpeed(interface, KSZ9477_PORT6);
         //Retrieve host interface duplex mode
         interface->duplexMode = ksz9477GetDuplexMode(interface, KSZ9477_PORT6);

         //Adjust MAC configuration parameters for proper operation
         interface->nicDriver->updateMacConfig(interface);

         //Update link state
         interface->linkState = TRUE;
      }
      else
      {
         //Update link state
         interface->linkState = FALSE;
      }

      //Process link state change event
      nicNotifyLinkChange(interface);
   }
}


/**
 * @brief Add tail tag to Ethernet frame
 * @param[in] interface Underlying network interface
 * @param[in] buffer Multi-part buffer containing the payload
 * @param[in,out] offset Offset to the first payload byte
 * @param[in] ancillary Additional options passed to the stack along with
 *   the packet
 * @return Error code
 **/

error_t ksz9477TagFrame(NetInterface *interface, NetBuffer *buffer,
   size_t *offset, NetTxAncillary *ancillary)
{
   error_t error;

   //Initialize status code
   error = NO_ERROR;

#if (ETH_PORT_TAGGING_SUPPORT == ENABLED)
   //SPI slave mode?
   if(interface->spiDriver != NULL)
   {
      //Valid port?
      if(ancillary->port <= KSZ9477_PORT5)
      {
         size_t length;
         const uint16_t *tailTag;

         //The two-byte tail tagging is used to indicate the destination port
         tailTag = &ksz9477IngressTailTag[ancillary->port];

         //Retrieve the length of the Ethernet frame
         length = netBufferGetLength(buffer) - *offset;

         //The host controller should manually add padding to the packet before
         //inserting the tail tag
         error = ethPadFrame(buffer, &length);

         //Check status code
         if(!error)
         {
            //The tail tag is inserted at the end of the packet, just before
            //the CRC
            error = netBufferAppend(buffer, tailTag, sizeof(uint16_t));
         }
      }
      else
      {
         //The port number is not valid
         error = ERROR_INVALID_PORT;
      }
   }
#endif

   //Return status code
   return error;
}


/**
 * @brief Decode tail tag from incoming Ethernet frame
 * @param[in] interface Underlying network interface
 * @param[in,out] frame Pointer to the received Ethernet frame
 * @param[in,out] length Length of the frame, in bytes
 * @param[in,out] ancillary Additional options passed to the stack along with
 *   the packet
 * @return Error code
 **/

error_t ksz9477UntagFrame(NetInterface *interface, uint8_t **frame,
   size_t *length, NetRxAncillary *ancillary)
{
   error_t error;

   //Initialize status code
   error = NO_ERROR;

#if (ETH_PORT_TAGGING_SUPPORT == ENABLED)
   //SPI slave mode?
   if(interface->spiDriver != NULL)
   {
      //Valid Ethernet frame received?
      if(*length >= (sizeof(EthHeader) + sizeof(uint8_t)))
      {
         uint8_t *tailTag;

         //The tail tag is inserted at the end of the packet, just before
         //the CRC
         tailTag = *frame + *length - sizeof(uint8_t);

         //The one byte tail tagging is used to indicate the source port
         ancillary->port = (*tailTag & KSZ9477_TAIL_TAG_SRC_PORT) + 1;

         //Strip tail tag from Ethernet frame
         *length -= sizeof(uint8_t);
      }
      else
      {
         //Drop the received frame
         error = ERROR_INVALID_LENGTH;
      }
   }
   else
   {
      //Tail tagging mode cannot be enabled through MDC/MDIO interface
      ancillary->port = 0;
   }
#endif

   //Return status code
   return error;
}


/**
 * @brief Get link state
 * @param[in] interface Underlying network interface
 * @param[in] port Port number
 * @return Link state
 **/

bool_t ksz9477GetLinkState(NetInterface *interface, uint8_t port)
{
   uint16_t value;
   bool_t linkState;

   //Check port number
   if(port >= KSZ9477_PORT1 && port <= KSZ9477_PORT5)
   {
      //Any link failure condition is latched in the BMSR register. Reading
      //the register twice will always return the actual link status
      value = ksz9477ReadPhyReg(interface, port, KSZ9477_BMSR);
      value = ksz9477ReadPhyReg(interface, port, KSZ9477_BMSR);

      //Retrieve current link state
      linkState = (value & KSZ9477_BMSR_LINK_STATUS) ? TRUE : FALSE;
   }
   else
   {
      //The specified port number is not valid
      linkState = FALSE;
   }

   //Return link status
   return linkState;
}


/**
 * @brief Get link speed
 * @param[in] interface Underlying network interface
 * @param[in] port Port number
 * @return Link speed
 **/

uint32_t ksz9477GetLinkSpeed(NetInterface *interface, uint8_t port)
{
   uint8_t type;
   uint16_t value;
   uint32_t linkSpeed;

   //Check port number
   if(port >= KSZ9477_PORT1 && port <= KSZ9477_PORT5)
   {
      //Read PHY control register
      value = ksz9477ReadPhyReg(interface, port, KSZ9477_PHYCON);

      //Retrieve current link speed
      if((value & KSZ9477_PHYCON_SPEED_1000BT) != 0)
      {
         //1000BASE-T
         linkSpeed = NIC_LINK_SPEED_1GBPS;
      }
      else if((value & KSZ9477_PHYCON_SPEED_100BTX) != 0)
      {
         //100BASE-TX
         linkSpeed = NIC_LINK_SPEED_100MBPS;
      }
      else if((value & KSZ9477_PHYCON_SPEED_10BT) != 0)
      {
         //10BASE-T
         linkSpeed = NIC_LINK_SPEED_10MBPS;
      }
      else
      {
         //The link speed is not valid
         linkSpeed = NIC_LINK_SPEED_UNKNOWN;
      }
   }
   else if(port == KSZ9477_PORT6)
   {
      //SPI slave mode?
      if(interface->spiDriver != NULL)
      {
         //Read port 6 XMII control 1 register
         value = ksz9477ReadSwitchReg8(interface, KSZ9477_PORT6_XMII_CTRL1);

         //Retrieve host interface type
         type = value & KSZ9477_PORTn_XMII_CTRL1_IF_TYPE;

         //Gigabit interface?
         if(type == KSZ9477_PORTn_XMII_CTRL1_IF_TYPE_RGMII &&
            (value & KSZ9477_PORTn_XMII_CTRL1_SPEED_1000) == 0)
         {
            //1000 Mb/s mode
            linkSpeed = NIC_LINK_SPEED_1GBPS;
         }
         else
         {
            //Read port 6 XMII control 0 register
            value = ksz9477ReadSwitchReg8(interface, KSZ9477_PORT6_XMII_CTRL0);

            //Retrieve host interface speed
            if((value & KSZ9477_PORTn_XMII_CTRL0_SPEED_10_100) != 0)
            {
               //100 Mb/s mode
               linkSpeed = NIC_LINK_SPEED_100MBPS;
            }
            else
            {
               //10 Mb/s mode
               linkSpeed = NIC_LINK_SPEED_10MBPS;
            }
         }
      }
      else
      {
         //The MDC/MDIO interface does not have access to all the configuration
         //registers. It can only access the standard MIIM registers
         linkSpeed = NIC_LINK_SPEED_100MBPS;
      }
   }
   else
   {
      //The specified port number is not valid
      linkSpeed = NIC_LINK_SPEED_UNKNOWN;
   }

   //Return link status
   return linkSpeed;
}


/**
 * @brief Get duplex mode
 * @param[in] interface Underlying network interface
 * @param[in] port Port number
 * @return Duplex mode
 **/

NicDuplexMode ksz9477GetDuplexMode(NetInterface *interface, uint8_t port)
{
   uint16_t value;
   NicDuplexMode duplexMode;

   //Check port number
   if(port >= KSZ9477_PORT1 && port <= KSZ9477_PORT5)
   {
      //Read PHY control register
      value = ksz9477ReadPhyReg(interface, port, KSZ9477_PHYCON);

      //Retrieve current duplex mode
      if((value & KSZ9477_PHYCON_DUPLEX_STATUS) != 0)
      {
         duplexMode = NIC_FULL_DUPLEX_MODE;
      }
      else
      {
         duplexMode = NIC_HALF_DUPLEX_MODE;
      }
   }
   else if(port == KSZ9477_PORT6)
   {
      //SPI slave mode?
      if(interface->spiDriver != NULL)
      {
         //Read port 6 XMII control 0 register
         value = ksz9477ReadSwitchReg8(interface, KSZ9477_PORT6_XMII_CTRL0);

         //Retrieve host interface duplex mode
         if((value & KSZ9477_PORTn_XMII_CTRL0_DUPLEX) != 0)
         {
            duplexMode = NIC_FULL_DUPLEX_MODE;
         }
         else
         {
            duplexMode = NIC_HALF_DUPLEX_MODE;
         }
      }
      else
      {
         //The MDC/MDIO interface does not have access to all the configuration
         //registers. It can only access the standard MIIM registers
         duplexMode = NIC_FULL_DUPLEX_MODE;
      }
   }
   else
   {
      //The specified port number is not valid
      duplexMode = NIC_UNKNOWN_DUPLEX_MODE;
   }

   //Return duplex mode
   return duplexMode;
}


/**
 * @brief Set port state
 * @param[in] interface Underlying network interface
 * @param[in] port Port number
 * @param[in] state Port state
 **/

void ksz9477SetPortState(NetInterface *interface, uint8_t port,
   SwitchPortState state)
{
   uint8_t temp;

   //Check port number
   if(port >= KSZ9477_PORT1 && port <= KSZ9477_PORT5)
   {
      //Read MSTP state register
      temp = ksz9477ReadSwitchReg8(interface, KSZ9477_PORTn_MSTP_STATE(port));

      //Update port state
      switch(state)
      {
      //Listening state
      case SWITCH_PORT_STATE_LISTENING:
         temp &= ~KSZ9477_PORTn_MSTP_STATE_TRANSMIT_EN;
         temp |= KSZ9477_PORTn_MSTP_STATE_RECEIVE_EN;
         temp |= KSZ9477_PORTn_MSTP_STATE_LEARNING_DIS;
         break;

      //Learning state
      case SWITCH_PORT_STATE_LEARNING:
         temp &= ~KSZ9477_PORTn_MSTP_STATE_TRANSMIT_EN;
         temp &= ~KSZ9477_PORTn_MSTP_STATE_RECEIVE_EN;
         temp &= ~KSZ9477_PORTn_MSTP_STATE_LEARNING_DIS;
         break;

      //Forwarding state
      case SWITCH_PORT_STATE_FORWARDING:
         temp |= KSZ9477_PORTn_MSTP_STATE_TRANSMIT_EN;
         temp |= KSZ9477_PORTn_MSTP_STATE_RECEIVE_EN;
         temp &= ~KSZ9477_PORTn_MSTP_STATE_LEARNING_DIS;
         break;

      //Disabled state
      default:
         temp &= ~KSZ9477_PORTn_MSTP_STATE_TRANSMIT_EN;
         temp &= ~KSZ9477_PORTn_MSTP_STATE_RECEIVE_EN;
         temp |= KSZ9477_PORTn_MSTP_STATE_LEARNING_DIS;
         break;
      }

      //Write the value back to MSTP state register
      ksz9477WriteSwitchReg8(interface, KSZ9477_PORTn_MSTP_STATE(port), temp);
   }
}


/**
 * @brief Get port state
 * @param[in] interface Underlying network interface
 * @param[in] port Port number
 * @return Port state
 **/

SwitchPortState ksz9477GetPortState(NetInterface *interface, uint8_t port)
{
   uint8_t temp;
   SwitchPortState state;

   //Check port number
   if(port >= KSZ9477_PORT1 && port <= KSZ9477_PORT5)
   {
      //Read MSTP state register
      temp = ksz9477ReadSwitchReg8(interface, KSZ9477_PORTn_MSTP_STATE(port));

      //Check port state
      if((temp & KSZ9477_PORTn_MSTP_STATE_TRANSMIT_EN) == 0 &&
         (temp & KSZ9477_PORTn_MSTP_STATE_RECEIVE_EN) == 0 &&
         (temp & KSZ9477_PORTn_MSTP_STATE_LEARNING_DIS) != 0)
      {
         //Disabled state
         state = SWITCH_PORT_STATE_DISABLED;
      }
      else if((temp & KSZ9477_PORTn_MSTP_STATE_TRANSMIT_EN) == 0 &&
         (temp & KSZ9477_PORTn_MSTP_STATE_RECEIVE_EN) != 0 &&
         (temp & KSZ9477_PORTn_MSTP_STATE_LEARNING_DIS) != 0)
      {
         //Listening state
         state = SWITCH_PORT_STATE_LISTENING;
      }
      else if((temp & KSZ9477_PORTn_MSTP_STATE_TRANSMIT_EN) == 0 &&
         (temp & KSZ9477_PORTn_MSTP_STATE_RECEIVE_EN) == 0 &&
         (temp & KSZ9477_PORTn_MSTP_STATE_LEARNING_DIS) == 0)
      {
         //Learning state
         state = SWITCH_PORT_STATE_LEARNING;
      }
      else if((temp & KSZ9477_PORTn_MSTP_STATE_TRANSMIT_EN) != 0 &&
         (temp & KSZ9477_PORTn_MSTP_STATE_RECEIVE_EN) != 0 &&
         (temp & KSZ9477_PORTn_MSTP_STATE_LEARNING_DIS) == 0)
      {
         //Forwarding state
         state = SWITCH_PORT_STATE_FORWARDING;
      }
      else
      {
         //Unknown state
         state = SWITCH_PORT_STATE_UNKNOWN;
      }
   }
   else
   {
      //The specified port number is not valid
      state = SWITCH_PORT_STATE_DISABLED;
   }

   //Return port state
   return state;
}


/**
 * @brief Set aging time for dynamic filtering entries
 * @param[in] interface Underlying network interface
 * @param[in] agingTime Aging time, in seconds
 **/

void ksz9477SetAgingTime(NetInterface *interface, uint32_t agingTime)
{
   //The Age Period in combination with the Age Count field determines the
   //aging time of dynamic entries in the address lookup table
   agingTime = (agingTime + 3) / 4;

   //Limit the range of the parameter
   agingTime = MIN(agingTime, 255);

   //Write the value to Switch Lookup Engine Control 3 register
   ksz9477WriteSwitchReg8(interface, KSZ9477_SWITCH_LUE_CTRL3,
      (uint8_t) agingTime);
}


/**
 * @brief Enable IGMP snooping
 * @param[in] interface Underlying network interface
 * @param[in] enable Enable or disable IGMP snooping
 **/

void ksz9477EnableIgmpSnooping(NetInterface *interface, bool_t enable)
{
   uint8_t temp;

   //Read the Global Port Mirroring and Snooping Control register
   temp = ksz9477ReadSwitchReg8(interface,
      KSZ9477_GLOBAL_PORT_MIRROR_SNOOP_CTRL);

   //Enable or disable IGMP snooping
   if(enable)
   {
      temp |= KSZ9477_GLOBAL_PORT_MIRROR_SNOOP_CTRL_IGMP_SNOOP_EN;
   }
   else
   {
      temp &= ~KSZ9477_GLOBAL_PORT_MIRROR_SNOOP_CTRL_IGMP_SNOOP_EN;
   }

   //Write the value back to Global Port Mirroring and Snooping Control register
   ksz9477WriteSwitchReg8(interface, KSZ9477_GLOBAL_PORT_MIRROR_SNOOP_CTRL,
      temp);
}


/**
 * @brief Enable MLD snooping
 * @param[in] interface Underlying network interface
 * @param[in] enable Enable or disable MLD snooping
 **/

void ksz9477EnableMldSnooping(NetInterface *interface, bool_t enable)
{
   uint8_t temp;

   //Read the Global Port Mirroring and Snooping Control register
   temp = ksz9477ReadSwitchReg8(interface,
      KSZ9477_GLOBAL_PORT_MIRROR_SNOOP_CTRL);

   //Enable or disable MLD snooping
   if(enable)
   {
      temp |= KSZ9477_GLOBAL_PORT_MIRROR_SNOOP_CTRL_MLD_SNOOP_EN;
   }
   else
   {
      temp &= ~KSZ9477_GLOBAL_PORT_MIRROR_SNOOP_CTRL_MLD_SNOOP_EN;
   }

   //Write the value back to Global Port Mirroring and Snooping Control register
   ksz9477WriteSwitchReg8(interface, KSZ9477_GLOBAL_PORT_MIRROR_SNOOP_CTRL,
      temp);
}


/**
 * @brief Enable reserved multicast table
 * @param[in] interface Underlying network interface
 * @param[in] enable Enable or disable reserved group addresses
 **/

void ksz9477EnableRsvdMcastTable(NetInterface *interface, bool_t enable)
{
   uint8_t temp;

   //Read the Switch Lookup Engine Control 0 register
   temp = ksz9477ReadSwitchReg8(interface, KSZ9477_SWITCH_LUE_CTRL0);

   //Enable or disable the reserved multicast table
   if(enable)
   {
      temp |= KSZ9477_SWITCH_LUE_CTRL0_RESERVED_MCAST_LOOKUP_EN;
   }
   else
   {
      temp &= ~KSZ9477_SWITCH_LUE_CTRL0_RESERVED_MCAST_LOOKUP_EN;
   }

   //Write the value back to Switch Lookup Engine Control 0 register
   ksz9477WriteSwitchReg8(interface, KSZ9477_SWITCH_LUE_CTRL0, temp);
}


/**
 * @brief Add a new entry to the static MAC table
 * @param[in] interface Underlying network interface
 * @param[in] entry Pointer to the forwarding database entry
 * @return Error code
 **/

error_t ksz9477AddStaticFdbEntry(NetInterface *interface,
   const SwitchFdbEntry *entry)
{
   error_t error;
   uint_t i;
   uint_t j;
   uint32_t value;
   SwitchFdbEntry currentEntry;

   //Keep track of the first free entry
   j = KSZ9477_STATIC_MAC_TABLE_SIZE;

   //Loop through the static MAC table
   for(i = 0; i < KSZ9477_STATIC_MAC_TABLE_SIZE; i++)
   {
      //Read current entry
      error = ksz9477GetStaticFdbEntry(interface, i, &currentEntry);

      //Valid entry?
      if(!error)
      {
         //Check whether the table already contains the specified MAC address
         if(macCompAddr(&currentEntry.macAddr, &entry->macAddr))
         {
            j = i;
            break;
         }
      }
      else
      {
         //Keep track of the first free entry
         if(j == KSZ9477_STATIC_MAC_TABLE_SIZE)
         {
            j = i;
         }
      }
   }

   //Any entry available?
   if(j < KSZ9477_STATIC_MAC_TABLE_SIZE)
   {
      //Write the Static Address Table Entry 1 register
      ksz9477WriteSwitchReg32(interface, KSZ9477_STATIC_TABLE_ENTRY1,
         KSZ9477_STATIC_TABLE_ENTRY1_VALID);

      //Set the relevant forward ports
      if(entry->destPorts == SWITCH_CPU_PORT_MASK)
      {
         value = KSZ9477_PORT6_MASK;
      }
      else
      {
         value = entry->destPorts & KSZ9477_PORT_MASK;
      }

      //Enable overriding of port state
      if(entry->override)
      {
         value |= KSZ9477_STATIC_TABLE_ENTRY2_OVERRIDE;
      }

      //Write the Static Address Table Entry 2 register
      ksz9477WriteSwitchReg32(interface, KSZ9477_STATIC_TABLE_ENTRY2, value);

      //Copy MAC address (first 16 bits)
      value = (entry->macAddr.b[0] << 8) | entry->macAddr.b[1];

      //Write the Static Address Table Entry 3 register
      ksz9477WriteSwitchReg32(interface, KSZ9477_STATIC_TABLE_ENTRY3, value);

      //Copy MAC address (last 32 bits)
      value = (entry->macAddr.b[2] << 24) | (entry->macAddr.b[3] << 16) |
         (entry->macAddr.b[4] << 8) | entry->macAddr.b[5];

      //Write the Static Address Table Entry 4 register
      ksz9477WriteSwitchReg32(interface, KSZ9477_STATIC_TABLE_ENTRY4, value);

      //Write the TABLE_INDEX field with the 4-bit index value
      value = (j << 16) & KSZ9477_STATIC_MCAST_TABLE_CTRL_TABLE_INDEX;
      //Set the TABLE_SELECT bit to 0 to select the static address table
      value &= ~KSZ9477_STATIC_MCAST_TABLE_CTRL_TABLE_SELECT;
      //Set the ACTION bit to 0 to indicate a write operation
      value &= ~KSZ9477_STATIC_MCAST_TABLE_CTRL_ACTION;
      //Set the START_FINISH bit to 1 to initiate the operation
      value |= KSZ9477_STATIC_MCAST_TABLE_CTRL_START_FINISH;

      //Start the write operation
      ksz9477WriteSwitchReg32(interface, KSZ9477_STATIC_MCAST_TABLE_CTRL,
         value);

      //When the operation is complete, the START_FINISH bit will be cleared
      //automatically
      do
      {
         //Read the Static Address and Reserved Multicast Table Control register
         value = ksz9477ReadSwitchReg32(interface,
            KSZ9477_STATIC_MCAST_TABLE_CTRL);

         //Poll the START_FINISH bit
      } while((value & KSZ9477_STATIC_MCAST_TABLE_CTRL_START_FINISH) != 0);

      //Successful processing
      error = NO_ERROR;
   }
   else
   {
      //The static MAC table is full
      error = ERROR_TABLE_FULL;
   }

   //Return status code
   return error;
}


/**
 * @brief Remove an entry from the static MAC table
 * @param[in] interface Underlying network interface
 * @param[in] entry Forwarding database entry to remove from the table
 * @return Error code
 **/

error_t ksz9477DeleteStaticFdbEntry(NetInterface *interface,
   const SwitchFdbEntry *entry)
{
   error_t error;
   uint_t j;
   uint32_t value;
   SwitchFdbEntry currentEntry;

   //Loop through the static MAC table
   for(j = 0; j < KSZ9477_STATIC_MAC_TABLE_SIZE; j++)
   {
      //Read current entry
      error = ksz9477GetStaticFdbEntry(interface, j, &currentEntry);

      //Valid entry?
      if(!error)
      {
         //Check whether the table contains the specified MAC address
         if(macCompAddr(&currentEntry.macAddr, &entry->macAddr))
         {
            break;
         }
      }
   }

   //Any matching entry?
   if(j < KSZ9477_STATIC_MAC_TABLE_SIZE)
   {
      //Clear Static Address Table Entry registers
      ksz9477WriteSwitchReg32(interface, KSZ9477_STATIC_TABLE_ENTRY1, 0);
      ksz9477WriteSwitchReg32(interface, KSZ9477_STATIC_TABLE_ENTRY2, 0);
      ksz9477WriteSwitchReg32(interface, KSZ9477_STATIC_TABLE_ENTRY3, 0);
      ksz9477WriteSwitchReg32(interface, KSZ9477_STATIC_TABLE_ENTRY4, 0);

      //Write the TABLE_INDEX field with the 4-bit index value
      value = (j << 16) & KSZ9477_STATIC_MCAST_TABLE_CTRL_TABLE_INDEX;
      //Set the TABLE_SELECT bit to 0 to select the static address table
      value &= ~KSZ9477_STATIC_MCAST_TABLE_CTRL_TABLE_SELECT;
      //Set the ACTION bit to 0 to indicate a write operation
      value &= ~KSZ9477_STATIC_MCAST_TABLE_CTRL_ACTION;
      //Set the START_FINISH bit to 1 to initiate the operation
      value |= KSZ9477_STATIC_MCAST_TABLE_CTRL_START_FINISH;

      //Start the write operation
      ksz9477WriteSwitchReg32(interface, KSZ9477_STATIC_MCAST_TABLE_CTRL,
         value);

      //When the operation is complete, the START_FINISH bit will be cleared
      //automatically
      do
      {
         //Read the Static Address and Reserved Multicast Table Control register
         value = ksz9477ReadSwitchReg32(interface,
            KSZ9477_STATIC_MCAST_TABLE_CTRL);

         //Poll the START_FINISH bit
      } while((value & KSZ9477_STATIC_MCAST_TABLE_CTRL_START_FINISH) != 0);

      //Successful processing
      error = NO_ERROR;
   }
   else
   {
      //The static MAC table does not contain the specified address
      error = ERROR_NOT_FOUND;
   }

   //Return status code
   return error;
}


/**
 * @brief Read an entry from the static MAC table
 * @param[in] interface Underlying network interface
 * @param[in] index Zero-based index of the entry to read
 * @param[out] entry Pointer to the forwarding database entry
 * @return Error code
 **/

error_t ksz9477GetStaticFdbEntry(NetInterface *interface, uint_t index,
   SwitchFdbEntry *entry)
{
   error_t error;
   uint32_t value;

   //Check index parameter
   if(index < KSZ9477_STATIC_MAC_TABLE_SIZE)
   {
      //Write the TABLE_INDEX field with the 4-bit index value
      value = (index << 16) & KSZ9477_STATIC_MCAST_TABLE_CTRL_TABLE_INDEX;
      //Set the TABLE_SELECT bit to 0 to select the static address table
      value &= ~KSZ9477_STATIC_MCAST_TABLE_CTRL_TABLE_SELECT;
      //Set the ACTION bit to 1 to indicate a read operation
      value |= KSZ9477_STATIC_MCAST_TABLE_CTRL_ACTION;
      //Set the START_FINISH bit to 1 to initiate the operation
      value |= KSZ9477_STATIC_MCAST_TABLE_CTRL_START_FINISH;

      //Start the read operation
      ksz9477WriteSwitchReg32(interface, KSZ9477_STATIC_MCAST_TABLE_CTRL,
         value);

      //When the operation is complete, the START_FINISH bit will be cleared
      //automatically
      do
      {
         //Read the Static Address and Reserved Multicast Table Control register
         value = ksz9477ReadSwitchReg32(interface,
            KSZ9477_STATIC_MCAST_TABLE_CTRL);

         //Poll the START_FINISH bit
      } while((value & KSZ9477_STATIC_MCAST_TABLE_CTRL_START_FINISH) != 0);

      //Read the Static Address Table Entry 1 register
      value = ksz9477ReadSwitchReg32(interface, KSZ9477_STATIC_TABLE_ENTRY1);

      //Valid entry?
      if((value & KSZ9477_STATIC_TABLE_ENTRY1_VALID) != 0)
      {
         //Read the Static Address Table Entry 2 register
         value = ksz9477ReadSwitchReg32(interface, KSZ9477_STATIC_TABLE_ENTRY2);

         //Retrieve the ports associated with this MAC address
         entry->srcPort = 0;
         entry->destPorts = value & KSZ9477_STATIC_TABLE_ENTRY2_PORT_FORWARD;

         //Check the value of the OVERRIDE bit
         if((value & KSZ9477_STATIC_TABLE_ENTRY2_OVERRIDE) != 0)
         {
            entry->override = TRUE;
         }
         else
         {
            entry->override = FALSE;
         }

         //Read the Static Address Table Entry 3 register
         value = ksz9477ReadSwitchReg32(interface, KSZ9477_STATIC_TABLE_ENTRY3);

         //Copy MAC address (first 16 bits)
         entry->macAddr.b[0] = (value >> 8) & 0xFF;
         entry->macAddr.b[1] = value & 0xFF;

         //Read the Static Address Table Entry 4 register
         value = ksz9477ReadSwitchReg32(interface, KSZ9477_STATIC_TABLE_ENTRY4);

         //Copy MAC address (last 32 bits)
         entry->macAddr.b[2] = (value >> 24) & 0xFF;
         entry->macAddr.b[3] = (value >> 16) & 0xFF;
         entry->macAddr.b[4] = (value >> 8) & 0xFF;
         entry->macAddr.b[5] = value & 0xFF;

         //Successful processing
         error = NO_ERROR;
      }
      else
      {
         //The entry is not valid
         error = ERROR_INVALID_ENTRY;
      }
   }
   else
   {
      //The end of the table has been reached
      error = ERROR_END_OF_TABLE;
   }

   //Return status code
   return error;
}


/**
 * @brief Flush static MAC table
 * @param[in] interface Underlying network interface
 **/

void ksz9477FlushStaticFdbTable(NetInterface *interface)
{
   uint_t i;
   uint32_t value;

   //Loop through the static MAC table
   for(i = 0; i < KSZ9477_STATIC_MAC_TABLE_SIZE; i++)
   {
      //Clear Static Address Table Entry registers
      ksz9477WriteSwitchReg32(interface, KSZ9477_STATIC_TABLE_ENTRY1, 0);
      ksz9477WriteSwitchReg32(interface, KSZ9477_STATIC_TABLE_ENTRY2, 0);
      ksz9477WriteSwitchReg32(interface, KSZ9477_STATIC_TABLE_ENTRY3, 0);
      ksz9477WriteSwitchReg32(interface, KSZ9477_STATIC_TABLE_ENTRY4, 0);

      //Write the TABLE_INDEX field with the 4-bit index value
      value = (i << 16) & KSZ9477_STATIC_MCAST_TABLE_CTRL_TABLE_INDEX;
      //Set the TABLE_SELECT bit to 0 to select the static address table
      value &= ~KSZ9477_STATIC_MCAST_TABLE_CTRL_TABLE_SELECT;
      //Set the ACTION bit to 0 to indicate a write operation
      value &= ~KSZ9477_STATIC_MCAST_TABLE_CTRL_ACTION;
      //Set the START_FINISH bit to 1 to initiate the operation
      value |= KSZ9477_STATIC_MCAST_TABLE_CTRL_START_FINISH;

      //Start the write operation
      ksz9477WriteSwitchReg32(interface, KSZ9477_STATIC_MCAST_TABLE_CTRL,
         value);

      //When the operation is complete, the START_FINISH bit will be cleared
      //automatically
      do
      {
         //Read the Static Address and Reserved Multicast Table Control register
         value = ksz9477ReadSwitchReg32(interface,
            KSZ9477_STATIC_MCAST_TABLE_CTRL);

         //Poll the START_FINISH bit
      } while((value & KSZ9477_STATIC_MCAST_TABLE_CTRL_START_FINISH) != 0);
   }
}


/**
 * @brief Read an entry from the dynamic MAC table
 * @param[in] interface Underlying network interface
 * @param[in] index Zero-based index of the entry to read
 * @param[out] entry Pointer to the forwarding database entry
 * @return Error code
 **/

error_t ksz9477GetDynamicFdbEntry(NetInterface *interface, uint_t index,
   SwitchFdbEntry *entry)
{
   error_t error;
   uint32_t value;

   //First entry?
   if(index == 0)
   {
      //Clear the ALU Table Access Control register to stop any operation
      ksz9477WriteSwitchReg32(interface, KSZ9477_ALU_TABLE_CTRL, 0);

      //Start the search operation
      ksz9477WriteSwitchReg32(interface, KSZ9477_ALU_TABLE_CTRL,
         KSZ9477_ALU_TABLE_CTRL_START_FINISH |
         KSZ9477_ALU_TABLE_CTRL_ACTION_SEARCH);
   }

   //Poll the VALID_ENTRY_OR_SEARCH_END bit until it is set
   do
   {
      //Read the ALU Table Access Control register
      value = ksz9477ReadSwitchReg32(interface, KSZ9477_ALU_TABLE_CTRL);

      //This bit goes high to indicate either a new valid entry is returned or
      //the search is complete
   } while((value & KSZ9477_ALU_TABLE_CTRL_VALID_ENTRY_OR_SEARCH_END) == 0);

   //Check whether the next valid entry is ready
   if((value & KSZ9477_ALU_TABLE_CTRL_VALID) != 0)
   {
      //Store the data from the ALU table entry
      entry->destPorts = 0;
      entry->override = FALSE;

      //Read the ALU Table Entry 1 and 2 registers
      value = ksz9477ReadSwitchReg32(interface, KSZ9477_ALU_TABLE_ENTRY1);
      value = ksz9477ReadSwitchReg32(interface, KSZ9477_ALU_TABLE_ENTRY2);

      //Retrieve the port associated with this MAC address
      switch(value & KSZ9477_ALU_TABLE_ENTRY2_PORT_FORWARD)
      {
      case KSZ9477_ALU_TABLE_ENTRY2_PORT1_FORWARD:
         entry->srcPort = KSZ9477_PORT1;
         break;
      case KSZ9477_ALU_TABLE_ENTRY2_PORT2_FORWARD:
         entry->srcPort = KSZ9477_PORT2;
         break;
      case KSZ9477_ALU_TABLE_ENTRY2_PORT3_FORWARD:
         entry->srcPort = KSZ9477_PORT3;
         break;
      case KSZ9477_ALU_TABLE_ENTRY2_PORT4_FORWARD:
         entry->srcPort = KSZ9477_PORT4;
         break;
      case KSZ9477_ALU_TABLE_ENTRY2_PORT5_FORWARD:
         entry->srcPort = KSZ9477_PORT5;
         break;
      case KSZ9477_ALU_TABLE_ENTRY2_PORT6_FORWARD:
         entry->srcPort = KSZ9477_PORT6;
         break;
      case KSZ9477_ALU_TABLE_ENTRY2_PORT7_FORWARD:
         entry->srcPort = KSZ9477_PORT7;
         break;
      default:
         entry->srcPort = 0;
         break;
      }

      //Read the ALU Table Entry 3 register
      value = ksz9477ReadSwitchReg32(interface, KSZ9477_ALU_TABLE_ENTRY3);

      //Copy MAC address (first 16 bits)
      entry->macAddr.b[0] = (value >> 8) & 0xFF;
      entry->macAddr.b[1] = value & 0xFF;

      //Read the ALU Table Entry 4 register
      value = ksz9477ReadSwitchReg32(interface, KSZ9477_ALU_TABLE_ENTRY4);

      //Copy MAC address (last 32 bits)
      entry->macAddr.b[2] = (value >> 24) & 0xFF;
      entry->macAddr.b[3] = (value >> 16) & 0xFF;
      entry->macAddr.b[4] = (value >> 8) & 0xFF;
      entry->macAddr.b[5] = value & 0xFF;

      //Successful processing
      error = NO_ERROR;
   }
   else
   {
      //The search can be stopped any time by setting the START_FINISH bit to 0
      ksz9477WriteSwitchReg32(interface, KSZ9477_ALU_TABLE_CTRL, 0);

      //The end of the table has been reached
      error = ERROR_END_OF_TABLE;
   }

   //Return status code
   return error;
}


/**
 * @brief Flush dynamic MAC table
 * @param[in] interface Underlying network interface
 * @param[in] port Port number
 **/

void ksz9477FlushDynamicFdbTable(NetInterface *interface, uint8_t port)
{
   uint_t temp;
   uint8_t state;

   //Flush only dynamic table entries
   temp = ksz9477ReadSwitchReg8(interface, KSZ9477_SWITCH_LUE_CTRL2);
   temp &= ~KSZ9477_SWITCH_LUE_CTRL2_FLUSH_OPTION;
   temp |= KSZ9477_SWITCH_LUE_CTRL2_FLUSH_OPTION_DYNAMIC;
   ksz9477WriteSwitchReg8(interface, KSZ9477_SWITCH_LUE_CTRL2, temp);

   //Valid port number?
   if(port >= KSZ9477_PORT1 && port <= KSZ9477_PORT7)
   {
      //Save the current state of the port
      state = ksz9477ReadSwitchReg8(interface, KSZ9477_PORTn_MSTP_STATE(port));

      //Turn off learning capability
      ksz9477WriteSwitchReg8(interface, KSZ9477_PORTn_MSTP_STATE(port),
         state | KSZ9477_PORTn_MSTP_STATE_LEARNING_DIS);

      //All the entries associated with a port that has its learning capability
      //being turned off will be flushed
      temp = ksz9477ReadSwitchReg8(interface, KSZ9477_SWITCH_LUE_CTRL1);
      temp |= KSZ9477_SWITCH_LUE_CTRL1_FLUSH_MSTP_ENTRIES;
      ksz9477WriteSwitchReg8(interface, KSZ9477_SWITCH_LUE_CTRL1, temp);

      //Restore the original state of the port
      ksz9477WriteSwitchReg8(interface, KSZ9477_PORTn_MSTP_STATE(port), state);
   }
   else
   {
      //Trigger a flush of the entire address lookup table
      temp = ksz9477ReadSwitchReg8(interface, KSZ9477_SWITCH_LUE_CTRL1);
      temp |= KSZ9477_SWITCH_LUE_CTRL1_FLUSH_ALU_TABLE;
      ksz9477WriteSwitchReg8(interface, KSZ9477_SWITCH_LUE_CTRL1, temp);
   }
}


/**
 * @brief Set forward ports for unknown multicast packets
 * @param[in] interface Underlying network interface
 * @param[in] enable Enable or disable forwarding of unknown multicast packets
 * @param[in] forwardPorts Port map
 **/

void ksz9477SetUnknownMcastFwdPorts(NetInterface *interface,
   bool_t enable, uint32_t forwardPorts)
{
   uint32_t temp;

   //Read Unknown Multicast Control register
   temp = ksz9477ReadSwitchReg32(interface, KSZ9477_UNKONWN_MULTICAST_CTRL);

   //Clear port map
   temp &= ~KSZ9477_UNKONWN_MULTICAST_CTRL_FWD_MAP;

   //Enable or disable forwarding of unknown multicast packets
   if(enable)
   {
      //Enable forwarding
      temp |= KSZ9477_UNKONWN_MULTICAST_CTRL_FWD;

      //Check whether unknown multicast packets should be forwarded to the CPU port
      if((forwardPorts & SWITCH_CPU_PORT_MASK) != 0)
      {
         temp |= KSZ9477_UNKONWN_MULTICAST_CTRL_FWD_MAP_PORT6;
      }

      //Select the desired forward ports
      temp |= forwardPorts & KSZ9477_UNKONWN_MULTICAST_CTRL_FWD_MAP_ALL;
   }
   else
   {
      //Disable forwarding
      temp &= ~KSZ9477_UNKONWN_MULTICAST_CTRL_FWD;
   }

   //Write the value back to Unknown Multicast Control register
   ksz9477WriteSwitchReg32(interface, KSZ9477_UNKONWN_MULTICAST_CTRL, temp);
}


/**
 * @brief Write PHY register
 * @param[in] interface Underlying network interface
 * @param[in] port Port number
 * @param[in] address PHY register address
 * @param[in] data Register value
 **/

void ksz9477WritePhyReg(NetInterface *interface, uint8_t port,
   uint8_t address, uint16_t data)
{
   uint16_t n;

   //SPI slave mode?
   if(interface->spiDriver != NULL)
   {
      //The SPI interface provides access to all PHY registers
      n = KSZ9477_PORTn_ETH_PHY_REG(port, address);
      //Write the 16-bit value
      ksz9477WriteSwitchReg16(interface, n, data);
   }
   else if(interface->smiDriver != NULL)
   {
      //Write the specified PHY register
      interface->smiDriver->writePhyReg(SMI_OPCODE_WRITE, port, address, data);
   }
   else
   {
      //Write the specified PHY register
      interface->nicDriver->writePhyReg(SMI_OPCODE_WRITE, port, address, data);
   }
}


/**
 * @brief Read PHY register
 * @param[in] interface Underlying network interface
 * @param[in] port Port number
 * @param[in] address PHY register address
 * @return Register value
 **/

uint16_t ksz9477ReadPhyReg(NetInterface *interface, uint8_t port,
   uint8_t address)
{
   uint16_t n;
   uint16_t data;

   //SPI slave mode?
   if(interface->spiDriver != NULL)
   {
      //The SPI interface provides access to all PHY registers
      n = KSZ9477_PORTn_ETH_PHY_REG(port, address);
      //Read the 16-bit value
      data = ksz9477ReadSwitchReg16(interface, n);
   }
   else if(interface->smiDriver != NULL)
   {
      //Read the specified PHY register
      data = interface->smiDriver->readPhyReg(SMI_OPCODE_READ, port, address);
   }
   else
   {
      //Read the specified PHY register
      data = interface->nicDriver->readPhyReg(SMI_OPCODE_READ, port, address);
   }

   //Return register value
   return data;
}


/**
 * @brief Dump PHY registers for debugging purpose
 * @param[in] interface Underlying network interface
 * @param[in] port Port number
 **/

void ksz9477DumpPhyReg(NetInterface *interface, uint8_t port)
{
   uint8_t i;

   //Loop through PHY registers
   for(i = 0; i < 32; i++)
   {
      //Display current PHY register
      TRACE_DEBUG("%02" PRIu8 ": 0x%04" PRIX16 "\r\n", i,
         ksz9477ReadPhyReg(interface, port, i));
   }

   //Terminate with a line feed
   TRACE_DEBUG("\r\n");
}


/**
 * @brief Write MMD register
 * @param[in] interface Underlying network interface
 * @param[in] port Port number
 * @param[in] devAddr Device address
 * @param[in] regAddr Register address
 * @param[in] data Register value
 **/

void ksz9477WriteMmdReg(NetInterface *interface, uint8_t port,
   uint8_t devAddr, uint16_t regAddr, uint16_t data)
{
   //Select register operation
   ksz9477WritePhyReg(interface, port, KSZ9477_MMDACR,
      KSZ9477_MMDACR_FUNC_ADDR | (devAddr & KSZ9477_MMDACR_DEVAD));

   //Write MMD register address
   ksz9477WritePhyReg(interface, port, KSZ9477_MMDAADR, regAddr);

   //Select data operation
   ksz9477WritePhyReg(interface, port, KSZ9477_MMDACR,
      KSZ9477_MMDACR_FUNC_DATA_NO_POST_INC | (devAddr & KSZ9477_MMDACR_DEVAD));

   //Write the content of the MMD register
   ksz9477WritePhyReg(interface, port, KSZ9477_MMDAADR, data);
}


/**
 * @brief Read MMD register
 * @param[in] interface Underlying network interface
 * @param[in] port Port number
 * @param[in] devAddr Device address
 * @param[in] regAddr Register address
 * @return Register value
 **/

uint16_t ksz9477ReadMmdReg(NetInterface *interface, uint8_t port,
   uint8_t devAddr, uint16_t regAddr)
{
   //Select register operation
   ksz9477WritePhyReg(interface, port, KSZ9477_MMDACR,
      KSZ9477_MMDACR_FUNC_ADDR | (devAddr & KSZ9477_MMDACR_DEVAD));

   //Write MMD register address
   ksz9477WritePhyReg(interface, port, KSZ9477_MMDAADR, regAddr);

   //Select data operation
   ksz9477WritePhyReg(interface, port, KSZ9477_MMDACR,
      KSZ9477_MMDACR_FUNC_DATA_NO_POST_INC | (devAddr & KSZ9477_MMDACR_DEVAD));

   //Read the content of the MMD register
   return ksz9477ReadPhyReg(interface, port, KSZ9477_MMDAADR);
}


/**
 * @brief Write switch register (8 bits)
 * @param[in] interface Underlying network interface
 * @param[in] address Switch register address
 * @param[in] data Register value
 **/

void ksz9477WriteSwitchReg8(NetInterface *interface, uint16_t address,
   uint8_t data)
{
   uint32_t command;

   //SPI slave mode?
   if(interface->spiDriver != NULL)
   {
      //Set up a write operation
      command = KSZ9477_SPI_CMD_WRITE;
      //Set register address
      command |= (address << 5) & KSZ9477_SPI_CMD_ADDR;

      //Pull the CS pin low
      interface->spiDriver->assertCs();

      //Write 32-bit command
      interface->spiDriver->transfer((command >> 24) & 0xFF);
      interface->spiDriver->transfer((command >> 16) & 0xFF);
      interface->spiDriver->transfer((command >> 8) & 0xFF);
      interface->spiDriver->transfer(command & 0xFF);

      //Write 8-bit data
      interface->spiDriver->transfer(data);

      //Terminate the operation by raising the CS pin
      interface->spiDriver->deassertCs();
   }
   else
   {
      //The MDC/MDIO interface does not have access to all the configuration
      //registers. It can only access the standard MIIM registers
   }
}


/**
 * @brief Read switch register (8 bits)
 * @param[in] interface Underlying network interface
 * @param[in] address Switch register address
 * @return Register value
 **/

uint8_t ksz9477ReadSwitchReg8(NetInterface *interface, uint16_t address)
{
   uint8_t data;
   uint32_t command;

   //SPI slave mode?
   if(interface->spiDriver != NULL)
   {
      //Set up a read operation
      command = KSZ9477_SPI_CMD_READ;
      //Set register address
      command |= (address << 5) & KSZ9477_SPI_CMD_ADDR;

      //Pull the CS pin low
      interface->spiDriver->assertCs();

      //Write 32-bit command
      interface->spiDriver->transfer((command >> 24) & 0xFF);
      interface->spiDriver->transfer((command >> 16) & 0xFF);
      interface->spiDriver->transfer((command >> 8) & 0xFF);
      interface->spiDriver->transfer(command & 0xFF);

      //Read 8-bit data
      data = interface->spiDriver->transfer(0xFF);

      //Terminate the operation by raising the CS pin
      interface->spiDriver->deassertCs();
   }
   else
   {
      //The MDC/MDIO interface does not have access to all the configuration
      //registers. It can only access the standard MIIM registers
      data = 0;
   }

   //Return register value
   return data;
}


/**
 * @brief Write switch register (16 bits)
 * @param[in] interface Underlying network interface
 * @param[in] address Switch register address
 * @param[in] data Register value
 **/

void ksz9477WriteSwitchReg16(NetInterface *interface, uint16_t address,
   uint16_t data)
{
   uint32_t command;

   //SPI slave mode?
   if(interface->spiDriver != NULL)
   {
      //Set up a write operation
      command = KSZ9477_SPI_CMD_WRITE;
      //Set register address
      command |= (address << 5) & KSZ9477_SPI_CMD_ADDR;

      //Pull the CS pin low
      interface->spiDriver->assertCs();

      //Write 32-bit command
      interface->spiDriver->transfer((command >> 24) & 0xFF);
      interface->spiDriver->transfer((command >> 16) & 0xFF);
      interface->spiDriver->transfer((command >> 8) & 0xFF);
      interface->spiDriver->transfer(command & 0xFF);

      //Write 16-bit data
      interface->spiDriver->transfer((data >> 8) & 0xFF);
      interface->spiDriver->transfer(data & 0xFF);

      //Terminate the operation by raising the CS pin
      interface->spiDriver->deassertCs();
   }
   else
   {
      //The MDC/MDIO interface does not have access to all the configuration
      //registers. It can only access the standard MIIM registers
   }
}


/**
 * @brief Read switch register (16 bits)
 * @param[in] interface Underlying network interface
 * @param[in] address Switch register address
 * @return Register value
 **/

uint16_t ksz9477ReadSwitchReg16(NetInterface *interface, uint16_t address)
{
   uint16_t data;
   uint32_t command;

   //SPI slave mode?
   if(interface->spiDriver != NULL)
   {
      //Set up a read operation
      command = KSZ9477_SPI_CMD_READ;
      //Set register address
      command |= (address << 5) & KSZ9477_SPI_CMD_ADDR;

      //Pull the CS pin low
      interface->spiDriver->assertCs();

      //Write 32-bit command
      interface->spiDriver->transfer((command >> 24) & 0xFF);
      interface->spiDriver->transfer((command >> 16) & 0xFF);
      interface->spiDriver->transfer((command >> 8) & 0xFF);
      interface->spiDriver->transfer(command & 0xFF);

      //Read 16-bit data
      data = interface->spiDriver->transfer(0xFF) << 8;
      data |= interface->spiDriver->transfer(0xFF);

      //Terminate the operation by raising the CS pin
      interface->spiDriver->deassertCs();
   }
   else
   {
      //The MDC/MDIO interface does not have access to all the configuration
      //registers. It can only access the standard MIIM registers
      data = 0;
   }

   //Return register value
   return data;
}


/**
 * @brief Write switch register (32 bits)
 * @param[in] interface Underlying network interface
 * @param[in] address Switch register address
 * @param[in] data Register value
 **/

void ksz9477WriteSwitchReg32(NetInterface *interface, uint16_t address,
   uint32_t data)
{
   uint32_t command;

   //SPI slave mode?
   if(interface->spiDriver != NULL)
   {
      //Set up a write operation
      command = KSZ9477_SPI_CMD_WRITE;
      //Set register address
      command |= (address << 5) & KSZ9477_SPI_CMD_ADDR;

      //Pull the CS pin low
      interface->spiDriver->assertCs();

      //Write 32-bit command
      interface->spiDriver->transfer((command >> 24) & 0xFF);
      interface->spiDriver->transfer((command >> 16) & 0xFF);
      interface->spiDriver->transfer((command >> 8) & 0xFF);
      interface->spiDriver->transfer(command & 0xFF);

      //Write 32-bit data
      interface->spiDriver->transfer((data >> 24) & 0xFF);
      interface->spiDriver->transfer((data >> 16) & 0xFF);
      interface->spiDriver->transfer((data >> 8) & 0xFF);
      interface->spiDriver->transfer(data & 0xFF);

      //Terminate the operation by raising the CS pin
      interface->spiDriver->deassertCs();
   }
   else
   {
      //The MDC/MDIO interface does not have access to all the configuration
      //registers. It can only access the standard MIIM registers
   }
}


/**
 * @brief Read switch register (32 bits)
 * @param[in] interface Underlying network interface
 * @param[in] address Switch register address
 * @return Register value
 **/

uint32_t ksz9477ReadSwitchReg32(NetInterface *interface, uint16_t address)
{
   uint32_t data;
   uint32_t command;

   //SPI slave mode?
   if(interface->spiDriver != NULL)
   {
      //Set up a read operation
      command = KSZ9477_SPI_CMD_READ;
      //Set register address
      command |= (address << 5) & KSZ9477_SPI_CMD_ADDR;

      //Pull the CS pin low
      interface->spiDriver->assertCs();

      //Write 32-bit command
      interface->spiDriver->transfer((command >> 24) & 0xFF);
      interface->spiDriver->transfer((command >> 16) & 0xFF);
      interface->spiDriver->transfer((command >> 8) & 0xFF);
      interface->spiDriver->transfer(command & 0xFF);

      //Read 32-bit data
      data = interface->spiDriver->transfer(0xFF) << 24;
      data |= interface->spiDriver->transfer(0xFF) << 16;
      data |= interface->spiDriver->transfer(0xFF) << 8;
      data |= interface->spiDriver->transfer(0xFF);

      //Terminate the operation by raising the CS pin
      interface->spiDriver->deassertCs();
   }
   else
   {
      //The MDC/MDIO interface does not have access to all the configuration
      //registers. It can only access the standard MIIM registers
      data = 0;
   }

   //Return register value
   return data;
}
