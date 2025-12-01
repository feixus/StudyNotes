#pragma once
#include "stb/stb_sprintf.h"

inline int FormatString(char* pBuffer, size_t bufferSize, const char* pFormat, ...)
{
    va_list args;
    va_start(args, pFormat);
    int result = stbsp_vsnprintf(pBuffer, (int)bufferSize, pFormat, args);
    va_end(args);
    
    if (pBuffer == nullptr)
        return result;

    if (result == -1 || result >= bufferSize)
        result = bufferSize - 1;
        
    pBuffer[result] = 0;
    return result;
}

inline int FormatStringVars(char* pBuffer, size_t bufferSize, const char* pFormat, va_list args)
{
    int result = stbsp_vsnprintf(pBuffer, (int)bufferSize, pFormat, args);
    if (pBuffer == nullptr)
        return result;
    if (result == -1 || result >= bufferSize)
        result = bufferSize - 1;
        
    pBuffer[result] = 0;
    return result;
}

template<typename... Args>
std::string Sprintf(const char* pFormat, Args... args)
{
    int length = FormatString(nullptr, 0, pFormat, args...);
	std::string str;
    str.resize(length);
	FormatString(str.data(), length + 1, pFormat, args...);
	return str;
}

namespace CharConv
{
	bool StrCmp(const char* pStrA, const char* pStrB, bool caseSensitive);

    inline void ToUpper(const char* pStr, char* pOut)
    {
        while(*pStr)
        {
            *pOut++ = (char)std::toupper(*pStr++);
        }
        *pOut = '\0';
    }

    inline void ToLower(const char* pStr, char* pOut)
    {
        while(*pStr)
        {
            *pOut++ = (char)std::tolower(*pStr++);
        }
        *pOut = '\0';
    }

	template<size_t I>
	int SplitString(const char* pStr, char(&buffer)[I], const char** pOut, char delimiter = ' ')
	{
		strcpy_s(buffer, pStr);

		int count = 0;
		char* p = buffer;
		char* token = buffer;

		while (*p)
		{
			if (*p == delimiter)
			{
				*p = '\0';

				if (*token != '\0')
					pOut[count++] = token;

				token = p + 1;
			}
			++p;
		}

		if (*token != '\0')
			pOut[count++] = token;

		return count;
	}

	bool FromString(const char* pStr, char& out);
	bool FromString(const char* pStr, int& out);
	bool FromString(const char* pStr, uint32_t& out);
	bool FromString(const char* pStr, float& out);
	bool FromString(const char* pStr, double& out);
	bool FromString(const char* pStr, const char*& pOut);
	bool FromString(const char* pStr, bool& out);

	template<typename T, int VALUES>
	bool StrArrayConvert(const char* pStr, T* pValue)
	{
		const char* pArgs[VALUES];
		char buffer[1024];
		int numValues = SplitString(pStr, buffer, &pArgs[0], ',');
		if (numValues != VALUES)
		{
			return false;
		}
		for (int i = 0; i < VALUES; ++i)
		{
			if (!FromString(pArgs[i], pValue[i]))
			{
				return false;
			}
		}
		return true;
	}

    inline void ToString(char val, std::string* pOut) { *pOut = Sprintf("%c", val); }
	inline void ToString(int val, std::string* pOut) { *pOut = Sprintf("%d", val); }
	inline void ToString(uint32_t val, std::string* pOut) { *pOut = Sprintf("%d", val); }
	inline void ToString(float val, std::string* pOut) { *pOut = Sprintf("%.3f", val); }
	inline void ToString(double val, std::string* pOut) { *pOut = Sprintf("%.3f", val); }
	inline void ToString(const char* val, std::string* pOut) { *pOut = val; }
	inline void ToString(bool val, std::string* pOut) { *pOut = Sprintf("%d", val ? "True" : "False"); }

	/*template<> bool FromString(const char* pStr, Vector4& out) { return StrArrayConvert<float, 4>(pStr, &out.x); }
	template<> bool FromString(const char* pStr, Vector3& out) { return StrArrayConvert<float, 3>(pStr, &out.x); }
	template<> bool FromString(const char* pStr, Vector2& out) { return StrArrayConvert<float, 2>(pStr, &out.x); }
	template<> bool FromString(const char* pStr, IntVector2& out) { return StrArrayConvert<int, 2>(pStr, &out.x); }
	template<> bool FromString(const char* pStr, IntVector3& out) { return StrArrayConvert<int, 3>(pStr, &out.x); }*/

	// namespace Private
	// {
	// 	template<size_t I, typename... Args>
	// 	void TupleFromArguments(std::tuple<Args...>& t, const char** pArgs, int& failIndex)
	// 	{
	// 		if (failIndex == -1)
	// 		{
	// 			if (!CharConv::FromString(pArgs[I], std::get<I>(t)))
	// 			{
	// 				failIndex = I;
	// 			}
	// 			if constexpr (I < sizeof...(Args) - 1)
	// 			{
	// 				TupleFromArguments<I + 1>(t, pArgs, failIndex);
	// 			}
	// 		}
	// 	}
	// }

	// template<typename... Args>
	// std::tuple<Args...> TupleFromArguments(const char** pArgs, int* pFailIndex)
	// {
	// 	std::tuple<Args...> pTuple;
	// 	if constexpr (sizeof...(Args) > 0)
	// 	{
	// 		Private::TupleFromArguments<0>(pTuple, pArgs, *pFailIndex);
	// 	}
	// 	return pTuple;
	// }

	template<typename... Args>
	std::tuple<Args...> TupleFromArguments(const char** pArgs, int* pFailIndex)
	{
		std::tuple<Args...> result;

		if constexpr (sizeof...(Args) > 0)
		{
			auto convert = [&]<size_t... I>(std::index_sequence<I...>)
			{
				(([&] {
					if (*pFailIndex == -1)
					{
						if (!CharConv::FromString(pArgs[I], std::get<I>(result)))
							*pFailIndex = I;
					}
				}()), ...);
			};

			convert(std::make_index_sequence<sizeof...(Args)>{});
		}

		return result;
	}
}
