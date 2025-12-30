#pragma once

namespace BitOperations
{
    template<typename T>
    bool LeastSignificantBit(T mask, uint32_t* pIndex)
    {
        *pIndex = ~0u;
        while(mask)
        {
            *pIndex += 1;
            if ((mask & 1) == 1)
            {
                return true;
            }
            mask >>= 1;
        }

        return false;
    }

    template<typename T>
    bool MostSignificantBit(T mask, uint32_t* pIndex)
    {
        if (mask == 0)
        {
            return false;
        }
        *pIndex = 0;
        while (mask >>= 1)
        {
            ++(*pIndex);
        }
        return true;
    }
}

template<uint32_t Bits, typename Storage = uint32_t>
class BitField;

using BitField16 = BitField<16, uint32_t>;
using BitField32 = BitField<32, uint32_t>;
using BitField64 = BitField<64, uint32_t>;

template<uint32_t Bits, typename Storage>
class BitField
{
public:
    class SetBitsIterator
    {
    public:
        explicit SetBitsIterator(const BitField* pBitField, bool end = false)
            : m_CurrentIndex(INVALID), m_pBitField(pBitField)
        {
            if (!end)
            {
                m_pBitField->LeastSignificantBit(&m_CurrentIndex);
            }
        }

        void operator++()
        {
            while (++m_CurrentIndex < Bits)
            {
                if (m_pBitField->GetBit(m_CurrentIndex))
                {
                    return;
                }
            }
            m_CurrentIndex = INVALID;
        }

        bool operator !=(const SetBitsIterator& other)
        {
            return m_CurrentIndex != other.m_CurrentIndex;
        }

        bool Valid() const
        {
            return m_CurrentIndex < Bits;
        }

        uint32_t Value() const
        {
            return m_CurrentIndex;
        }

        uint32_t operator*() const
        {
            return m_CurrentIndex;
        }

        static constexpr uint32_t INVALID = ~0u;
    
    private:
        uint32_t m_CurrentIndex;
        const BitField* m_pBitField;
    };

    BitField()
    {
        ClearAll();
    }

    explicit BitField(bool set)
    {
        if (set)
        {
            SetAll();
        }
        else
        {
            ClearAll();
        }
    }

    template<uint32_t, typename> friend class BitField;

    template<typename T>
    explicit BitField(T value)
    {
        static_assert(std::is_integral_v<T>, "not an integral type");
        ClearAll();
        uint32_t size = sizeof(value) < sizeof(Storage) * Elements() ? sizeof(value) : sizeof(Storage) * Elements();
        memcpy(Data, &value, size);
    }

    template<uint32_t otherNumBits, typename otherStorage>
    BitField(const BitField<otherNumBits, otherStorage>& other)
    {
        ClearAll();
        static_assert(Bits <= otherNumBits, "source cant have more bits");
        uint32_t size = Bits <= otherNumBits ? Bits : otherNumBits;
        memcpy(Data, other.Data, size / 8);
    }

    inline void SetBit(uint32_t bit)
    {
        assert(bit < Size());
        Data[StorageIndexOfBit(bit)] |= MakeBitmaskForStorage(bit);
    }

    inline void ClearBit(const uint32_t bit)
    {
        assert(bit < Size());
        Data[StorageIndexOfBit(bit)] &= ~MakeBitmaskForStorage(bit);
    }

    inline bool GetBit(const uint32_t bit) const
    {
        assert(bit < Size());
        return (Data[StorageIndexOfBit(bit)] & MakeBitmaskForStorage(bit)) != 0;
    }

    void AssignBit(uint32_t bit, bool set)
    {
        set ? SetBit(bit) : ClearBit(bit);
    }

    void ClearAll()
    {
        memset(Data, 0x00000000, sizeof(Storage) * Elements());
    }

    void SetAll()
    {
        memset(Data, 0xFFFFFFFF, sizeof(Storage) * Elements());
    }

    void SetRange(uint32_t from, uint32_t to, bool set = true)
    {
        assert(from < Size());
        assert(to <= Size());
        assert(from <= to);
        while (from < to)
        {
            uint32_t fromInStorage = from % BitsPerStorage();
            uint32_t storageIndex = StorageIndexOfBit(from);
            uint32_t maxBitInStorage = (storageIndex + 1) * BitsPerStorage();
            Storage mask = (Storage)~0 << fromInStorage;
            if (to < maxBitInStorage)
            {
                Storage mask2 = ((Storage)1 << (to % BitsPerStorage())) - (Storage)1;
                mask &= mask2;
            }
            if (set)
            {
                Data[storageIndex] |= mask;
            }
            else
            {
                Data[storageIndex] &= ~mask;
            }
            from = maxBitInStorage;
        }
    }

     void SetBitAndUp(uint32_t bit, uint32_t count = ~0)
    {
        assert(bit < Size());
        count = count < Size() - bit ? count : Size() - bit;
        SetRange(bit, bit + count);
    }

    void SetBitAndDown(uint32_t bit, uint32_t count = ~0)
    {
        assert(bit < Size());
        count = bit < count ? bit : count;
        SetRange(bit - count, bit);
    }

    SetBitsIterator GetSetBitsIterator() const
    {
        return SetBitsIterator(this);
    }

    bool IsEqual(const BitField& other) const
    {
        for (uint32_t i = 0; i < Elements(); ++i)
        {
            if (Data[i] != other.Data[i])
            {
                return false;
            }
        }
        return true;
    }

    bool HasAnyBitSet() const
    {
        for (uint32_t i = 0; i < Elements(); ++i)
        {
            if (Data[i] > 0)
            {
                return true;
            }
        }
        return false;
    }

    bool HasNoBitSet() const
    {
        for (uint32_t i = 0; i < Elements(); ++i)
        {
            if (Data[i] > 0)
            {
                return false;
            }
        }
        return true;
    }

    bool MostSignificantBit(uint32_t* pIndex) const
    {
        for (int32_t i = (int)Elements() - 1; i >= 0; --i)
        {
            if (BitOperations::MostSignificantBit(Data[i], pIndex))
            {
                *pIndex += i * BitsPerStorage();
                return true;
            }
        }
        return false;
    }

    bool LeastSignificantBit(uint32_t* pIndex) const
    {
        for (uint32_t i = 0; i < Elements(); ++i)
        {
            if (BitOperations::LeastSignificantBit(Data[i], pIndex))
            {
                *pIndex += i * BitsPerStorage();
                return true;
            }
        }
        return false;
    }

    SetBitsIterator begin() const
    {
        return SetBitsIterator(this);
    }

    SetBitsIterator end() const
    {
        return SetBitsIterator(this, true);
    }

    bool operator[](uint32_t index) const
    {
        return GetBit(index);
    }

    bool operator==(const BitField& other) const
    {
        for (uint32_t i = 0; i < Elements(); ++i)
        {
            if (Data[i] != other.Data[i])
            {
                return false;
            }
        }
        return true;
    }

    bool operator!=(const BitField& other) const
    {
        for (uint32_t i = 0; i < Elements(); ++i)
        {
            if (Data[i] != other.Data[i])
            {
                return true;
            }
        }
        return false;
    }

    BitField& operator&=(const BitField& other)
    {
        for (uint32_t i = 0; i < Elements(); ++i)
        {
            Data[i] &= other.Data[i];
        }
        return *this;
    }

    BitField& operator|=(const BitField& other)
    {
        for (uint32_t i = 0; i < Elements(); ++i)
        {
            Data[i] |= other.Data[i];
        }
        return *this;
    }

    BitField& operator^=(const BitField& other)
    {
        for (uint32_t i = 0; i < Elements(); ++i)
        {
            Data[i] ^= other.Data[i];
        }
        return *this;
    }

    BitField operator&(const BitField& other) const
    {
        BitField out;
        for (uint32_t i = 0; i < Elements(); ++i)
        {
            out.Data[i] = Data[i] & other.Data[i];
        }
        return out;
    }

    BitField operator|(const BitField& other) const
    {
        BitField out;
        for (uint32_t i = 0; i < Elements(); ++i)
        {
            out.Data[i] = Data[i] | other.Data[i];
        }
        return out;
    }

    BitField operator^(const BitField& other) const
    {
        BitField out;
        for (uint32_t i = 0; i < Elements(); ++i)
        {
            out.Data[i] = Data[i] ^ other.Data[i];
        }
        return out;
    }

    BitField operator~() const
    {
        BitField out;
        for (uint32_t i = 0; i < Elements(); ++i)
        {
            out.Data[i] = ~Data[i];
        }
        return out;
    }

    static constexpr uint32_t Size()
    {
        return Bits;
    }

    static constexpr uint32_t Capacity()
    {
        return Bits;
    }

private:
    static constexpr uint32_t StorageIndexOfBit(uint32_t bit)
    {
        return bit / BitsPerStorage();
    }

    static constexpr uint32_t IndexOfBitInStorage(uint32_t bit)
    {
        return bit % BitsPerStorage();
    }

    static constexpr uint32_t BitsPerStorage()
    {
        return sizeof(Storage) * 8;
    }

    static constexpr Storage MakeBitmaskForStorage(uint32_t bit)
    {
        return (Storage)1 << IndexOfBitInStorage(bit);
    }

    static constexpr uint32_t Elements()
    {
        return (Bits + BitsPerStorage() - 1) / BitsPerStorage();
    }

    Storage Data[Elements()];
};