#pragma once

#define CPP_DELEGATES_USE_OLD_NAMING 0

#define DECLARE_DELEGATE(name, ...) \
using name = Delegate<void, __VA_ARGS__>

#define DECLARE_DELEGATE_RET(name, retValue, ...) \
using name = Delegate<retValue, __VA_ARGS__>

#define DECLARE_MULTICAST_DELEGATE(name, ...) \
using name = MulticastDelegate<__VA_ARGS__>; \
using name ## Delegate = MulticastDelegate<__VA_ARGS__>::DelegateT

#define DECLARE_EVENT(name, ownerType, ...) \
class name : public MulticastDelegate<__VA_ARGS__> \
{ \
private: \
    friend class ownerType; \
    using MulticastDelegate::Broadcast; \
    using MulticastDelegate::RemoveAll; \
    using MulticastDelegate::Remove; \
};

// base type for delegates
template<typename RetVal, typename... Args>
class IDelegate
{
public:
    IDelegate() = default;
    virtual ~IDelegate() noexcept = default;
    virtual RetVal Execute(Args&&... args) = 0;
    virtual void* GetOwner() const
    {
        return nullptr;
    }
};

template<typename RetVal, typename... Args2>
class StaticDelegate;

template<typename RetVal, typename... Args, typename... Args2>
class StaticDelegate<RetVal(Args...), Args2...> : public IDelegate<RetVal, Args...>
{
public:
    using DelegateFunction = RetVal(*)(Args..., Args2...);

    StaticDelegate(DelegateFunction function, Args2&&... args)
        : m_Function(function), m_Payload(std::forward<Args2>(args)...)
    {}

    virtual RetVal Execute(Args&&... args) override
    {
        std::apply(
        [&](auto&&... payloadArgs) {
            return std::invoke(m_Function, 
                               std::forward<Args>(args)..., 
                               std::forward<decltype(payloadArgs)>(payloadArgs)...);
        },
        m_Payload
        );
    }

private:
    DelegateFunction m_Function;
    std::tuple<Args2...> m_Payload;
};

template<typename T, typename RetVal, typename... Args2>
class RawDelegate;

template<typename T, typename RetVal, typename... Args, typename... Args2>
class RawDelegate<T, RetVal(Args...), Args2...> : public IDelegate<RetVal, Args...>
{
public:
    using DelegateFunction = RetVal(T::*)(Args..., Args2...);

    RawDelegate(T* pObject,DelegateFunction function, Args2&&... args)
        : m_pObject(pObject), m_Function(function), m_Payload(std::forward<Args2>(args)...)
    {}

    virtual RetVal Execute(Args&&... args) override
    {
		return std::apply(
			[&](auto&&... payloadArgs) {
				return std::invoke(m_Function, 
					               m_pObject,
					               std::forward<Args>(args)...,
					               std::forward<decltype(payloadArgs)>(payloadArgs)...);
			},
			m_Payload
		);
    }

    virtual void* GetOwner() const override
    {
        return m_pObject;
    }

private:
    T* m_pObject;
    DelegateFunction m_Function;
    std::tuple<Args2...> m_Payload;
};

template<typename TLambda, typename RetVal, typename... Args>
class LambdaDelegate;

template<typename TLambda, typename RetVal, typename... Args, typename... Args2>
class LambdaDelegate<TLambda, RetVal(Args...), Args2...> : public IDelegate<RetVal, Args...>
{
public:
    LambdaDelegate(TLambda&& lambda, Args2&&... args)
        : m_Lambda(std::forward<TLambda>(lambda)), m_Payload(std::forward<Args2>(args)...)
    {}

    virtual RetVal Execute(Args&&... args) override
    {
        std::apply(
        [&](auto&&... payloadArgs) {
            return std::invoke(m_Lambda, 
				               std::forward<Args>(args)...,
				               std::forward<decltype(payloadArgs)>(payloadArgs)...);
        },
        m_Payload
        );
    }

private:
    TLambda m_Lambda;
    std::tuple<Args2...> m_Payload;
};

template<typename T, typename RetVal, typename... Args>
class SPDelegate;

template<typename RetVal, typename T, typename... Args, typename... Args2>
class SPDelegate<T, RetVal(Args...), Args2...> : public IDelegate<RetVal, Args...>
{
public:
    using DelegateFunction = RetVal(T::*)(Args..., Args2...);

    SPDelegate(const std::shared_ptr<T>& pObject, DelegateFunction function, Args2&&... args)
        : m_pObject(pObject), m_Function(function), m_Payload(std::forward<Args2>(args)...)
    {}

    virtual RetVal Execute(Args&&... args) override
    {
		if (m_pObject.expired())
		{
			return RetVal();
		}

		std::shared_ptr<T> pPinned = m_pObject.lock();
        std::apply(
        [&](auto&&... payloadArgs) {
            return std::invoke(m_Function, 
                               pPinned.get(), 
                               std::forward<Args>(args)..., 
                               std::forward<decltype(payloadArgs)>(payloadArgs)...);
        },
        m_Payload
        );
    }

    virtual void* GetOwner() const override
    {
        if (m_pObject.expired() == false)
        {
            return nullptr;
        }

        return m_pObject.lock().get();
    }

private:

    std::weak_ptr<T> m_pObject;
    DelegateFunction m_Function;
    std::tuple<Args2...> m_Payload;
};

// a handle to a delegate used for a multicast delegate
// static ID so that every handle is unique
class DelegateHandle
{
public:
    constexpr DelegateHandle() noexcept : m_Id(-1) {}
    explicit DelegateHandle(bool) noexcept : m_Id(GetNewID()) {}

    ~DelegateHandle() noexcept = default;

    DelegateHandle(const DelegateHandle&) = default;
    DelegateHandle& operator=(const DelegateHandle&) = default;

    DelegateHandle(DelegateHandle&& other) noexcept : m_Id(other.m_Id)
    {
        other.Reset();
    }

    DelegateHandle& operator=(DelegateHandle&& other) noexcept
    {
        m_Id = other.m_Id;
        other.Reset();
        return *this;
    }

    inline operator bool() const noexcept
    {
        return IsValid();
    }

    inline bool operator==(const DelegateHandle& other) const noexcept
    {
        return m_Id == other.m_Id;
    }

    inline bool operator<(const DelegateHandle& other) const noexcept
    {
        return m_Id < other.m_Id;
    }

    inline bool IsValid() const noexcept
    {
        return m_Id != -1;
    }

    inline void Reset() noexcept
    {
        m_Id = -1;
    }

private:
    __int64 m_Id;
    inline static __int64 CURRENT_ID{0};
    static __int64 GetNewID();
};

template<size_t MaxStackSize>
class InlineAllocator
{
public:
    constexpr InlineAllocator() noexcept : m_Size(0)
    {
        static_assert(MaxStackSize > sizeof(void*), "MaxStackSize is smaller or equal to the size of a pointer. This will make the use of an InlineAllocator pointless. Please increse the MaxStackSize");
    }

    ~InlineAllocator() noexcept
    {
        Free();
    }

    InlineAllocator(const InlineAllocator& other) : m_Size(other.m_Size)
    {
        if (other.HasAllocation())
        {
            memcpy(Allocate(other.m_Size), other.GetAllocation(), other.m_Size);
        }
    }

    InlineAllocator& operator=(const InlineAllocator& other)
    {
        if (other.HasAllocation())
        {
            memcpy(Allocate(other.m_Size), other.GetAllocation(), other.m_Size);
        }
        m_Size = other.m_Size;
        return *this;
    }

    InlineAllocator(InlineAllocator&& other) noexcept : m_Size(other.m_Size)
    {
        other.m_Size = 0;
        if (m_Size > MaxStackSize)
        {
            std::swap(pPtr, other.pPtr);
        }
        else
        {
            memcpy(Buffer, other.Buffer, m_Size);
        }
    }

    InlineAllocator& operator=(InlineAllocator&& other) noexcept
    {
        Free();
        m_Size = other.m_Size;
        other.m_Size = 0;
        if (m_Size > MaxStackSize)
        {
            std::swap(pPtr, other.pPtr);
        }
        else
        {
            memcpy(Buffer, other.Buffer, m_Size);
        }
        return *this;
    }

    // allocate memory of given size
    // if the size is over the predefined threshold, it will be allocated on the heap
    void* Allocate(const size_t size)
    {
        if (m_Size != size)
        {
            Free();
            m_Size = size;
            if (m_Size > MaxStackSize)
            {
                pPtr = new char[size];
                return pPtr;
            }
        }
        return (void*)Buffer;
    }

    void Free()
    {
        if (m_Size > MaxStackSize)
        {
            delete[] (char*)pPtr;
        }
        m_Size = 0;
    }

    void* GetAllocation() const
    {
        if (HasAllocation())
        {
            return HasHeapAllocation() ? pPtr : (void*)Buffer;
        }
        return nullptr;
    }

    inline bool HasAllocation() const
    {
        return m_Size > 0;
    }

    inline bool HasHeapAllocation() const
    {
        return m_Size > MaxStackSize;
    }

private:
    // if the allocation is smaller than the threshold, use the buffer
    // otherwise pPtr is used together with a separate dynamic allocation
    union
    {
        char Buffer[MaxStackSize];
        void* pPtr;
    };

    size_t m_Size;
};

template<typename RetVal, typename... Args>
class Delegate
{
public:
    using IDelegateT = IDelegate<RetVal, Args...>;

    constexpr Delegate() noexcept {}

    ~Delegate() noexcept
    {
        Release();
    }

    Delegate(const Delegate& other) : m_Allocator(other.m_Allocator) {}
    Delegate& operator=(const Delegate& other)
    {
        Release();
        m_Allocator = other.m_Allocator;
        return *this;
    }

    Delegate(Delegate&& other) noexcept : m_Allocator(std::move(other.m_Allocator)) {}
    Delegate& operator=(Delegate&& other) noexcept
    {
        Release();
        m_Allocator = std::move(other.m_Allocator);
        return *this;
    }

    template<typename T, typename... Args2>
    static Delegate CreateRaw(T* pObj, RetVal(T::*pFunction)(Args..., Args2...), Args2... args)
    {
        Delegate handler;
        handler.Bind<RawDelegate<T, RetVal(Args...), Args2...>>(pObj, pFunction, std::forward<Args2>(args)...);
        return handler;
    }

    template<typename... Args2>
    static Delegate CreateStatic(RetVal(*pFunction)(Args..., Args2...), Args2... args)
    {
        Delegate handler;
        handler.Bind<StaticDelegate<RetVal(Args...), Args2...>>(pFunction, std::forward<Args2>(args)...);
        return handler;
    }

    template<typename TLambda, typename... Args2>
    static Delegate CreateLambda(TLambda&& lambda, Args2... args)
    {
        Delegate handler;
        handler.Bind<LambdaDelegate<TLambda, RetVal(Args...), Args2...>>(std::forward<TLambda>(lambda), std::forward<Args2>(args)...);
        return handler;
    }

    template<typename T, typename... Args2>
    static Delegate CreateSP(const std::shared_ptr<T>& pObj, RetVal(T::*pFunction)(Args..., Args2...), Args2... args)
    {
        Delegate handler;
        handler.Bind<SPDelegate<T, RetVal(Args...), Args2...>>(pObj, pFunction, std::forward<Args2>(args)...);
        return handler;
    }

    // bind a member function
    template<typename T, typename... Args2>
    inline void BindRaw(T* pObj, RetVal(T::*pFunction)(Args..., Args2...), Args2&&... args)
    {
        *this = CreateRaw<T, Args2...>(pObj, pFunction, std::forward<Args2>(args)...);
    }

    // bind a static/global function
    template<typename... Args2>
    inline void BindStatic(RetVal(*pFunction)(Args..., Args2...), Args2&&... args)
    {
        *this = CreateStatic<Args2...>(pFunction, std::forward<Args2>(args)...);
    }

    // bind a lambda
    template<typename TLambda, typename... Args2>
    inline void BindLambda(TLambda&& lambda, Args2&&... args)
    {
        *this = CreateLambda<TLambda, Args2...>(std::forward<TLambda>(lambda), std::forward<Args2>(args)...);
    }

    // bind a member function with a shared pointer
    template<typename T, typename... Args2>
    inline void BindSP(const std::shared_ptr<T> pObj, RetVal(T::*pFunction)(Args..., Args2...), Args2&&... args)
    {
        *this = CreateSP<T, Args2...>(pObj, pFunction, std::forward<Args2>(args)...);
    }

    inline bool IsBound() const
    {
        return m_Allocator.HasAllocation();
    }

    RetVal Execute(Args... args) const
    {
        assert(m_Allocator.HasAllocation() && "Delegate is not bound");
        return GetDelegate()->Execute(std::forward<Args>(args)...);
    }

    RetVal ExecuteIfBound(Args... args) const
    {
        if (IsBound())
        {
            return Execute(std::forward<Args>(args)...);
        }
        return RetVal();
    }

    // get the owner of the delegate
    // only valid for SPDelegate and RawDelegate
    void* GetOwner() const
    {
        if (m_Allocator.HasAllocation())
        {
            return GetDelegate()->GetOwner();
        }
        return nullptr;
    }

    inline void ClearIfBoundTo(void* pObj)
    {
        if (pObj != nullptr && IsBoundTo(pObj))
        {
            Release();
        }
    }

    inline void Clear()
    {
        Release();
    }

    inline bool IsBoundTo(void* pObj) const
    {
        if (pObj == nullptr || m_Allocator.HasAllocation() == false)
        {
            return false;
        }
        return GetDelegate()->GetOwner() == pObj;
    }

    // determines the stack size the inline allocator can use
    // this is a public function so it can easily be requested
    constexpr static __int32 GetAllocatorStackSize() noexcept
    {
        return 32;
    }

private:
    template<typename T, typename... Args3>
    void Bind(Args3&&... args)
    {
        Release();
        void* pAlloc = m_Allocator.Allocate(sizeof(T));
        new (pAlloc) T(std::forward<Args3>(args)...);
    }

    void Release()
    {
        if (m_Allocator.HasAllocation())
        {
            GetDelegate()->~IDelegate();
            m_Allocator.Free();
        }
    }

    inline IDelegateT* GetDelegate() const
    {
        return static_cast<IDelegateT*>(m_Allocator.GetAllocation());
    }

    // allocator for the delegate itself.
    // delegate gets allocated inline when its is smaller or equal than 64 bytes in size.
    // can be changed by preference
    InlineAllocator<Delegate::GetAllocatorStackSize()> m_Allocator;
};

#if CPP_DELEGATES_USE_OLD_NAMING
template<typename RetVal, typename... Args>
using SinglecastDelegate = Delegate<RetVal, Args...>;
#endif

// delegate that can be bound to by multiple objects
template<typename... Args>
class MulticastDelegate
{
public:
	using DelegateT = Delegate<void, Args...>;

private:
    using DelegateHandlePair = std::pair<DelegateHandle, DelegateT>;

public:
    constexpr MulticastDelegate() : m_Locks(0) {}

    ~MulticastDelegate() noexcept = default;

	MulticastDelegate(const MulticastDelegate& other) = default;
	MulticastDelegate& operator=(const MulticastDelegate& other) = default;

    MulticastDelegate(MulticastDelegate&& other) noexcept
        : m_Events(std::move(other.m_Events)), m_Locks(other.m_Locks)
    {}

    MulticastDelegate& operator=(MulticastDelegate&& other) noexcept
    {
        m_Events = std::move(other.m_Events);
        m_Locks = other.m_Locks;
        return *this;
	}

    inline DelegateHandle operator+=(DelegateT&& handler) noexcept
    {
		return Add(std::forward<DelegateT>(handler));
	}

    inline bool operator-=(DelegateHandle& handle)
    {
        return Remove(handle);
    }

    inline DelegateHandle Add(DelegateT&& handler) noexcept
    {
        // favour an empty space over a possible array relllocation
        for (size_t i = 0; i < m_Events.size(); i++)
        {
            if (m_Events[i].first.IsValid() == false)
            {
                m_Events[i] = std::make_pair(DelegateHandle(true), std::move(handler));
                return m_Events[i].first;
            }
        }

        m_Events.push_back(std::make_pair(DelegateHandle(true), std::move(handler)));
        return m_Events.back().first;
    }

	// bind a member function
    template<typename T, typename... Args2>
    inline DelegateHandle AddRaw(T* pObject, void(T::* pFunction)(Args..., Args2...), Args2&&... args)
    {
        return Add(DelegateT::CreateRaw(pObject, pFunction, std::forward<Args2>(args)...));
    }

	// bind a static/global function
    template<typename... Args2>
    inline DelegateHandle AddStatic(void(*pFunction)(Args..., Args2...), Args2&&... args)
    {
        return Add(DelegateT::CreateStatic(pFunction, std::forward<Args2>(args)...));
	}

	// bind a lambda
    template<typename LambdaType, typename... Args2>
    inline DelegateHandle AddLambda(LambdaType&& lambda, Args2&&... args)
    {
        return Add(DelegateT::CreateLambda(std::forward<LambdaType>(lambda), std::forward<Args2>(args)...));
	}

    // bind a member function with a shared pointer
	template<typename T, typename... Args2>
    inline DelegateHandle AddSP(std::shared_ptr<T> pObject, void(T::* pFunction)(Args..., Args2...), Args2&&... args)
    {
		return Add(DelegateT::CreateSP(pObject, pFunction, std::forward<Args2>(args)...));
    }

    // removes all handles thar are bound from a specific object
	// ignored when pObject is nullptr
    // note: only works on Raw and SP bindings
    void RemoveObject(void* pObject)
    {
        if (pObject == nullptr)
        {
            return;
		}

        for (size_t i = m_Events.size() - 1; i >= 0 ; i--)
        {
            if (m_Events[i].second.GetOwner() == pObject)
            {
                if (IsLocked())
                {
                    m_Events[i].Clear();
                }
                else
                {
					std::swap(m_Events[i], m_Events[m_Events.size() - 1]);
                    m_Events.pop_back();
                }
            }
        }
    }

    // remove a function from the event list by the handle
    bool Remove(DelegateHandle& handle)
    {
        if (handle.IsValid() == false)
        {
            return false;
		}

        for (size_t i = 0; i < m_Events.size(); i++)
        {
            if (m_Events[i].first == handle)
            {
                if (IsLocked())
                {
					m_Events[i].second.Clear();
                }
                else
                {
					std::swap(m_Events[i], m_Events[m_Events.size() - 1]);
					m_Events.pop_back();
                }
				handle.Reset();
                return true;
            }
        }
        return false;
    }

    bool IsBoundTo(const DelegateHandle& handle) const
    {
        if (handle.IsValid() == false)
        {
            return false;
		}

        for (const auto& event : m_Events)
        {
            if (event.first == handle)
            {
                return true;
            }
        }
		return false;
    }

    // remove all the functions bound to the delegate
    inline void RemoveAll()
    {
        if (IsLocked())
        {
            for (auto& event : m_Events)
            {
                event.second.Clear();
            }
        }
        else
        {
            m_Events.clear();
		}
    }

    void Compress(const size_t maxSpace = 0)
    {
        if (IsLocked())
        {
            return;
        }

        size_t toDelegate = 0;
        for (size_t i = 0; i < m_Events.size() - toDelegate; ++i)
        {
            if (m_Events[i].first.IsValid() == false)
            {
				std::swap(m_Events[i], m_Events[m_Events.size() - toDelegate - 1]);
				++toDelegate;
            }
            if (toDelegate > maxSpace)
            {
				m_Events.resize(m_Events.size() - toDelegate);
            }
        }
    }

    // execute all functions that are bound
    inline void Broadcast(Args... args)
    {
        Lock();
        for (const auto& event : m_Events)
        {
            if (event.first.IsValid())
            {
                event.second.Execute(std::forward<Args>(args)...);
            }
		}
        Unlock();
    }

private:
    inline void Lock()
    {
        ++m_Locks;
    }

    inline void Unlock()
    {
        assert(m_Locks > 0);
        --m_Locks;
    }

    // returns true is the delegate is currently broadcasting
    // if this is true, the order of the array should not be changed otherwise this causes undefined behavior
    inline bool IsLocked() const
    {
        return m_Locks > 0;
	}

	std::vector<DelegateHandlePair> m_Events;
    unsigned int m_Locks;
};