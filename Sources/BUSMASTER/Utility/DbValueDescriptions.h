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
 * \file      DbValueDescriptions.h
 * \brief     BUSMASTER-NG: value descriptions of signals, read from the associated CAN databases
 *
 * Since BUSMASTER 3.0 the database engine (DBManager.dll, closed source) no longer returns the value descriptions
 * of signals ([VALUE_DESCRIPTION] in the .dbf, VAL_ in a DBC), so the Message window showed "3" instead of
 * "Position control" (rbei-etas/busmaster issues 1075, 1133, 1171, 1025). The descriptions are read here from the
 * .dbf files of the associated databases and put in place of the physical value, as BUSMASTER 2.x did.
 *
 * Header only: every module that includes it keeps its own cache. Thread safe.
 */
#pragma once

#include <windows.h>
#include <cstdlib>
#include <fstream>
#include <list>
#include <map>
#include <string>
#include "IBMNetWorkGetService.h"

class CDbValueDescriptions
{
public:
    /**
     * Replaces the physical value of the interpreted signals of message unMsgId with their value description, when
     * the database has one for the raw value. The unit is removed for those signals (it describes the number).
     */
    static void vApply(IBMNetWorkGetService* pouNetwork, ETYPE_BUS eBus, unsigned int unMsgId,
                       std::list<InterpreteSignals>& lstSignals)
    {
        if ((nullptr == pouNetwork) || lstSignals.empty())
        {
            return;
        }
        CDbValueDescriptions& ouThis = ouGet();
        EnterCriticalSection(&ouThis.m_sLock);
        std::list<ICluster*> lstClusters;
        pouNetwork->GetDBServiceList(eBus, 0, lstClusters);
        for (ICluster* pouCluster : lstClusters)
        {
            std::string strPath;
            if (nullptr == pouCluster)
            {
                continue;
            }
            // the result code is not used: DBManager fills the path but returns EC_NA
            pouCluster->GetDBFilePath(strPath);
            if (strPath.empty())
            {
                continue;
            }
            const sDATABASE& sDb = ouThis.sGetDatabase(strPath);
            auto itMsg = sDb.m_mapMessages.find(unMsgId & 0x1FFFFFFF);
            if (itMsg == sDb.m_mapMessages.end())
            {
                continue;
            }
            for (InterpreteSignals& ouSignal : lstSignals)
            {
                auto itSig = itMsg->second.find(ouSignal.m_omSigName);
                if (itSig == itMsg->second.end())
                {
                    continue;
                }
                std::string strDesc;
                if (bFindDescription(itSig->second, ouSignal.m_omRawValue, strDesc))
                {
                    ouSignal.m_omEnggValue = strDesc;
                    ouSignal.m_omUnit = "";
                }
            }
            break;  // the first database that knows the message wins, as for the interpretation
        }
        LeaveCriticalSection(&ouThis.m_sLock);
    }

private:
    struct sSIGNAL
    {
        unsigned int m_unLength = 0;
        std::map<long long, std::string> m_mapDescriptions;
    };
    typedef std::map<std::string, sSIGNAL> SIGNALMAP;
    struct sDATABASE
    {
        FILETIME m_sWriteTime;
        std::map<unsigned int, SIGNALMAP> m_mapMessages;
        // set in the constructor: the Visual Studio 2013 compiler of the Windows 7 build has no brace initializer here
        sDATABASE()
        {
            m_sWriteTime.dwLowDateTime = 0;
            m_sWriteTime.dwHighDateTime = 0;
        }
    };

    CRITICAL_SECTION m_sLock;
    std::map<std::string, sDATABASE> m_mapDatabases;

    CDbValueDescriptions()
    {
        InitializeCriticalSection(&m_sLock);
    }
    ~CDbValueDescriptions()
    {
        DeleteCriticalSection(&m_sLock);
    }

    static CDbValueDescriptions& ouGet()
    {
        static CDbValueDescriptions s_ouInstance;
        return s_ouInstance;
    }

    /* The raw value as shown by the interpretation ("3", "-1" or, in hex mode, "0x3") compared with the descriptions;
       for signed signals a hex raw value is sign-extended to the length of the signal. */
    static bool bFindDescription(const sSIGNAL& sSignal, const std::string& strRaw, std::string& strDesc)
    {
        if (sSignal.m_mapDescriptions.empty() || strRaw.empty())
        {
            return false;
        }
        char* pcEnd = nullptr;
        bool bHex = (strRaw.size() > 2) && (strRaw[0] == '0') && ((strRaw[1] == 'x') || (strRaw[1] == 'X'));
        long long nValue = 0;
        if (bHex)
        {
            unsigned long long unValue = _strtoui64(strRaw.c_str() + 2, &pcEnd, 16);
            nValue = (long long)unValue;
            auto it = sSignal.m_mapDescriptions.find(nValue);
            if (it != sSignal.m_mapDescriptions.end())
            {
                strDesc = it->second;
                return true;
            }
            if ((sSignal.m_unLength > 0) && (sSignal.m_unLength < 64) && (unValue >> (sSignal.m_unLength - 1)) & 1)
            {
                nValue = (long long)(unValue | (~0ULL << sSignal.m_unLength));     // negative value
            }
        }
        else
        {
            nValue = _strtoi64(strRaw.c_str(), &pcEnd, 10);
        }
        if ((nullptr == pcEnd) || (pcEnd == strRaw.c_str()))
        {
            return false;
        }
        auto it = sSignal.m_mapDescriptions.find(nValue);
        if (it == sSignal.m_mapDescriptions.end())
        {
            return false;
        }
        strDesc = it->second;
        return true;
    }

    /* The descriptions of a database, read again when the file has changed (for example saved by the editor) */
    const sDATABASE& sGetDatabase(const std::string& strPath)
    {
        WIN32_FILE_ATTRIBUTE_DATA sAttributes;
        FILETIME sWriteTime = { 0, 0 };
        if (GetFileAttributesExA(strPath.c_str(), GetFileExInfoStandard, &sAttributes))
        {
            sWriteTime = sAttributes.ftLastWriteTime;
        }
        auto it = m_mapDatabases.find(strPath);
        if ((it != m_mapDatabases.end()) && (0 == CompareFileTime(&it->second.m_sWriteTime, &sWriteTime)))
        {
            return it->second;
        }
        sDATABASE& sDb = m_mapDatabases[strPath];
        sDb.m_sWriteTime = sWriteTime;
        sDb.m_mapMessages.clear();
        vReadDatabase(strPath, sDb);
        return sDb;
    }

    static std::string strField(const std::string& strLine, size_t nIndex)
    {
        size_t nStart = 0;
        for (size_t nField = 0; nField < nIndex; nField++)
        {
            nStart = strLine.find(',', nStart);
            if (std::string::npos == nStart)
            {
                return "";
            }
            nStart++;
        }
        size_t nEnd = strLine.find(',', nStart);
        return strLine.substr(nStart, (std::string::npos == nEnd) ? std::string::npos : nEnd - nStart);
    }

    /* [START_MSG] <name>,<id>,...   [START_SIGNALS] <name>,<length>,...   [VALUE_DESCRIPTION] "<text>",<value>
       (or <text>,<value> without quotes, as old databases and the database editor write it) */
    static void vReadDatabase(const std::string& strPath, sDATABASE& sDb)
    {
        std::ifstream ouFile(strPath.c_str());
        std::string strLine;
        const std::string strMsgTag = "[START_MSG] ";
        const std::string strSigTag = "[START_SIGNALS] ";
        const std::string strDescTag = "[VALUE_DESCRIPTION] ";
        SIGNALMAP* pMessage = nullptr;
        sSIGNAL* pSignal = nullptr;
        bool bSkipSection = false;  // not supported messages and other sections are not used
        while (std::getline(ouFile, strLine))
        {
            if (!strLine.empty() && (strLine.back() == '\r'))
            {
                strLine.pop_back();
            }
            if ((0 == strLine.compare(0, 21, "[START_NOT_SUPPORTED]")) || (0 == strLine.compare(0, 19, "[START_VALUE_TABLE]"))
                    || (0 == strLine.compare(0, 16, "[START_SIG_LIST]")))
            {
                bSkipSection = true;
            }
            else if ((0 == strLine.compare(0, 19, "[END_NOT_SUPPORTED]")) || (0 == strLine.compare(0, 17, "[END_VALUE_TABLE]"))
                     || (0 == strLine.compare(0, 14, "[END_SIG_LIST]")))
            {
                bSkipSection = false;
            }
            else if (bSkipSection)
            {
                continue;
            }
            else if (0 == strLine.compare(0, strMsgTag.size(), strMsgTag))
            {
                std::string strBody = strLine.substr(strMsgTag.size());
                unsigned int unId = (unsigned int)strtoul(strField(strBody, 1).c_str(), nullptr, 10);
                pMessage = &sDb.m_mapMessages[unId & 0x1FFFFFFF];
                pSignal = nullptr;
            }
            else if ((nullptr != pMessage) && (0 == strLine.compare(0, strSigTag.size(), strSigTag)))
            {
                std::string strBody = strLine.substr(strSigTag.size());
                pSignal = &(*pMessage)[strField(strBody, 0)];
                pSignal->m_unLength = (unsigned int)strtoul(strField(strBody, 1).c_str(), nullptr, 10);
            }
            else if ((nullptr != pSignal) && (0 == strLine.compare(0, strDescTag.size(), strDescTag)))
            {
                std::string strBody = strLine.substr(strDescTag.size());
                size_t nComma = strBody.rfind(',');
                if (std::string::npos == nComma)
                {
                    continue;
                }
                std::string strText = strBody.substr(0, nComma);
                if ((strText.size() >= 2) && (strText.front() == '"') && (strText.back() == '"'))
                {
                    strText = strText.substr(1, strText.size() - 2);
                }
                long long nValue = _strtoi64(strBody.c_str() + nComma + 1, nullptr, 10);
                pSignal->m_mapDescriptions[nValue] = strText;
            }
            else if (0 == strLine.compare(0, 9, "[END_MSG]"))
            {
                pMessage = nullptr;
                pSignal = nullptr;
            }
        }
    }
};
