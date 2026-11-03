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
 * \file      UpdateCheck.cpp
 * \brief     BUSMASTER-NG: Help > Check for Updates
 *
 * WinINet rather than WinHTTP: it uses the proxy settings of Windows (including automatic configuration scripts)
 * and TLS 1.2 also on Windows 7, which GitHub requires.
 */
#include "stdafx.h"
#include <wininet.h>
#include <vector>
#include "../../BusmasterNGVersion.h"
#include "UpdateCheck.h"

#pragma comment(lib, "wininet.lib")

// latest published release: drafts and pre-releases are never returned
#define UPDATE_API_URL      "https://api.github.com/repos/teoCLMR/busmaster-ng/releases/latest"
#define UPDATE_RELEASES_URL "https://github.com/teoCLMR/busmaster-ng/releases"
#define UPDATE_TIMEOUT_MS   10000
#define UPDATE_MAX_REPLY    (1024 * 1024)

/* Reads the release information. On failure omError says why, in words for the user. */
static bool bDownloadLatestRelease(CStringA& omReply, CString& omError)
{
    HINTERNET hInternet = ::InternetOpen(BMNG_PRODUCT_NAME "/" BMNG_VERSION_STRING,
                                         INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (hInternet == nullptr)
    {
        omError.Format("the Internet functions of Windows are not available, error %lu", ::GetLastError());
        return false;
    }
    DWORD dwTimeout = UPDATE_TIMEOUT_MS;
    ::InternetSetOption(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &dwTimeout, sizeof(dwTimeout));
    ::InternetSetOption(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &dwTimeout, sizeof(dwTimeout));
    ::InternetSetOption(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &dwTimeout, sizeof(dwTimeout));

    const char acHeaders[] = "Accept: application/vnd.github+json\r\n";
    HINTERNET hRequest = ::InternetOpenUrl(hInternet, UPDATE_API_URL, acHeaders, (DWORD)strlen(acHeaders),
                                           INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                                           INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_NO_UI, 0);
    bool bResult = false;
    if (hRequest == nullptr)
    {
        DWORD dwError = ::GetLastError();
        if (dwError == ERROR_INTERNET_TIMEOUT)
        {
            omError = "GitHub did not answer in time";
        }
        else
        {
            omError.Format("no connection to GitHub, error %lu", dwError);
        }
    }
    else
    {
        DWORD dwStatus = 0;
        DWORD dwSize = sizeof(dwStatus);
        ::HttpQueryInfo(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &dwStatus, &dwSize, nullptr);
        if (dwStatus == HTTP_STATUS_NOT_FOUND)
        {
            omError = "no release is published on GitHub";
        }
        else if (dwStatus != HTTP_STATUS_OK)
        {
            omError.Format("GitHub answered with HTTP status %lu", dwStatus);
        }
        else
        {
            std::vector<char> acBuffer(16 * 1024);
            DWORD dwRead = 0;
            bResult = true;
            while (::InternetReadFile(hRequest, acBuffer.data(), (DWORD)acBuffer.size(), &dwRead) && dwRead > 0)
            {
                omReply.Append(acBuffer.data(), (int)dwRead);
                if (omReply.GetLength() > UPDATE_MAX_REPLY)
                {
                    break;
                }
            }
            if (omReply.IsEmpty())
            {
                omError = "the answer of GitHub could not be read";
                bResult = false;
            }
        }
        ::InternetCloseHandle(hRequest);
    }
    ::InternetCloseHandle(hInternet);
    return bResult;
}

/* Value of the string field pcField of a JSON object (GitHub never escapes characters in a tag name). */
static CStringA omGetJsonString(const CStringA& omJson, const char* pcField)
{
    CStringA omKey;
    omKey.Format("\"%s\"", pcField);
    int nPos = omJson.Find(omKey);
    if (nPos < 0)
    {
        return "";
    }
    nPos += omKey.GetLength();
    while (nPos < omJson.GetLength() && (isspace((unsigned char)omJson[nPos]) || omJson[nPos] == ':'))
    {
        nPos++;
    }
    if (nPos >= omJson.GetLength() || omJson[nPos] != '"')
    {
        return "";
    }
    int nEnd = omJson.Find('"', nPos + 1);
    return nEnd < 0 ? CStringA() : omJson.Mid(nPos + 1, nEnd - nPos - 1);
}

/* "v3.4.2" or "3.4.2" -> { 3, 4, 2, 0 }. False when the text is not such a version. */
static bool bParseVersion(const CStringA& omText, int anVersion[4])
{
    const char* pcText = omText;
    if (*pcText == 'v' || *pcText == 'V')
    {
        pcText++;
    }
    for (int i = 0; i < 4; i++)
    {
        anVersion[i] = 0;
    }
    for (int i = 0; i < 4; i++)
    {
        if (!isdigit((unsigned char)*pcText))
        {
            return false;
        }
        char* pcEnd = nullptr;
        anVersion[i] = (int)strtol(pcText, &pcEnd, 10);
        pcText = pcEnd;
        if (*pcText != '.')
        {
            return *pcText == '\0';
        }
        pcText++;
    }
    return false;
}

static int nCompareVersions(const int anFirst[4], const int anSecond[4])
{
    for (int i = 0; i < 4; i++)
    {
        if (anFirst[i] != anSecond[i])
        {
            return anFirst[i] < anSecond[i] ? -1 : 1;
        }
    }
    return 0;
}

static void vOpenUrl(const CString& omUrl)
{
    if (::ShellExecute(nullptr, "open", omUrl, nullptr, nullptr, SW_SHOWNORMAL) <= (HINSTANCE)32)
    {
        AfxMessageBox("The browser could not be started. The releases of BUSMASTER-NG are at\n" UPDATE_RELEASES_URL,
                      MB_OK | MB_ICONWARNING);
    }
}

void vCheckForUpdates(CWnd* pParent)
{
    CStringA omReply;
    CString omError;
    bool bDownloaded;
    {
        CWaitCursor omWait;
        bDownloaded = bDownloadLatestRelease(omReply, omError);
    }

    CStringA omTag = omGetJsonString(omReply, "tag_name");
    int anLatest[4], anRunning[4];
    if (bDownloaded && !bParseVersion(omTag, anLatest))
    {
        omError = "GitHub did not give the version of the latest release";
        bDownloaded = false;
    }
    bParseVersion(BMNG_VERSION_STRING, anRunning);

    CString omText;
    if (!bDownloaded)
    {
        omText.Format("Could not check for updates: %s.\n\nOpen the BUSMASTER-NG releases page in the browser?",
                      (LPCTSTR)omError);
        if (::MessageBox(pParent->GetSafeHwnd(), omText, BMNG_PRODUCT_NAME, MB_YESNO | MB_ICONWARNING) == IDYES)
        {
            vOpenUrl(UPDATE_RELEASES_URL);
        }
    }
    else if (nCompareVersions(anLatest, anRunning) > 0)
    {
        CStringA omVersion = omTag;
        omVersion.TrimLeft("vV");
        omText.Format("BUSMASTER-NG %s is available (this is version %s).\n\nOpen its download page?",
                      (LPCSTR)omVersion, BMNG_VERSION_STRING);
        if (::MessageBox(pParent->GetSafeHwnd(), omText, BMNG_PRODUCT_NAME, MB_YESNO | MB_ICONINFORMATION) == IDYES)
        {
            // the tag has passed bParseVersion: only a 'v', digits and dots go into the address
            vOpenUrl(CString(UPDATE_RELEASES_URL "/tag/") + CString(omTag));
        }
    }
    else
    {
        omText.Format("BUSMASTER-NG %s is the latest version.", BMNG_VERSION_STRING);
        ::MessageBox(pParent->GetSafeHwnd(), omText, BMNG_PRODUCT_NAME, MB_OK | MB_ICONINFORMATION);
    }
}
