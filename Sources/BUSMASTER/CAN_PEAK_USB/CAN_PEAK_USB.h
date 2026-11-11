/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file      CAN_PEAK_USB.h
 * \brief     BUSMASTER-NG: PEAK CAN driver built on PCAN-Basic (PCANBasic.dll)
 */

#pragma once

#ifndef __AFXWIN_H__
#error include 'CAN_PEAK_USB_stdafx.h' before including this file for PCH
#endif

#include "DIL_Interface/HardwareListingCAN.h"

/* Baud rate check of the hardware selection dialog: PCAN-Basic takes any rate an SJA1000 at 16 MHz can make */
class CPeakCanBaudRateCheck : public IChangeRegisters
{
public:
    int InvokeAdavancedSettings(PSCONTROLLER_DETAILS pControllerDetails, UINT nCount, UINT nSelected);
    double vValidateBaudRate(double dBaudRate, int nItem, UINT unClock = 16);
};

class CCAN_PEAK_USB : public CWinApp
{
public:
    CCAN_PEAK_USB();

public:
    virtual BOOL InitInstance();

    DECLARE_MESSAGE_MAP()
};
