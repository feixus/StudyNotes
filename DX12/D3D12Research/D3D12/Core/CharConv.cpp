#include "stdafx.h"
#include "CharConv.h"

namespace CharConv
{
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

	template<>
	bool StrConvert(const char* pStr, int& out)
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

	template<>
	bool StrConvert(const char* pStr, uint32_t& out)
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

	template<>
	bool StrConvert(const char* pStr, float& out)
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

	template<>
	bool StrConvert(const char* pStr, double& out)
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

	template<>
	bool StrConvert(const char* pStr, char& out)
	{
		out = pStr[0];
		return true;
	}

	template<>
	bool StrConvert(const char* pStr, const char*& pOut)
	{
		pOut = pStr;
		return true;
	}

	template<>
	bool StrConvert(const char* pStr, bool& out)
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
