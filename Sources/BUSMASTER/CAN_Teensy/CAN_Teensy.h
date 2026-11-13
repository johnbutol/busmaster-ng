/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file  CAN_Teensy.h
 * @brief CAN driver for a Teensy 4.0 with the BUSMASTER-NG CAN firmware (Firmware/Teensy4_CAN).
 */

#pragma once

#ifndef __AFXWIN_H__
#error include 'stdafx.h' before including this file for PCH
#endif

class CCAN_Teensy : public CWinApp
{
public:
    CCAN_Teensy();
    virtual BOOL InitInstance();

    DECLARE_MESSAGE_MAP()
};
