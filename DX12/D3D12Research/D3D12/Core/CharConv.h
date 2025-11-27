namespace CharConv
{
	bool StrCmp(const char* pStrA, const char* pStrB, bool caseSensitive);

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

	template<typename T>
	inline bool StrConvert(const char* pStr, T& out)
	{
		static_assert(false, "Not implemented.");
	}

	template<> bool StrConvert(const char* pStr, char& out);
	template<> bool StrConvert(const char* pStr, int& out);
	template<> bool StrConvert(const char* pStr, uint32_t& out);
	template<> bool StrConvert(const char* pStr, float& out);
	template<> bool StrConvert(const char* pStr, double& out);
	template<> bool StrConvert(const char* pStr, const char*& pOut);
	template<> bool StrConvert(const char* pStr, bool& out);

	template<typename T, size_t VALUES>
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
			if (!StrConvert(pArgs[i], pValue[i]))
			{
				return false;
			}
		}
		return true;
	}

	/*template<> bool StrConvert(const char* pStr, Vector4& out) { return StrArrayConvert<float, 4>(pStr, &out.x); }
	template<> bool StrConvert(const char* pStr, Vector3& out) { return StrArrayConvert<float, 3>(pStr, &out.x); }
	template<> bool StrConvert(const char* pStr, Vector2& out) { return StrArrayConvert<float, 2>(pStr, &out.x); }
	template<> bool StrConvert(const char* pStr, IntVector2& out) { return StrArrayConvert<int, 2>(pStr, &out.x); }
	template<> bool StrConvert(const char* pStr, IntVector3& out) { return StrArrayConvert<int, 3>(pStr, &out.x); }*/

	// namespace Private
	// {
	// 	template<size_t I, typename... Args>
	// 	void TupleFromArguments(std::tuple<Args...>& t, const char** pArgs, int& failIndex)
	// 	{
	// 		if (failIndex == -1)
	// 		{
	// 			if (!CharConv::StrConvert(pArgs[I], std::get<I>(t)))
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
						if (!CharConv::StrConvert(pArgs[I], std::get<I>(result)))
							*pFailIndex = I;
					}
				}()), ...);
			};

			convert(std::make_index_sequence<sizeof...(Args)>{});
		}

		return result;
	}
}
