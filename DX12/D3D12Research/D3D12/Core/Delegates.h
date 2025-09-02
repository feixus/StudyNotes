#pragma once

#ifndef DELEGATE_ASSERT
#include <assert.h>
#define DELEGATE_ASSERT(expression, ...) assert(expression)
#endif

#ifndef DELEGATE_STATIC_ASSERT
#define DELEGATE_STATIC_ASSERT(expression, msg)
#endif

// the allocation size of delegate data.
// delegates larger than this will be heap allocated.
#ifndef DELEGATE_INLINE_ALLOCATION_SIZE
#define DELEGATE_INLINE_ALLOCATION_SIZE 32
#endif

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


/////////////////////////////////////////////////////////////////
/////////////////INTERNAL SECTION////////////////////////////////
/////////////////////////////////////////////////////////////////

namespace _DelegateInternal
{
    template<bool IsConst, typename Object, typename RetVal, typename... Args>
    struct MemberFunction;

    template<typename Object, typename RetVal, typename... Args>
    struct MemberFunction<true, Object, RetVal, Args...>
    {
        using Type = RetVal(Object::*)(Args...) const;
    };

	template<typename Object, typename RetVal, typename... Args>
	struct MemberFunction<false, Object, RetVal, Args...>
	{
		using Type = RetVal(Object::*)(Args...);
	};

    static void* (*Alloc)(size_t size) = [](size_t size) { return malloc(size); };
    static void (*Free)(void* pPtr) = [](void* pPtr) { free(pPtr); };

    template<typename T>
    void DelegateDeleteFunc(T* pPtr)
    {
        pPtr->~T();
        DelegateFreeFunc(pPtr);
    }
}

namespace Delegates
{
    using AllocateCallback = void* (*)(size_t size);
    using FreeCallback = void(*)(void* pPtr);

    inline void SetAllocationCallbacks(AllocateCallback allocateCallback, FreeCallback freeCallback)
    {
        _DelegateInternal::Alloc = allocateCallback;
        _DelegateInternal::Free = freeCallback;
    }
}

class IDelegateBase
{
public:
    IDelegateBase() = default;
	virtual ~IDelegateBase() noexcept = default;
	virtual const void* GetOwner() const { return nullptr; }
};

// base type for delegates
template<typename RetVal, typename... Args>
class IDelegate : public IDelegateBase
{
public:
    virtual RetVal Execute(Args&&... args) = 0;
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

template<bool IsConst, typename T, typename RetVal, typename... Args2>
class RawDelegate;

template<bool IsConst, typename T, typename RetVal, typename... Args, typename... Args2>
class RawDelegate<IsConst, T, RetVal(Args...), Args2...> : public IDelegate<RetVal, Args...>
{
public:
    using DelegateFunction = typename _DelegateInternal::MemberFunction<IsConst, T, RetVal, Args..., Args2...>::Type;

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

    virtual const void* GetOwner() const override
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

template<bool IsConst, typename T, typename RetVal, typename... Args>
class SPDelegate;

template<bool IsConst, typename RetVal, typename T, typename... Args, typename... Args2>
class SPDelegate<IsConst, T, RetVal(Args...), Args2...> : public IDelegate<RetVal, Args...>
{
public:
    using DelegateFunction = typename _DelegateInternal::MemberFunction<IsConst, T, RetVal, Args..., Args2...>::Type;

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

    virtual const void* GetOwner() const override
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
    constexpr DelegateHandle() noexcept : m_Id(INVALID_ID) {}
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
        return m_Id != INVALID_ID;
    }

    inline void Reset() noexcept
    {
        m_Id = INVALID_ID;
    }

    constexpr static const uint32_t INVALID_ID = (uint32_t)~0;

private:
    uint32_t m_Id;
    inline static uint32_t CURRENT_ID{INVALID_ID};
    static uint32_t GetNewID()
	{
        DelegateHandle::CURRENT_ID++;
		if (DelegateHandle::CURRENT_ID == INVALID_ID)
		{
			DelegateHandle::CURRENT_ID = 0;
		}
		return DelegateHandle::CURRENT_ID;
	}
};

template<size_t MaxStackSize>
class InlineAllocator
{
public:
    constexpr InlineAllocator() noexcept : m_Size(0)
    {
        DELEGATE_STATIC_ASSERT(MaxStackSize > sizeof(void*), "MaxStackSize is smaller or equal to the size of a pointer. This will make the use of an InlineAllocator pointless. Please increse the MaxStackSize");
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
                pPtr = _DelegateInternal::Alloc(size);
                return pPtr;
            }
        }
        return (void*)Buffer;
    }

    void Free()
    {
        if (m_Size > MaxStackSize)
        {
            _DelegateInternal::Free(pPtr);
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

    size_t GetSize() const
    {
        return m_Size;
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
        void* pPtr{nullptr};
    };

    size_t m_Size;
};

class DelegateBase
{
public:
	constexpr DelegateBase() noexcept : m_Allocator() {}

	virtual ~DelegateBase() noexcept
	{
		Release();
	}

	// copy constructor
    DelegateBase(const DelegateBase& other) : m_Allocator(other.m_Allocator) {}
	// copy assignment operator
    DelegateBase& operator=(const DelegateBase& other)
	{
		Release();
		m_Allocator = other.m_Allocator;
		return *this;
	}

	// move constructor
    DelegateBase(DelegateBase&& other) noexcept : m_Allocator(std::move(other.m_Allocator)) {}
	// move assignment operator
    DelegateBase& operator=(DelegateBase&& other) noexcept
	{
		Release();
		m_Allocator = std::move(other.m_Allocator);
		return *this;
	}

	// get the owner of the delegate
	// only valid for SPDelegate and RawDelegate
	const void* GetOwner() const
	{
		if (m_Allocator.HasAllocation())
		{
			return GetDelegate()->GetOwner();
		}
		return nullptr;
	}

    size_t GetSize() const
    {
        return m_Allocator.GetSize();
    }

	inline bool IsBound() const
	{
		return m_Allocator.HasAllocation();
	}

	inline bool IsBoundTo(void* pObj) const
	{
		if (pObj == nullptr || m_Allocator.HasAllocation() == false)
		{
			return false;
		}
		return GetDelegate()->GetOwner() == pObj;
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

protected:
	void Release()
	{
		if (m_Allocator.HasAllocation())
		{
			GetDelegate()->~IDelegateBase();
			m_Allocator.Free();
		}
	}

	IDelegateBase* GetDelegate() const
	{
		return static_cast<IDelegateBase*>(m_Allocator.GetAllocation());
	}

	// allocator for the delegate itself.
	// delegate gets allocated inline when its is smaller or equal than 64 bytes in size.
	// can be changed by preference
	InlineAllocator<DELEGATE_INLINE_ALLOCATION_SIZE> m_Allocator;
};

template<typename RetVal, typename... Args>
class Delegate : public DelegateBase
{
private:
    template<typename T, typename... Args2>
    using ConstMemberFunction = typename _DelegateInternal::MemberFunction<true, T, RetVal, Args..., Args2...>::Type;
	template<typename T, typename... Args2>
	using NonConstMemberFunction = typename _DelegateInternal::MemberFunction<false, T, RetVal, Args..., Args2...>::Type;

public:
    using IDelegateT = IDelegate<RetVal, Args...>;

    template<typename T, typename... Args2>
    [[nodiscard]] static Delegate CreateRaw(T* pObj, NonConstMemberFunction<T, Args2...> pFunction, Args2... args)
    {
        Delegate handler;
        handler.Bind<RawDelegate<false, T, RetVal(Args...), Args2...>>(pObj, pFunction, std::forward<Args2>(args)...);
        return handler;
    }

	template<typename T, typename... Args2>
	[[nodiscard]] static Delegate CreateRaw(T* pObj, ConstMemberFunction<T, Args2...> pFunction, Args2... args)
	{
		Delegate handler;
		handler.Bind<RawDelegate<true, T, RetVal(Args...), Args2...>>(pObj, pFunction, std::forward<Args2>(args)...);
		return handler;
	}

    template<typename... Args2>
    [[nodiscard]] static Delegate CreateStatic(RetVal(*pFunction)(Args..., Args2...), Args2... args)
    {
        Delegate handler;
        handler.Bind<StaticDelegate<RetVal(Args...), Args2...>>(pFunction, std::forward<Args2>(args)...);
        return handler;
    }

    template<typename TLambda, typename... Args2>
    [[nodiscard]] static Delegate CreateLambda(TLambda&& lambda, Args2... args)
    {
        Delegate handler;
        handler.Bind<LambdaDelegate<TLambda, RetVal(Args...), Args2...>>(std::forward<TLambda>(lambda), std::forward<Args2>(args)...);
        return handler;
    }

    template<typename T, typename... Args2>
    [[nodiscard]] static Delegate CreateSP(const std::shared_ptr<T>& pObj, NonConstMemberFunction<T, Args2...> pFunction, Args2... args)
    {
        Delegate handler;
        handler.Bind<SPDelegate<false, T, RetVal(Args...), Args2...>>(pObj, pFunction, std::forward<Args2>(args)...);
        return handler;
    }

	template<typename T, typename... Args2>
	[[nodiscard]] static Delegate CreateSP(const std::shared_ptr<T>& pObj, ConstMemberFunction<T, Args2...> pFunction, Args2... args)
	{
		Delegate handler;
		handler.Bind<SPDelegate<true, T, RetVal(Args...), Args2...>>(pObj, pFunction, std::forward<Args2>(args)...);
		return handler;
	}

    // bind a member function
    template<typename T, typename... Args2>
    inline void BindRaw(T* pObj, NonConstMemberFunction<T, Args2...> pFunction, Args2&&... args)
    {
        DELEGATE_STATIC_ASSERT(!std::is_const<T>::value, "cant bind a non-const function on a const object");
        *this = CreateRaw<T, Args2...>(pObj, pFunction, std::forward<Args2>(args)...);
    }

	template<typename T, typename... Args2>
	inline void BindRaw(T* pObj, ConstMemberFunction<T, Args2...> pFunction, Args2&&... args)
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
	inline void BindSP(const std::shared_ptr<T> pObj, NonConstMemberFunction<T, Args2...> pFunction, Args2&&... args)
	{
        DELEGATE_STATIC_ASSERT(!std::is_const<T>::value, "cant bind a non-const function on a const object");
		*this = CreateSP<T, Args2...>(pObj, pFunction, std::forward<Args2>(args)...);
	}

	template<typename T, typename... Args2>
	inline void BindSP(const std::shared_ptr<T> pObj, ConstMemberFunction<T, Args2...> pFunction, Args2&&... args)
	{
		*this = CreateSP<T, Args2...>(pObj, pFunction, std::forward<Args2>(args)...);
	}

    RetVal Execute(Args... args) const
    {
        DELEGATE_ASSERT(m_Allocator.HasAllocation(), "Delegate is not bound");
        return ((IDelegateT*)GetDelegate())->Execute(std::forward<Args>(args)...);
    }

    RetVal ExecuteIfBound(Args... args) const
    {
        if (IsBound())
        {
            return ((IDelegateT*)GetDelegate())->Execute(std::forward<Args>(args)...);
        }
        return RetVal();
    }

private:
    template<typename T, typename... Args3>
    void Bind(Args3&&... args)
    {
        Release();
        void* pAlloc = m_Allocator.Allocate(sizeof(T));
        new (pAlloc) T(std::forward<Args3>(args)...);
    }
};

class MulticastDelegateBase
{
public:
    virtual ~MulticastDelegateBase() = default;
};

// delegate that can be bound to by multiple objects
template<typename... Args>
class MulticastDelegate : public MulticastDelegateBase
{
public:
	using DelegateT = Delegate<void, Args...>;

private:
    struct DelegateHandlerPair
    {
        DelegateHandle Handle;
        DelegateT Callback;
        DelegateHandlerPair() : Handle(false) {}
		DelegateHandlerPair(const DelegateHandle& handle, const DelegateT& callback) : Handle(handle), Callback(callback) {}
		DelegateHandlerPair(const DelegateHandle& handle, DelegateT&& callback) : Handle(handle), Callback(std::move(callback)) {}
    };

    template<typename T, typename... Args2>
    using ConstMemberFunction = typename _DelegateInternal::MemberFunction<true, T, void, Args..., Args2...>::Type;
	template<typename T, typename... Args2>
	using NonConstMemberFunction = typename _DelegateInternal::MemberFunction<false, T, void, Args..., Args2...>::Type;

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
        m_Locks = std::move(other.m_Locks);
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
            if (m_Events[i].Handle.IsValid() == false)
            {
                m_Events[i] = DelegateHandlerPair(DelegateHandle(true), std::move(handler));
                return m_Events[i].Handle;
            }
        }

        m_Events.emplace_back(DelegateHandle(true), std::move(handler));
        return m_Events.back().Handle;
    }

	// bind a member function
    template<typename T, typename... Args2>
    inline DelegateHandle AddRaw(T* pObject, NonConstMemberFunction<T, Args2...> pFunction, Args2&&... args)
    {
        return Add(DelegateT::CreateRaw(pObject, pFunction, std::forward<Args2>(args)...));
    }

	template<typename T, typename... Args2>
	inline DelegateHandle AddRaw(T* pObject, ConstMemberFunction<T, Args2...> pFunction, Args2&&... args)
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
    inline DelegateHandle AddSP(std::shared_ptr<T> pObject, NonConstMemberFunction<T, Args...> pFunction, Args2&&... args)
    {
		return Add(DelegateT::CreateSP(pObject, pFunction, std::forward<Args2>(args)...));
    }

	template<typename T, typename... Args2>
	inline DelegateHandle AddSP(std::shared_ptr<T> pObject, ConstMemberFunction<T, Args...> pFunction, Args2&&... args)
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
            if (m_Events[i].Callback.GetOwner() == pObject)
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
            if (m_Events[i].Handle == handle)
            {
                if (IsLocked())
                {
					m_Events[i].Callback.Clear();
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
            if (event.Handle == handle)
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
                event.Callback.Clear();
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
            if (m_Events[i].Handle.IsValid() == false)
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
            if (event.Handle.IsValid())
            {
                event.Callback.Execute(std::forward<Args>(args)...);
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

	std::vector<DelegateHandlerPair> m_Events;
    unsigned int m_Locks;
};