/**
 * @file msp432e4_eth_driver.c
 * @brief MSP432E4 Ethernet controller
 *
 * @section License
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2010-2023 Oryx Embedded SARL. All rights reserved.
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
 * @version 2.2.4
 **/

//Switch to the appropriate trace level
#define TRACE_LEVEL NIC_TRACE_LEVEL

//Dependencies
#include <stdint.h>
#include "msp432.h"
#include "inc/hw_emac.h"
#include "driverlib/gpio.h"
#include "driverlib/interrupt.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "core/net.h"
#include "drivers/mac/msp432e4_eth_driver.h"
#include "debug.h"

//Underlying network interface
static NetInterface *nicDriverInterface;

//IAR EWARM compiler?
#if defined(__ICCARM__)

//Transmit buffer
#pragma data_alignment = 4
static uint8_t txBuffer[MSP432E4_ETH_TX_BUFFER_COUNT][MSP432E4_ETH_TX_BUFFER_SIZE];
//Receive buffer
#pragma data_alignment = 4
static uint8_t rxBuffer[MSP432E4_ETH_RX_BUFFER_COUNT][MSP432E4_ETH_RX_BUFFER_SIZE];
//Transmit DMA descriptors
#pragma data_alignment = 4
static Msp432e4TxDmaDesc txDmaDesc[MSP432E4_ETH_TX_BUFFER_COUNT];
//Receive DMA descriptors
#pragma data_alignment = 4
static Msp432e4RxDmaDesc rxDmaDesc[MSP432E4_ETH_RX_BUFFER_COUNT];

//Keil MDK-ARM or GCC compiler?
#else

//Transmit buffer
static uint8_t txBuffer[MSP432E4_ETH_TX_BUFFER_COUNT][MSP432E4_ETH_TX_BUFFER_SIZE]
   __attribute__((aligned(4)));
//Receive buffer
static uint8_t rxBuffer[MSP432E4_ETH_RX_BUFFER_COUNT][MSP432E4_ETH_RX_BUFFER_SIZE]
   __attribute__((aligned(4)));
//Transmit DMA descriptors
static Msp432e4TxDmaDesc txDmaDesc[MSP432E4_ETH_TX_BUFFER_COUNT]
   __attribute__((aligned(4)));
//Receive DMA descriptors
static Msp432e4RxDmaDesc rxDmaDesc[MSP432E4_ETH_RX_BUFFER_COUNT]
   __attribute__((aligned(4)));

#endif

//Pointer to the current TX DMA descriptor
static Msp432e4TxDmaDesc *txCurDmaDesc;
//Pointer to the current RX DMA descriptor
static Msp432e4RxDmaDesc *rxCurDmaDesc;


/**
 * @brief MSP432E4 Ethernet MAC driver
 **/

const NicDriver msp432e4EthDriver =
{
   NIC_TYPE_ETHERNET,
   ETH_MTU,
   msp432e4EthInit,
   msp432e4EthTick,
   msp432e4EthEnableIrq,
   msp432e4EthDisableIrq,
   msp432e4EthEventHandler,
   msp432e4EthSendPacket,
   msp432e4EthUpdateMacAddrFilter,
   msp432e4EthUpdateMacConfig,
   msp432e4EthWritePhyReg,
   msp432e4EthReadPhyReg,
   TRUE,
   TRUE,
   TRUE,
   FALSE
};


/**
 * @brief MSP432E4 Ethernet MAC initialization
 * @param[in] interface Underlying network interface
 * @return Error code
 **/

error_t msp432e4EthInit(NetInterface *interface)
{
   error_t error;
#ifdef ti_sysbios_BIOS___VERS
   Hwi_Params hwiParams;
#endif

   //Debug message
   TRACE_INFO("Initializing MSP432E4 Ethernet controller...\r\n");

   //Save underlying network interface
   nicDriverInterface = interface;

   //Enable Ethernet controller clock
   SysCtlPeripheralEnable(SYSCTL_PERIPH_EMAC0);

   //Reset Ethernet controller
   SysCtlPeripheralReset(SYSCTL_PERIPH_EMAC0);
   //Wait for the reset to complete
   while(!SysCtlPeripheralReady(SYSCTL_PERIPH_EMAC0))
   {
   }

   //Enable internal PHY clock
   SysCtlPeripheralEnable(SYSCTL_PERIPH_EPHY0);

   //Reset internal PHY
   SysCtlPeripheralReset(SYSCTL_PERIPH_EPHY0);
   //Wait for the reset to complete
   while(!SysCtlPeripheralReady(SYSCTL_PERIPH_EPHY0))
   {
   }

   //GPIO configuration
   msp432e4EthInitGpio(interface);

   //Perform a software reset
   EMAC0->DMABUSMOD |= EMAC_DMABUSMOD_SWR;
   //Wait for the reset to complete
   while((EMAC0->DMABUSMOD & EMAC_DMABUSMOD_SWR) != 0)
   {
   }

   //Adjust MDC clock range depending on SYSCLK frequency
   EMAC0->MIIADDR = EMAC_MIIADDR_CR_100_150;

   //Internal or external Ethernet PHY?
   if(interface->phyDriver != NULL)
   {
      //Ethernet PHY initialization
      error = interface->phyDriver->init(interface);
   }
   else if(interface->switchDriver != NULL)
   {
      //Ethernet switch initialization
      error = interface->switchDriver->init(interface);
   }
   else
   {
      //Reset internal PHY transceiver
      msp432e4EthWritePhyReg(SMI_OPCODE_WRITE, 0, EPHY_BMCR,
         EPHY_BMCR_MIIRESET);

      //Wait for the reset to complete
      while(msp432e4EthReadPhyReg(SMI_OPCODE_READ, 0, EPHY_BMCR) &
         EPHY_BMCR_MIIRESET)
      {
      }

      //Dump PHY registers for debugging purpose
      msp432e4EthDumpPhyReg();

      //Configure LED0, LED1 and LED2
      msp432e4EthWritePhyReg(SMI_OPCODE_WRITE, 0, EPHY_LEDCFG,
         EPHY_LEDCFG_LED0_TX | EPHY_LEDCFG_LED1_RX | EPHY_LEDCFG_LED2_LINK);

      //Configure PHY interrupts as desired
      msp432e4EthWritePhyReg(SMI_OPCODE_WRITE, 0, EPHY_MISR1,
         EPHY_MISR1_LINKSTATEN);

      //Enable PHY interrupts
      msp432e4EthWritePhyReg(SMI_OPCODE_WRITE, 0, EPHY_SCR, EPHY_SCR_INTEN);

      //The internal Ethernet PHY is initialized
      error = NO_ERROR;
   }

   //Any error to report?
   if(error)
   {
      return error;
   }

   //Use default MAC configuration
   EMAC0->CFG = EMAC_CFG_DRO;

   //Set the MAC address of the station
   EMAC0->ADDR0L = interface->macAddr.w[0] | (interface->macAddr.w[1] << 16);
   EMAC0->ADDR0H = interface->macAddr.w[2];

   //The MAC supports 3 additional addresses for unicast perfect filtering
   EMAC0->ADDR1L = 0;
   EMAC0->ADDR1H = 0;
   EMAC0->ADDR2L = 0;
   EMAC0->ADDR2H = 0;
   EMAC0->ADDR3L = 0;
   EMAC0->ADDR3H = 0;

   //Initialize hash table
   EMAC0->HASHTBLL = 0;
   EMAC0->HASHTBLH = 0;

   //Configure the receive filter
   EMAC0->FRAMEFLTR = EMAC_FRAMEFLTR_HPF | EMAC_FRAMEFLTR_HMC;
   //Disable flow control
   EMAC0->FLOWCTL = 0;
   //Enable store and forward mode
   EMAC0->DMAOPMODE = EMAC_DMAOPMODE_RSF | EMAC_DMAOPMODE_TSF;

   //Configure DMA bus mode
   EMAC0->DMABUSMOD = EMAC_DMABUSMOD_AAL | EMAC_DMABUSMOD_USP |
      EMAC_DMABUSMOD_RPBL_1 | EMAC_DMABUSMOD_PR_1_1 | EMAC_DMABUSMOD_PBL_1 |
      EMAC_DMABUSMOD_ATDS;

   //Initialize DMA descriptor lists
   msp432e4EthInitDmaDesc(interface);

   //Prevent interrupts from being generated when the transmit statistic
   //counters reach half their maximum value
   EMAC0->MMCTXIM = EMAC_MMCTXIM_OCTCNT | EMAC_MMCTXIM_MCOLLGF |
      EMAC_MMCTXIM_SCOLLGF | EMAC_MMCTXIM_GBF;

   //Prevent interrupts from being generated when the receive statistic
   //counters reach half their maximum value
   EMAC0->MMCRXIM = EMAC_MMCRXIM_UCGF | EMAC_MMCRXIM_ALGNERR |
      EMAC_MMCRXIM_CRCERR | EMAC_MMCRXIM_GBF;

   //Disable MAC interrupts
   EMAC0->IM = EMAC_IM_TSI | EMAC_IM_PMT;
   //Enable the desired DMA interrupts
   EMAC0->DMAIM = EMAC_DMAIM_NIE | EMAC_DMAIM_RIE | EMAC_DMAIM_TIE;
   //Enable PHY interrupts
   EMAC0->EPHYIM = EMAC_EPHYIM_INT;

#ifdef ti_sysbios_BIOS___VERS
   //Configure Ethernet interrupt
   Hwi_Params_init(&hwiParams);
   hwiParams.enableInt = FALSE;
   hwiParams.priority = MSP432E4_ETH_IRQ_PRIORITY;

   //Register interrupt handler
   Hwi_create(INT_EMAC0, (Hwi_FuncPtr) msp432e4EthIrqHandler, &hwiParams, NULL);
#else
   //Set priority grouping (3 bits for pre-emption priority, no bits for subpriority)
   IntPriorityGroupingSet(MSP432E4_ETH_IRQ_PRIORITY_GROUPING);
   //Configure Ethernet interrupt priority
   IntPrioritySet(INT_EMAC0, MSP432E4_ETH_IRQ_PRIORITY);
#endif

   //Enable MAC transmission and reception
   EMAC0->CFG |= EMAC_CFG_TE | EMAC_CFG_RE;
   //Enable DMA transmission and reception
   EMAC0->DMAOPMODE |= EMAC_DMAOPMODE_ST | EMAC_DMAOPMODE_SR;

   //Accept any packets from the upper layer
   osSetEvent(&interface->nicTxEvent);

   //Successful initialization
   return NO_ERROR;
}


/**
 * @brief GPIO configuration
 * @param[in] interface Underlying network interface
 **/

__weak_func void msp432e4EthInitGpio(NetInterface *interface)
{
//MSP-EXP432E401Y evaluation board?
#if defined(USE_MSP_EXP432E401Y)
   //Enable GPIO clock
   SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);

   //Select the relevant alternate function for PF0 and PF4
   GPIOPinConfigure(GPIO_PF0_EN0LED0);
   GPIOPinConfigure(GPIO_PF4_EN0LED1);

   //Configure Ethernet LED pins for proper operation
   GPIOPinTypeEthernetLED(GPIO_PORTF_BASE, GPIO_PIN_0 | GPIO_PIN_4);
#endif
}


/**
 * @brief Initialize DMA descriptor lists
 * @param[in] interface Underlying network interface
 **/

void msp432e4EthInitDmaDesc(NetInterface *interface)
{
   uint_t i;

   //Initialize TX DMA descriptor list
   for(i = 0; i < MSP432E4_ETH_TX_BUFFER_COUNT; i++)
   {
      //Use chain structure rather than ring structure
      txDmaDesc[i].tdes0 = EMAC_TDES0_IC | EMAC_TDES0_TCH;
      //Initialize transmit buffer size
      txDmaDesc[i].tdes1 = 0;
      //Transmit buffer address
      txDmaDesc[i].tdes2 = (uint32_t) txBuffer[i];
      //Next descriptor address
      txDmaDesc[i].tdes3 = (uint32_t) &txDmaDesc[i + 1];
      //Reserved fields
      txDmaDesc[i].tdes4 = 0;
      txDmaDesc[i].tdes5 = 0;
      //Transmit frame time stamp
      txDmaDesc[i].tdes6 = 0;
      txDmaDesc[i].tdes7 = 0;
   }

   //The last descriptor is chained to the first entry
   txDmaDesc[i - 1].tdes3 = (uint32_t) &txDmaDesc[0];
   //Point to the very first descriptor
   txCurDmaDesc = &txDmaDesc[0];

   //Initialize RX DMA descriptor list
   for(i = 0; i < MSP432E4_ETH_RX_BUFFER_COUNT; i++)
   {
      //The descriptor is initially owned by the DMA
      rxDmaDesc[i].rdes0 = EMAC_RDES0_OWN;
      //Use chain structure rather than ring structure
      rxDmaDesc[i].rdes1 = EMAC_RDES1_RCH | (MSP432E4_ETH_RX_BUFFER_SIZE & EMAC_RDES1_RBS1);
      //Receive buffer address
      rxDmaDesc[i].rdes2 = (uint32_t) rxBuffer[i];
      //Next descriptor address
      rxDmaDesc[i].rdes3 = (uint32_t) &rxDmaDesc[i + 1];
      //Extended status
      rxDmaDesc[i].rdes4 = 0;
      //Reserved field
      rxDmaDesc[i].rdes5 = 0;
      //Receive frame time stamp
      rxDmaDesc[i].rdes6 = 0;
      rxDmaDesc[i].rdes7 = 0;
   }

   //The last descriptor is chained to the first entry
   rxDmaDesc[i - 1].rdes3 = (uint32_t) &rxDmaDesc[0];
   //Point to the very first descriptor
   rxCurDmaDesc = &rxDmaDesc[0];

   //Start location of the TX descriptor list
   EMAC0->TXDLADDR = (uint32_t) txDmaDesc;
   //Start location of the RX descriptor list
   EMAC0->RXDLADDR = (uint32_t) rxDmaDesc;
}


/**
 * @brief MSP432E4 Ethernet MAC timer handler
 *
 * This routine is periodically called by the TCP/IP stack to handle periodic
 * operations such as polling the link state
 *
 * @param[in] interface Underlying network interface
 **/

void msp432e4EthTick(NetInterface *interface)
{
   //Valid Ethernet PHY or switch driver?
   if(interface->phyDriver != NULL)
   {
      //Handle periodic operations
      interface->phyDriver->tick(interface);
   }
   else if(interface->switchDriver != NULL)
   {
      //Handle periodic operations
      interface->switchDriver->tick(interface);
   }
   else
   {
      //Just for sanity
   }
}


/**
 * @brief Enable interrupts
 * @param[in] interface Underlying network interface
 **/

void msp432e4EthEnableIrq(NetInterface *interface)
{
#ifdef ti_sysbios_BIOS___VERS
   //Enable Ethernet MAC interrupts
   Hwi_enableInterrupt(INT_EMAC0);
#else
   //Enable Ethernet MAC interrupts
   IntEnable(INT_EMAC0);
#endif

   //Valid Ethernet PHY or switch driver?
   if(interface->phyDriver != NULL)
   {
      //Enable Ethernet PHY interrupts
      interface->phyDriver->enableIrq(interface);
   }
   else if(interface->switchDriver != NULL)
   {
      //Enable Ethernet switch interrupts
      interface->switchDriver->enableIrq(interface);
   }
   else
   {
      //Just for sanity
   }
}


/**
 * @brief Disable interrupts
 * @param[in] interface Underlying network interface
 **/

void msp432e4EthDisableIrq(NetInterface *interface)
{
#ifdef ti_sysbios_BIOS___VERS
   //Disable Ethernet MAC interrupts
   Hwi_disableInterrupt(INT_EMAC0);
#else
   //Disable Ethernet MAC interrupts
   IntDisable(INT_EMAC0);
#endif

   //Valid Ethernet PHY or switch driver?
   if(interface->phyDriver != NULL)
   {
      //Disable Ethernet PHY interrupts
      interface->phyDriver->disableIrq(interface);
   }
   else if(interface->switchDriver != NULL)
   {
      //Disable Ethernet switch interrupts
      interface->switchDriver->disableIrq(interface);
   }
   else
   {
      //Just for sanity
   }
}


/**
 * @brief MSP432E4 Ethernet MAC interrupt service routine
 **/

void msp432e4EthIrqHandler(void)
{
   bool_t flag;
   uint32_t status;

   //Interrupt service routine prologue
   osEnterIsr();

   //This flag will be set if a higher priority task must be woken
   flag = FALSE;

   //Read PHY status register
   status = EMAC0->EPHYRIS;

   //PHY interrupt?
   if((status & EMAC_EPHYRIS_INT) != 0)
   {
      //Disable PHY interrupt
      EMAC0->EPHYIM &= ~EMAC_EPHYIM_INT;

      //Set event flag
      nicDriverInterface->nicEvent = TRUE;
      //Notify the TCP/IP stack of the event
      flag |= osSetEventFromIsr(&netEvent);
   }

   //Read DMA status register
   status = EMAC0->DMARIS;

   //Packet transmitted?
   if((status & EMAC_DMARIS_TI) != 0)
   {
      //Clear TI interrupt flag
      EMAC0->DMARIS = EMAC_DMARIS_TI;

      //Check whether the TX buffer is available for writing
      if((txCurDmaDesc->tdes0 & EMAC_TDES0_OWN) == 0)
      {
         //Notify the TCP/IP stack that the transmitter is ready to send
         flag |= osSetEventFromIsr(&nicDriverInterface->nicTxEvent);
      }
   }

   //Packet received?
   if((status & EMAC_DMARIS_RI) != 0)
   {
      //Disable RIE interrupt
      EMAC0->DMAIM &= ~EMAC_DMAIM_RIE;

      //Set event flag
      nicDriverInterface->nicEvent = TRUE;
      //Notify the TCP/IP stack of the event
      flag |= osSetEventFromIsr(&netEvent);
   }

   //Clear NIS interrupt flag
   EMAC0->DMARIS = EMAC_DMARIS_NIS;

   //Interrupt service routine epilogue
   osExitIsr(flag);
}


/**
 * @brief MSP432E4 Ethernet MAC event handler
 * @param[in] interface Underlying network interface
 **/

void msp432e4EthEventHandler(NetInterface *interface)
{
   error_t error;
   uint32_t status;

   //PHY interrupt?
   if((EMAC0->EPHYRIS & EMAC_EPHYRIS_INT) != 0)
   {
      //Clear PHY interrupt flag
      EMAC0->EPHYMISC = EMAC_EPHYMISC_INT;

      //Internal or external Ethernet PHY?
      if(interface->phyDriver != NULL)
      {
         //Handle events
         interface->phyDriver->eventHandler(interface);
      }
      else if(interface->switchDriver != NULL)
      {
         //Handle events
         interface->switchDriver->eventHandler(interface);
      }
      else
      {
         //Read PHY interrupt status register
         status = msp432e4EthReadPhyReg(SMI_OPCODE_READ, 0, EPHY_MISR1);

         //Check whether the link state has changed?
         if((status & EPHY_MISR1_LINKSTAT) != 0)
         {
            //Read BMSR register
            status = msp432e4EthReadPhyReg(SMI_OPCODE_READ, 0, EPHY_BMSR);

            //Check whether the link is up
            if((status & EPHY_BMSR_LINKSTAT) != 0)
            {
               //Read PHY status register
               status = msp432e4EthReadPhyReg(SMI_OPCODE_READ, 0, EPHY_STS);

               //Check current speed
               if((status & EPHY_STS_SPEED) != 0)
               {
                  interface->linkSpeed = NIC_LINK_SPEED_10MBPS;
               }
               else
               {
                  interface->linkSpeed = NIC_LINK_SPEED_100MBPS;
               }

               //Check duplex mode
               if((status & EPHY_STS_DUPLEX) != 0)
               {
                  interface->duplexMode = NIC_FULL_DUPLEX_MODE;
               }
               else
               {
                  interface->duplexMode = NIC_HALF_DUPLEX_MODE;
               }

               //Update link state
               interface->linkState = TRUE;

               //Adjust MAC configuration parameters for proper operation
               msp432e4EthUpdateMacConfig(interface);
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
   }

   //Packet received?
   if((EMAC0->DMARIS & EMAC_DMARIS_RI) != 0)
   {
      //Clear interrupt flag
      EMAC0->DMARIS = EMAC_DMARIS_RI;

      //Process all pending packets
      do
      {
         //Read incoming packet
         error = msp432e4EthReceivePacket(interface);

         //No more data in the receive buffer?
      } while(error != ERROR_BUFFER_EMPTY);
   }

   //Re-enable DMA interrupts
   EMAC0->DMAIM = EMAC_DMAIM_NIE | EMAC_DMAIM_RIE | EMAC_DMAIM_TIE;
   //Re-enable PHY interrupts
   EMAC0->EPHYIM = EMAC_EPHYIM_INT;
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

error_t msp432e4EthSendPacket(NetInterface *interface,
   const NetBuffer *buffer, size_t offset, NetTxAncillary *ancillary)
{
   size_t length;

   //Retrieve the length of the packet
   length = netBufferGetLength(buffer) - offset;

   //Check the frame length
   if(length > MSP432E4_ETH_TX_BUFFER_SIZE)
   {
      //The transmitter can accept another packet
      osSetEvent(&interface->nicTxEvent);
      //Report an error
      return ERROR_INVALID_LENGTH;
   }

   //Make sure the current buffer is available for writing
   if((txCurDmaDesc->tdes0 & EMAC_TDES0_OWN) != 0)
   {
      return ERROR_FAILURE;
   }

   //Copy user data to the transmit buffer
   netBufferRead((uint8_t *) txCurDmaDesc->tdes2, buffer, offset, length);

   //Write the number of bytes to send
   txCurDmaDesc->tdes1 = length & EMAC_TDES1_TBS1;
   //Set LS and FS flags as the data fits in a single buffer
   txCurDmaDesc->tdes0 |= EMAC_TDES0_LS | EMAC_TDES0_FS;
   //Give the ownership of the descriptor to the DMA
   txCurDmaDesc->tdes0 |= EMAC_TDES0_OWN;

   //Clear TU flag to resume processing
   EMAC0->DMARIS = EMAC_DMARIS_TU;
   //Instruct the DMA to poll the transmit descriptor list
   EMAC0->TXPOLLD = 0;

   //Point to the next descriptor in the list
   txCurDmaDesc = (Msp432e4TxDmaDesc *) txCurDmaDesc->tdes3;

   //Check whether the next buffer is available for writing
   if((txCurDmaDesc->tdes0 & EMAC_TDES0_OWN) == 0)
   {
      //The transmitter can accept another packet
      osSetEvent(&interface->nicTxEvent);
   }

   //Data successfully written
   return NO_ERROR;
}


/**
 * @brief Receive a packet
 * @param[in] interface Underlying network interface
 * @return Error code
 **/

error_t msp432e4EthReceivePacket(NetInterface *interface)
{
   error_t error;
   size_t n;
   NetRxAncillary ancillary;

   //Current buffer available for reading?
   if((rxCurDmaDesc->rdes0 & EMAC_RDES0_OWN) == 0)
   {
      //FS and LS flags should be set
      if((rxCurDmaDesc->rdes0 & EMAC_RDES0_FS) != 0 &&
         (rxCurDmaDesc->rdes0 & EMAC_RDES0_LS) != 0)
      {
         //Make sure no error occurred
         if((rxCurDmaDesc->rdes0 & EMAC_RDES0_ES) == 0)
         {
            //Retrieve the length of the frame
            n = (rxCurDmaDesc->rdes0 & EMAC_RDES0_FL) >> 16;
            //Limit the number of data to read
            n = MIN(n, MSP432E4_ETH_RX_BUFFER_SIZE);

            //Additional options can be passed to the stack along with the packet
            ancillary = NET_DEFAULT_RX_ANCILLARY;

            //Pass the packet to the upper layer
            nicProcessPacket(interface, (uint8_t *) rxCurDmaDesc->rdes2, n,
               &ancillary);

            //Valid packet received
            error = NO_ERROR;
         }
         else
         {
            //The received packet contains an error
            error = ERROR_INVALID_PACKET;
         }
      }
      else
      {
         //The packet is not valid
         error = ERROR_INVALID_PACKET;
      }

      //Give the ownership of the descriptor back to the DMA
      rxCurDmaDesc->rdes0 = EMAC_RDES0_OWN;
      //Point to the next descriptor in the list
      rxCurDmaDesc = (Msp432e4RxDmaDesc *) rxCurDmaDesc->rdes3;
   }
   else
   {
      //No more data in the receive buffer
      error = ERROR_BUFFER_EMPTY;
   }

   //Clear RU flag to resume processing
   EMAC0->DMARIS = EMAC_DMARIS_RU;
   //Instruct the DMA to poll the receive descriptor list
   EMAC0->RXPOLLD = 0;

   //Return status code
   return error;
}


/**
 * @brief Configure MAC address filtering
 * @param[in] interface Underlying network interface
 * @return Error code
 **/

error_t msp432e4EthUpdateMacAddrFilter(NetInterface *interface)
{
   uint_t i;
   uint_t j;
   uint_t k;
   uint32_t crc;
   uint32_t hashTable[2];
   MacAddr unicastMacAddr[3];
   MacFilterEntry *entry;

   //Debug message
   TRACE_DEBUG("Updating MAC filter...\r\n");

   //Set the MAC address of the station
   EMAC0->ADDR0L = interface->macAddr.w[0] | (interface->macAddr.w[1] << 16);
   EMAC0->ADDR0H = interface->macAddr.w[2];

   //The MAC supports 3 additional addresses for unicast perfect filtering
   unicastMacAddr[0] = MAC_UNSPECIFIED_ADDR;
   unicastMacAddr[1] = MAC_UNSPECIFIED_ADDR;
   unicastMacAddr[2] = MAC_UNSPECIFIED_ADDR;

   //The hash table is used for multicast address filtering
   hashTable[0] = 0;
   hashTable[1] = 0;

   //The MAC address filter contains the list of MAC addresses to accept
   //when receiving an Ethernet frame
   for(i = 0, j = 0; i < MAC_ADDR_FILTER_SIZE; i++)
   {
      //Point to the current entry
      entry = &interface->macAddrFilter[i];

      //Valid entry?
      if(entry->refCount > 0)
      {
         //Multicast address?
         if(macIsMulticastAddr(&entry->addr))
         {
            //Compute CRC over the current MAC address
            crc = msp432e4EthCalcCrc(&entry->addr, sizeof(MacAddr));

            //The upper 6 bits in the CRC register are used to index the
            //contents of the hash table
            k = (crc >> 26) & 0x3F;

            //Update hash table contents
            hashTable[k / 32] |= (1 << (k % 32));
         }
         else
         {
            //Up to 3 additional MAC addresses can be specified
            if(j < 3)
            {
               //Save the unicast address
               unicastMacAddr[j++] = entry->addr;
            }
         }
      }
   }

   //Configure the first unicast address filter
   if(j >= 1)
   {
      //When the AE bit is set, the entry is used for perfect filtering
      EMAC0->ADDR1L = unicastMacAddr[0].w[0] | (unicastMacAddr[0].w[1] << 16);
      EMAC0->ADDR1H = unicastMacAddr[0].w[2] | EMAC_ADDR1H_AE;
   }
   else
   {
      //When the AE bit is cleared, the entry is ignored
      EMAC0->ADDR1L = 0;
      EMAC0->ADDR1H = 0;
   }

   //Configure the second unicast address filter
   if(j >= 2)
   {
      //When the AE bit is set, the entry is used for perfect filtering
      EMAC0->ADDR2L = unicastMacAddr[1].w[0] | (unicastMacAddr[1].w[1] << 16);
      EMAC0->ADDR2H = unicastMacAddr[1].w[2] | EMAC_ADDR2H_AE;
   }
   else
   {
      //When the AE bit is cleared, the entry is ignored
      EMAC0->ADDR2L = 0;
      EMAC0->ADDR2H = 0;
   }

   //Configure the third unicast address filter
   if(j >= 3)
   {
      //When the AE bit is set, the entry is used for perfect filtering
      EMAC0->ADDR3L = unicastMacAddr[2].w[0] | (unicastMacAddr[2].w[1] << 16);
      EMAC0->ADDR3H = unicastMacAddr[2].w[2] | EMAC_ADDR3H_AE;
   }
   else
   {
      //When the AE bit is cleared, the entry is ignored
      EMAC0->ADDR3L = 0;
      EMAC0->ADDR3H = 0;
   }

   //Configure the multicast hash table
   EMAC0->HASHTBLL = hashTable[0];
   EMAC0->HASHTBLH = hashTable[1];

   //Debug message
   TRACE_DEBUG("  HASHTBLL = %08" PRIX32 "\r\n", EMAC0->HASHTBLL);
   TRACE_DEBUG("  HASHTBLH = %08" PRIX32 "\r\n", EMAC0->HASHTBLH);

   //Successful processing
   return NO_ERROR;
}


/**
 * @brief Adjust MAC configuration parameters for proper operation
 * @param[in] interface Underlying network interface
 * @return Error code
 **/

error_t msp432e4EthUpdateMacConfig(NetInterface *interface)
{
   uint32_t config;

   //Read current MAC configuration
   config = EMAC0->CFG;

   //10BASE-T or 100BASE-TX operation mode?
   if(interface->linkSpeed == NIC_LINK_SPEED_100MBPS)
   {
      config |= EMAC_CFG_FES;
   }
   else
   {
      config &= ~EMAC_CFG_FES;
   }

   //Half-duplex or full-duplex mode?
   if(interface->duplexMode == NIC_FULL_DUPLEX_MODE)
   {
      config |= EMAC_CFG_DUPM;
   }
   else
   {
      config &= ~EMAC_CFG_DUPM;
   }

   //Update MAC configuration register
   EMAC0->CFG = config;

   //Successful processing
   return NO_ERROR;
}


/**
 * @brief Write PHY register
 * @param[in] opcode Access type (2 bits)
 * @param[in] phyAddr PHY address (5 bits)
 * @param[in] regAddr Register address (5 bits)
 * @param[in] data Register value
 **/

void msp432e4EthWritePhyReg(uint8_t opcode, uint8_t phyAddr,
   uint8_t regAddr, uint16_t data)
{
   uint32_t temp;

   //Valid opcode?
   if(opcode == SMI_OPCODE_WRITE)
   {
      //Take care not to alter MDC clock configuration
      temp = EMAC0->MIIADDR & EMAC_MIIADDR_CR_M;
      //Set up a write operation
      temp |= EMAC_MIIADDR_MIIW | EMAC_MIIADDR_MIIB;
      //PHY address
      temp |= (phyAddr << EMAC_MIIADDR_PLA_S) & EMAC_MIIADDR_PLA_M;
      //Register address
      temp |= (regAddr << EMAC_MIIADDR_MII_S) & EMAC_MIIADDR_MII_M;

      //Data to be written in the PHY register
      EMAC0->MIIDATA = data & EMAC_MIIDATA_DATA_M;

      //Start a write operation
      EMAC0->MIIADDR = temp;
      //Wait for the write to complete
      while((EMAC0->MIIADDR & EMAC_MIIADDR_MIIB) != 0)
      {
      }
   }
   else
   {
      //The MAC peripheral only supports standard Clause 22 opcodes
   }
}


/**
 * @brief Read PHY register
 * @param[in] opcode Access type (2 bits)
 * @param[in] phyAddr PHY address (5 bits)
 * @param[in] regAddr Register address (5 bits)
 * @return Register value
 **/

uint16_t msp432e4EthReadPhyReg(uint8_t opcode, uint8_t phyAddr,
   uint8_t regAddr)
{
   uint16_t data;
   uint32_t temp;

   //Valid opcode?
   if(opcode == SMI_OPCODE_READ)
   {
      //Take care not to alter MDC clock configuration
      temp = EMAC0->MIIADDR & EMAC_MIIADDR_CR_M;
      //Set up a read operation
      temp |= EMAC_MIIADDR_MIIB;
      //PHY address
      temp |= (phyAddr << EMAC_MIIADDR_PLA_S) & EMAC_MIIADDR_PLA_M;
      //Register address
      temp |= (regAddr << EMAC_MIIADDR_MII_S) & EMAC_MIIADDR_MII_M;

      //Start a read operation
      EMAC0->MIIADDR = temp;
      //Wait for the read to complete
      while((EMAC0->MIIADDR & EMAC_MIIADDR_MIIB) != 0)
      {
      }

      //Get register value
      data = EMAC0->MIIDATA & EMAC_MIIDATA_DATA_M;
   }
   else
   {
      //The MAC peripheral only supports standard Clause 22 opcodes
      data = 0;
   }

   //Return the value of the PHY register
   return data;
}


/**
 * @brief Dump PHY registers for debugging purpose
 **/

void msp432e4EthDumpPhyReg(void)
{
   uint8_t i;

   //Loop through PHY registers
   for(i = 0; i < 32; i++)
   {
      //Display current PHY register
      TRACE_DEBUG("%02" PRIu8 ": 0x%04" PRIX16 "\r\n", i,
         msp432e4EthReadPhyReg(SMI_OPCODE_READ, 0, i));
   }

   //Terminate with a line feed
   TRACE_DEBUG("\r\n");
}


/**
 * @brief CRC calculation
 * @param[in] data Pointer to the data over which to calculate the CRC
 * @param[in] length Number of bytes to process
 * @return Resulting CRC value
 **/

uint32_t msp432e4EthCalcCrc(const void *data, size_t length)
{
   uint_t i;
   uint_t j;
   uint32_t crc;
   const uint8_t *p;

   //Point to the data over which to calculate the CRC
   p = (uint8_t *) data;
   //CRC preset value
   crc = 0xFFFFFFFF;

   //Loop through data
   for(i = 0; i < length; i++)
   {
      //The message is processed bit by bit
      for(j = 0; j < 8; j++)
      {
         //Update CRC value
         if((((crc >> 31) ^ (p[i] >> j)) & 0x01) != 0)
         {
            crc = (crc << 1) ^ 0x04C11DB7;
         }
         else
         {
            crc = crc << 1;
         }
      }
   }

   //Return CRC value
   return ~crc;
}
