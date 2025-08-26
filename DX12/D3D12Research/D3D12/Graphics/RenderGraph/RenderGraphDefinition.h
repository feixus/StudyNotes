#pragma once

#define RG_DEBUG 1

#ifndef RG_DEBUG
#define RG_DEBUG 0
#endif

#ifndef RG_ASSERT
#define RG_ASSERT(expression, msg) check((expression) && #msg)
#endif

#ifndef RG_STATIC_ASSERT
#define RG_STATIC_ASSERT(expression, msg) static_assert(expression, msg)
#endif

struct RGResourceHandle
{
    RGResourceHandle() = default;
    explicit RGResourceHandle(int id) : Index(id) {}

    bool operator==(const RGResourceHandle& other) const { return Index == other.Index;}
    bool operator!=(const RGResourceHandle& other) const { return Index != other.Index;}

    constexpr static const int InvalidIndex{-1};

    inline void Invalidate() { Index = InvalidIndex; }
    inline bool IsValid() const { return Index != InvalidIndex; }

    int Index{InvalidIndex};
};