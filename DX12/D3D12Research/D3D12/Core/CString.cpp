#include "stdafx.h"
#include "CString.h"

namespace CString
{
	void TrimSpaces(char* pStr)
	{
		char* pNewStart = pStr;
		while (*pNewStart == ' ') { ++pNewStart; }
		strcpy_s(pStr, INT_MAX, pNewStart);
		char* pEnd = pStr + strlen(pStr);
		while (pEnd > pStr && pEnd[-1] == ' ')
		{
			--pEnd;
		}
		*pEnd = '\0';
	}

	bool StrCmp(const char* pStrA, const char* pStrB, bool caseSensitive)
	{
		if (caseSensitive)
		{
			// Use standard strcmp for case-sensitive (highly optimized)
			return std::strcmp(pStrA, pStrB) == 0;
		}

		// Case-insensitive comparison
		while (*pStrA && *pStrB)
		{
			if (std::tolower(*pStrA) != std::tolower(*pStrB))
			{
				return false;
			}
			++pStrA;
			++pStrB;
		}
		return *pStrA == *pStrB;
	}

	bool FromString(const char* pStr, int& out)
	{
		if (!pStr || *pStr == '\0')
		{
			return false;
		}

		const char* end = pStr;
		while (*end) ++end;

		auto result = std::from_chars(pStr, end, out);
		return result.ec == std::errc() && result.ptr == end;
	}

	bool FromString(const char* pStr, uint32_t& out)
	{
		if (!pStr || *pStr == '\0')
		{
			return false;
		}

		const char* end = pStr;
		while (*end) ++end;

		auto result = std::from_chars(pStr, end, out);
		return result.ec == std::errc() && result.ptr == end;
	}

	bool FromString(const char* pStr, float& out)
	{
		if (!pStr || *pStr == '\0')
		{
			return false;
		}

		const char* end = pStr;
		while (*end) ++end;

		auto result = std::from_chars(pStr, end, out);
		return result.ec == std::errc() && result.ptr == end;
	}

	bool FromString(const char* pStr, double& out)
	{
		if (!pStr || *pStr == '\0')
		{
			return false;
		}

		const char* end = pStr;
		while (*end) ++end;

		auto result = std::from_chars(pStr, end, out);
		return result.ec == std::errc() && result.ptr == end;
	}

	bool FromString(const char* pStr, char& out)
	{
		out = pStr[0];
		return true;
	}

	bool FromString(const char* pStr, const char*& pOut)
	{
		pOut = pStr;
		return true;
	}

	bool FromString(const char* pStr, bool& out)
	{
		if (*pStr == '0' || StrCmp(pStr, "false", false))
		{
			out = false;
			return true;
		}
		else if (*pStr == '1' || StrCmp(pStr, "true", false))
		{
			out = true;
			return true;
		}
		return false;
	}
}
