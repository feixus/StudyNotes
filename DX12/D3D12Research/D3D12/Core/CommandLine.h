#pragma once

class CommandLine
{
public:
    // -DebugShaders
    static bool Parse(const char* pCommandLine);

	static bool GetInt(const std::string& name, int& value, int defaultValue = 0);
	static bool GetBool(const std::string& parameter);
    static const std::string& Get() { return m_CommandLine;}

private:
    static inline std::unordered_map<std::string, std::string> m_Parameters;
    static inline std::string m_CommandLine;
};
