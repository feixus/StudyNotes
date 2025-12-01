#pragma once
#include "CString.h"

class IConsoleObject;

class CVarManager
{
public:
	static void Initialize();
	static void RegisterConsoleObject(const char* pName, IConsoleObject* pObject);
    static bool Execute(const char* pCommand, char delimiter = ' ');

	static IConsoleObject* FindConsoleObject(const char* pName);

	template<typename T>
	static void ForEachCvar(T&& callback)
	{
		for (const auto& cvar : GetObjects())
		{
			callback(cvar);
		}
	}

	static const std::vector<IConsoleObject*>& GetObjects();
};

class IConsoleObject
{
public:
	IConsoleObject(const char* pName) : m_pName(pName)
	{
		CVarManager::RegisterConsoleObject(pName, this);
	}

	virtual ~IConsoleObject() = default;
	virtual bool Execute(const char** pArgs, uint32_t numArgs) = 0;

	virtual int GetInt() const { return 0; }
	virtual float GetFloat() const { return 0.0f; }
	virtual bool GetBool() const { return false; }
	virtual std::string GetString() const { return ""; }
	const char* GetName() const { return m_pName; }

private:
	const char* m_pName;
};

template<typename T, bool = std::is_pointer_v<T>>
struct DecayNonPointer {};

template<typename T>
struct DecayNonPointer<T, 0>
{
    using Type = std::remove_const_t<std::remove_reference_t<T>>;
};

template<typename T>
struct DecayNonPointer<T, 1>
{
    using Type = T;
};

template<typename... Args>
class DelegateConsoleCommand : public IConsoleObject
{
public:
	DelegateConsoleCommand(const char* pName, Delegate<void, Args...>&& delegate)
		: IConsoleObject(pName), m_Callback(std::move(delegate))
	{}

	template<typename T>
	DelegateConsoleCommand(const char* pName, T&& callback)
		: IConsoleObject(pName), m_Callback(Delegate<void, Args...>::CreateLambda(std::move(callback)))
	{}

	virtual bool Execute(const char** pArgs, uint32_t numArgs) override
	{
		if (numArgs == sizeof...(Args))
		{
			return ExecuteInternal(pArgs, numArgs, std::index_sequence_for<Args...>());
		}
		else
		{
			E_LOG(Warning, "invalid number of arguments for command '%s' (expected %d, got %d)", GetName(), sizeof...(Args), numArgs);
		}
		return false;
	}

private:
	template<size_t... Is>
	bool ExecuteInternal(const char** pArgs, uint32_t numArgs, std::index_sequence<Is...>)
	{
		int failIndex = -1;
		std::tuple<typename DecayNonPointer<Args>::Type...> arguments = CString::TupleFromArguments<DecayNonPointer<Args>::Type...>(pArgs, &failIndex);
		if (failIndex >= 0)
		{
			E_LOG(Warning, "failed to convert argument '%s'", pArgs[failIndex]);
			return false;
		}

		m_Callback.Execute(std::get<Is>(arguments)...);
		return true;
	}

	Delegate<void, Args...> m_Callback;
};

template<typename... Args>
class ConsoleCommand : public IConsoleObject
{
public:
	using CallbackFn = void(*)(Args...);

	ConsoleCommand(const char* pName, CallbackFn fn) : IConsoleObject(pName), m_Callback(fn)
	{}

	virtual bool Execute(const char** pArgs, uint32_t numArgs) override
	{
		if (numArgs == sizeof...(Args))
		{
			return ExecuteInternal(pArgs, numArgs, std::index_sequence_for<Args...>());
		}
		else
		{
			E_LOG(Warning, "invalid number of arguments for command '%s' (expected %d, got %d)", GetName(), (int)sizeof...(Args), numArgs);
		}
		return false;
	}

private:
	template<size_t... Is>
	bool ExecuteInternal(const char** pArgs, uint32_t numArgs, std::index_sequence<Is...>)
	{
		int failIndex = -1;
		std::tuple<typename DecayNonPointer<Args>::Type...> arguments = CString::TupleFromArguments<typename DecayNonPointer<Args>::Type...>(pArgs, &failIndex);
		if (failIndex >= 0)
		{
			E_LOG(Warning, "failed to convert argument '%s'", pArgs[failIndex]);
			return false;
		}

		m_Callback(std::get<Is>(arguments)...);
		return true;
	}

	CallbackFn m_Callback;
};

template<typename T>
class ConsoleVariable : public IConsoleObject
{
public:
	ConsoleVariable(const char* pName, T defaultValue, Delegate<void, IConsoleObject*> onModified = {})
		: IConsoleObject(pName), m_Value(defaultValue), m_OnModified(onModified)
	{}

	void SetValue(const T& value)
    {
        m_Value = value;
        m_OnModified.ExecuteIfBound(this);
    }

    ConsoleVariable& operator=(const T& value)
    {
        SetValue(value);
        return *this;
    }

	virtual bool Execute(const char** pArgs, uint32_t numArgs) override
	{
        if (numArgs <= 0)
        {
            std::string val;
            CString::ToString(m_Value, &val);
            E_LOG(Info, "'%s' = %s", GetName(), val.c_str());
            return false;
        }

        std::remove_reference_t<T> val;
        if (CString::FromString(pArgs[0], val))
        {
            SetValue(val);
            E_LOG(Info, "'%s' = %s", GetName(), pArgs[0]);
            return true;
        }
		
		return false;
	}

    operator const T&() const { return m_Value; }

	virtual int GetInt() const override;
	virtual float GetFloat() const override;
	virtual bool GetBool() const override;
	virtual std::string GetString() const override;

	T& Get() { return m_Value; }
	const T& Get() const { return m_Value; }

private:
	T m_Value;
	Delegate<void, IConsoleObject*> m_OnModified;
};

template<typename T>
using ConsoleVariableRef = ConsoleVariable<T&>;


class ImGuiConsole
{
public:
    static void Strtrim(char* s) { char* str_end = s + strlen(s); while (str_end > s && str_end[-1] == ' ') str_end--; *str_end = 0; }

    ImGuiConsole()
    {
        m_Input[0] = '\0';
    }

    void Update(const ImVec2& position, const ImVec2& size);
    
private:
    int InputCallback(ImGuiInputTextCallbackData* pCallbackData);

    std::vector<std::string> m_History;
    std::vector<const char*> m_Suggestions;
    std::array<char, 1024> m_Input{};
    int m_HistoryPos{-1};
    int m_SuggestionPos{-1};

    bool m_ShowConsole{false};
    bool m_FocusConsole{true};
    bool m_AutoCompleted{false};
};
