#pragma once

#include <stdint.h>

template <size_t S>
struct EnumSize;

template <>
struct EnumSize<1>
{
	typedef int8_t type;
};

template <>
struct EnumSize<2>
{
	typedef int16_t type;
};

template <>
struct EnumSize<4>
{
	typedef int32_t type;
};

template <>
struct EnumSize<8>
{
	typedef int64_t type;
};

template <class T>
struct EnumFlagSize
{
	typedef typename EnumSize<sizeof(T)>::type type;
};

#define DECLARE_BITMASK_TYPE(ENUMTYPE) \
inline constexpr ENUMTYPE operator | (ENUMTYPE a, ENUMTYPE b) throw() { return ENUMTYPE(((EnumFlagSize<ENUMTYPE>::type)a) | ((EnumFlagSize<ENUMTYPE>::type)b)); } \
inline constexpr ENUMTYPE& operator |= (ENUMTYPE& a, ENUMTYPE b) throw() { return (ENUMTYPE&)(((EnumFlagSize<ENUMTYPE>::type&)a) |= ((EnumFlagSize<ENUMTYPE>::type)b)); } \
inline constexpr ENUMTYPE operator & (ENUMTYPE a, ENUMTYPE b) throw() { return ENUMTYPE(((EnumFlagSize<ENUMTYPE>::type)a) & ((EnumFlagSize<ENUMTYPE>::type)b)); } \
inline constexpr ENUMTYPE& operator &= (ENUMTYPE& a, ENUMTYPE b) throw() { return (ENUMTYPE&)(((EnumFlagSize<ENUMTYPE>::type&)a) &= ((EnumFlagSize<ENUMTYPE>::type)b)); } \
inline constexpr ENUMTYPE operator ~ (ENUMTYPE a) throw() { return ENUMTYPE(~((EnumFlagSize<ENUMTYPE>::type)a)); } \
inline constexpr ENUMTYPE operator ^ (ENUMTYPE a, ENUMTYPE b) throw() { return ENUMTYPE(((EnumFlagSize<ENUMTYPE>::type)a) ^ ((EnumFlagSize<ENUMTYPE>::type)b)); } \
inline constexpr ENUMTYPE& operator ^= (ENUMTYPE& a, ENUMTYPE b) throw() { return (ENUMTYPE&)(((EnumFlagSize<ENUMTYPE>::type&)a) ^= ((EnumFlagSize<ENUMTYPE>::type)b)); } \
inline constexpr bool Any(ENUMTYPE a, ENUMTYPE b) throw() { return (ENUMTYPE)(((EnumFlagSize<ENUMTYPE>::type)a) & ((EnumFlagSize<ENUMTYPE>::type)b)) != (ENUMTYPE)0;} \
inline constexpr bool All(ENUMTYPE a, ENUMTYPE b) throw() { return (ENUMTYPE)(((EnumFlagSize<ENUMTYPE>::type)a) & ((EnumFlagSize<ENUMTYPE>::type)b)) == b; }
