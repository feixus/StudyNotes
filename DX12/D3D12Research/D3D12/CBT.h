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
    }

    template<typename HeapFn>
    void Update(HeapFn&& fn)
    {
		IterateLeaves(fn);
		SumReduction();
    }

	template<typename HeapFn>
	void IterateLeaves(HeapFn&& fn)
	{
		uint32_t numNodes = NumNodes();
		for (uint32_t leafIndex = 0; leafIndex < numNodes; leafIndex++)
		{
			uint32_t heapIndex = LeafIndexToHeapIndex(leafIndex);
			fn(heapIndex);
		}
	}

    uint32_t LeafIndexToHeapIndex(uint32_t leafIndex) const
    {
        uint32_t heapID = 1;
        while (Bits[heapID] > 1)
        {
            uint32_t leftChildValue = Bits[LeftChildID(heapID)];
            if (leafIndex < leftChildValue)
            {
                heapID = LeftChildID(heapID);
            }
            else
            {
                leafIndex -= leftChildValue;
                heapID = RightChildID(heapID);
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
                c,    b,    0.0f,
                0.5f, 0.0f, 0.5f,
                0.0f, c,    b);
        }

        inline Matrix GetWindingMatrix(uint32_t bit)
        {
            float b = (float)bit;
            float c = 1.0f - b;
            return DirectX::XMFLOAT3X3(
                c,    0.0f, b,
                0.0f, 1.0f, 0.0f,
                b,    0.0f, c);
        }

        inline Matrix GetSquareMatrix(uint32_t quadBit)
        {
            float b = float(quadBit);
            float c = 1.0f - b;

            return DirectX::XMFLOAT3X3(
                c, 0.0f, b,
                b, c,    b,
                b, 0.0f, c);
        }
    }

    inline Matrix GetMatrix(uint32_t heapID)
    {
        uint32_t d;
        BitOperations::MostSignificantBit(heapID, &d);

        int32_t bitID = Math::Max(0, (int32_t)d - 1);
        Matrix m = Private::GetSquareMatrix(Private::GetBitValue(heapID, bitID));

        for (bitID = d - 2; bitID >= 0; bitID--)
        {
            m = Private::GetSplitMatrix(Private::GetBitValue(heapID, bitID)) * m;
        }
        return Private::GetWindingMatrix((d ^ 1) & 1) * m;
    }

    inline void GetTriangleVertices(uint32_t heapIndex, Vector3& a, Vector3& b, Vector3& c)
    {
        static const Matrix baseTriangle = DirectX::XMFLOAT3X3{
            0, 1, 0,
            0, 0, 0,
            1, 0, 0
        };
        Matrix t = GetMatrix(heapIndex) * baseTriangle;
        a = Vector3(t._11, t._12, t._13);
        b = Vector3(t._21, t._22, t._23);
        c = Vector3(t._31, t._32, t._33);
    }

    float sign(const Vector2& p1, const Vector2& p2, const Vector2& p3)
    {
        return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
    }

    bool PointInTriangle(const Vector2& pt, const Vector2& v1, const Vector2& v2, const Vector2& v3)
    {
        float d1, d2, d3;
        bool has_neg, has_pos;

        d1 = sign(pt, v1, v2);
        d2 = sign(pt, v2, v3);
        d3 = sign(pt, v3, v1);

        has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
        has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);

        return !(has_neg && has_pos);
    }
}
