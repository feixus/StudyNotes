#pragma once

struct CBT
{
    static uint32_t Exp2(uint32_t exp)
    {
        return (uint32_t)exp2(exp);
    }

    // most significant bit
    static uint32_t MSB(uint32_t value)
    {
         if (value == 0)
         {
             return -1;
         }

        return std::bit_width(value) - 1;
    }

    CBT(uint32_t maxDepth) : MaxDepth(maxDepth), Size(Exp2(maxDepth + 1))
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
        uint32_t minRange = Exp2(depth);
        uint32_t maxRange = Exp2(depth + 1);
        uint32_t interval = Exp2(MaxDepth - depth);
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
            uint32_t minRange = Exp2(d);
            uint32_t maxRange = Exp2(d + 1);
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
        int32_t msb = MSB(heapID);
        assert(msb != -1);
        return heapID * Exp2(MaxDepth - msb);
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
        return Exp2(MaxDepth);
    }

    uint32_t MaxDepth;
    uint32_t Size;
    uint8_t* Bits{nullptr};
};
