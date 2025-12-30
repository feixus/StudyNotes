#pragma once

struct CBT
{
    CBT(uint32_t maxDepth) : MaxDepth(maxDepth), Size(Math::Exp2(maxDepth + 1))
    {
        Bits = new uint8_t[Size];
        InitAtDepth(0);
    }

    ~CBT()
    {
        delete[] Bits;
    }

    void InitAtDepth(uint32_t depth)
    {
        assert(depth <= MaxDepth);
        memset(Bits, 0, Size * sizeof(uint8_t));
        Bits[0] = (uint8_t)MaxDepth;
        uint32_t minRange = Math::Exp2(depth);
        uint32_t maxRange = Math::Exp2(depth + 1);
        uint32_t interval = Math::Exp2(MaxDepth - depth);
        for (uint32_t heapID = minRange; heapID < maxRange; heapID++)
        {
            Bits[heapID * interval] = 1;
        }
        SumReduction();
    }

    void SumReduction()
    {
        int32_t d = MaxDepth - 1;
        while (d >= 0)
        {
            uint32_t minRange = Math::Exp2(d);
            uint32_t maxRange = Math::Exp2(d + 1);
            for (uint32_t k = minRange; k < maxRange; k++)
            {
                Bits[k] = Bits[LeftChildID(k)] + Bits[RightChildID(k)];
            }
            d--;
        }

		for (uint32_t i = 0; i < Size; i++)
		{
			E_LOG(Info, "%d = %d", i, Bits[i]);
		}
    }

    template<typename HeapFn>
    void Update(HeapFn&& fn)
    {
        uint32_t numNodes = NumNodes();
		for (uint32_t leafIndex = 0; leafIndex < numNodes; leafIndex++)
		{
			uint32_t heapIndex = LeafIndexToHeapIndex(leafIndex);
			fn(heapIndex);
		}
		SumReduction();
    }

    uint32_t LeafIndexToHeapIndex(uint32_t leafIndex) const
    {
        uint32_t heapID = 1;
        while (Bits[heapID] > 1)
        {
            if (leafIndex < Bits[heapID])
            {
                heapID = 2 * heapID;
            }
            else
            {
                leafIndex -= Bits[heapID];
                heapID = 2 * heapID + 1;
            }
        }
        return heapID;
    }

    uint32_t EncodeNode(uint32_t heapID) const
    {
        uint32_t leafIndex = 0;
        while (heapID > 1)
        {
            if (leafIndex % 2 == 0)
            {
                leafIndex = leafIndex  + Bits[heapID ^ 1];
            }
            heapID = heapID / 2;
        }
        return leafIndex;
    }

    uint32_t BitfieldHeapID(uint32_t heapID) const
    {
        uint32_t msb = 0;
        assert(BitOperations::MostSignificantBit(heapID, &msb));
        return heapID * Math::Exp2(MaxDepth - msb);
    }

    void SplitNode(uint32_t heapID)
    {
        uint32_t rightChild = RightChildID(heapID);
        uint32_t bit = BitfieldHeapID(rightChild);
        Bits[bit] = 1;
    }

    void MergeNode(uint32_t heapID)
    {
        uint32_t rightSibling = heapID | 1;
        uint32_t bit = BitfieldHeapID(rightSibling);
        Bits[bit] = 0;
    }

    static uint32_t LeftChildID(uint32_t heapID)
    {
        return heapID * 2;
    }

    static uint32_t RightChildID(uint32_t heapID)
    {
        return heapID * 2 + 1;
    }

    static uint32_t ParentID(uint32_t heapID)
    {
        return heapID >> 1;
    }

    static uint32_t SiblingID(uint32_t heapID)
    {
        return heapID ^ 1;
    }

    static uint32_t GetDepth(uint32_t heapID)
    {
        return (uint32_t)floor(log2(heapID));
    }

    bool IsLeafNode(uint32_t heapID)
    {
        return Bits[heapID] == 1;
    }

    uint32_t NumNodes() const
    {
        return Bits[1];
    }

    uint32_t NumBitfieldBits() const
    {
        return Math::Exp2(MaxDepth);
    }

    void GetElementRange(uint32_t heapID, uint32_t& begin, uint32_t& size) const
    {
        uint32_t depth = GetDepth(heapID);
        size = MaxDepth - depth + 1;
        begin = Math::Exp2(depth + 1) + heapID * size;
    }

    uint32_t MaxDepth;
    uint32_t Size;
    uint8_t* Bits{nullptr};
};

namespace LEB
{
    namespace Private
    {
        inline bool GetBitValue(uint32_t value, uint32_t bit)
        {
            return (value >> bit) & 1u;
        }

        inline Matrix GetSplitMatrix(bool bitSet)
        {
            float b = (float)bitSet;
            float c = 1.0f - b;
            return DirectX::XMFLOAT3X3(
                c, b, 0.0f,
                0.5f, 0.0f, 0.5f,
                0.0f, c, b);
        }

        inline Matrix GetWindingMatrix(uint32_t bit)
        {
            float b = (float)bit;
            float c = 1.0f - b;
            return DirectX::XMFLOAT3X3(
                c, 0.0f, b,
                0.0f, 1.0f, 0.0f,
                b, 0.0f, c);
        }
    }

    inline Matrix GetMatrix(uint32_t heapID)
    {
        Matrix m = Matrix::Identity;
        uint32_t d;
        BitOperations::MostSignificantBit(heapID, &d);
        for (int32_t bitID = d - 1; bitID >= 0; bitID--)
        {
            m = Private::GetSplitMatrix(Private::GetBitValue(heapID, bitID)) * m;
        }
        return Private::GetWindingMatrix(d & 1) * m;
    }
}
