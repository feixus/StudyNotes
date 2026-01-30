#pragma once
#include "Core/Bitfield.h"

class CBT
{
public:
    using StorageType = uint32_t;
    constexpr static uint32_t NumBitsPerElement = sizeof(StorageType) * 8;

    static uint32_t ComputeSize(uint32_t maxDepth)
    {
        uint32_t numBits = 1u << (maxDepth + 2);
        return numBits / NumBitsPerElement;
    }

    void InitBare(uint32_t maxDepth, uint32_t initialDepth)
    {
        assert(initialDepth <= maxDepth);

        Storage.clear();
        Storage.resize(ComputeSize(maxDepth));
        Storage[0] |= 1 << maxDepth;

        uint32_t minRange = 1u << initialDepth;
        uint32_t maxRange = 1u << (initialDepth + 1);
        uint32_t interval = 1u << (maxDepth - initialDepth);
        for (uint32_t heapIndex = minRange; heapIndex < maxRange; heapIndex++)
        {
            SetData(heapIndex * interval, 1);
        }
    }

    void Init(uint32_t maxDepth, uint32_t initialDepth)
    {
        InitBare(maxDepth, initialDepth);
        SumReduction();
    }

    // get a value from the bag of bits. we must read from 2 elements in case the value crosses the boundary.
    uint32_t BinaryHeapGet(uint32_t bitOffset, uint32_t bitCount) const
    {
        auto BitfieldGet_Single = [](StorageType buffer, uint32_t bitOffset, uint32_t bitCount) -> uint32_t
        {
            assert(bitOffset + bitCount <= NumBitsPerElement);
            uint32_t bitMask = ~(~0u << bitCount);
            return (buffer >> bitOffset) & bitMask;
        };

        uint32_t elementIndex = bitOffset / NumBitsPerElement;
        uint32_t elementOffsetLSB = bitOffset % NumBitsPerElement;
        uint32_t bitCountLSB = Math::Min(bitCount, NumBitsPerElement - elementOffsetLSB);
        uint32_t bitCountMSB = bitCount - bitCountLSB;
        uint32_t valueLSB = BitfieldGet_Single(Storage[elementIndex], elementOffsetLSB, bitCountLSB);
        uint32_t valueMSB = BitfieldGet_Single(Storage[Math::Min(elementIndex + 1, (uint32_t)Storage.size() - 1)], 0, bitCountMSB);
        uint32_t val = valueLSB | (valueMSB << bitCountLSB);
        return val;
    }

    void BinaryHeapSet(uint32_t bitOffset, uint32_t bitCount, uint32_t value)
    {
        auto BitfieldSet_Single = [](StorageType& buffer, uint32_t bitOffset, uint32_t bitCount, uint32_t value)
        {
            assert(bitOffset + bitCount <= NumBitsPerElement);
            uint32_t bitMask = ~(~(~0u << bitCount) << bitOffset);
            buffer &= bitMask;
            buffer |= value << bitOffset;
        };

        uint32_t elementIndex = bitOffset / NumBitsPerElement;
        uint32_t elementOffsetLSB = bitOffset % NumBitsPerElement;
        // split the write into 2 elements in case it crosses the boundary.
        uint32_t bitCountLSB = Math::Min(bitCount, NumBitsPerElement - elementOffsetLSB);
        uint32_t bitCountMSB = bitCount - bitCountLSB;
        BitfieldSet_Single(Storage[elementIndex], elementOffsetLSB, bitCountLSB, value);
        BitfieldSet_Single(Storage[Math::Min(elementIndex + 1, (uint32_t)Storage.size() - 1)], 0, bitCountMSB, value >> bitCountLSB);
    }

    // sum reduction bottom to top. this can be parallelized per layer.
    void SumReduction()
    {
        int32_t depth = GetMaxDepth();
        uint32_t count = 1u << depth;

        // prepass
        for (uint32_t bitIndex = 0; bitIndex < count; bitIndex += (1 << 5))
        {
            uint32_t nodeIndex = bitIndex + count;
            uint32_t bitOffset = NodeBitIndex(nodeIndex);
            uint32_t elementIndex = bitOffset >> 5u;

            uint32_t bitField = Storage[elementIndex];
            bitField = (bitField & 0x55555555u) + ((bitField >> 1u) & 0x55555555u);
            uint32_t data = bitField;
            Storage[(bitOffset - count) >> 5u] = data;

            bitField = (bitField & 0x33333333u) + ((bitField >> 2u) & 0x33333333u);
            data = (bitField >> 0u) & (7u << 0u) |
					(bitField >> 1u) & (7u << 3u) |
                    (bitField >> 2u) & (7u << 6u) |
                    (bitField >> 3u) & (7u << 9u) |
                    (bitField >> 4u) & (7u << 12u) |
                    (bitField >> 5u) & (7u << 15u) |
                    (bitField >> 6u) & (7u << 18u) |
                    (bitField >> 7u) & (7u << 21u);

            BinaryHeapSet(NodeBitIndex(nodeIndex >> 2u), 24, data);

            bitField = (bitField & 0x0F0F0F0Fu) + ((bitField >> 4u) & 0x0F0F0F0Fu);
            data = (bitField >> 0u) & (15u << 0u) |
                    (bitField >> 4u) & (15u << 4u) |
                    (bitField >> 8u) & (15u << 8u) |
                    (bitField >> 12u) & (15u << 12u);

            BinaryHeapSet(NodeBitIndex(nodeIndex >> 3u), 16, data);

            bitField = (bitField & 0x00FF00FFu) + ((bitField >> 8u) & 0x00FF00FFu);
            data = (bitField >> 0u) & (31u << 0u) |
					(bitField >> 11u) & (31u << 5u);
            BinaryHeapSet(NodeBitIndex(nodeIndex >> 4u), 10, data);

            bitField = (bitField & 0x0000FFFFu) + ((bitField >> 16u) & 0x0000FFFFu);
            data = bitField;
            BinaryHeapSet(NodeBitIndex(nodeIndex >> 5u), 6, data);
        }

        depth -= 5;
        while (depth-- > 0)
        {
            count = 1u << depth;
            for (uint32_t k = count; k < count << 1u; k++)
            {
                SetData(k, GetData(LeftChildIndex(k)) + GetData(RightChildIndex(k)));
            }
        }
    }

    uint32_t GetData(uint32_t heapIndex) const
    {
        uint32_t offset = NodeBitIndex(heapIndex);
        uint32_t size = GetNodeBitSize(heapIndex);
        return BinaryHeapGet(offset, size);
    }

    void SetData(uint32_t heapIndex, uint32_t value)
    {
        uint32_t offset = NodeBitIndex(heapIndex);
        uint32_t size = GetNodeBitSize(heapIndex);
        BinaryHeapSet(offset, size, value);
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

    void BitfieldSet(uint32_t bitOffset, uint32_t value)
    {
        uint32_t elementIndex = bitOffset / NumBitsPerElement;
        uint32_t bitIndex = bitOffset % NumBitsPerElement;
        uint32_t bitMask = ~(1u << bitIndex);

        Storage[elementIndex] &= bitMask;
        Storage[elementIndex] |= (value << bitIndex);
    }

    void SplitNode(uint32_t heapIndex)
    {
        if (!IsCeilNode(heapIndex))
        {
            uint32_t rightChild = RightChildIndex(heapIndex);
            uint32_t bitfieldIndex = BitfieldHeapIndex(rightChild);
            uint32_t bit = NodeBitIndex(bitfieldIndex);
            BitfieldSet(bit, 1);
        }
    }

    void MergeNode(uint32_t heapIndex)
    {
        if (!IsRootNode(heapIndex))
        {
            uint32_t rightSibling = heapIndex | 1;
            uint32_t bitfieldIndex = BitfieldHeapIndex(rightSibling);
            uint32_t bit = NodeBitIndex(bitfieldIndex);
            BitfieldSet(bit, 0);
        }
    }

    uint32_t GetNodeBitSize(uint32_t heapIndex) const
    {
        uint32_t depth = GetDepth(heapIndex);
        return GetMaxDepth() - depth + 1;
    }

    uint32_t NodeBitIndex(uint32_t heapIndex) const
    {
        uint32_t depth = GetDepth(heapIndex);
        uint32_t t1 = 2u << depth;
        uint32_t t2 = 1u + GetMaxDepth() - depth;
        return t1 + heapIndex * t2;
    }

    uint32_t BitfieldHeapIndex(uint32_t heapIndex) const
    {
        return heapIndex * (1u << (GetMaxDepth() - GetDepth(heapIndex)));
    }

    uint32_t CeilNode(uint32_t heapIndex) const
    {
        uint32_t depth = GetDepth(heapIndex);
        return heapIndex << (GetMaxDepth() - depth);
    }

    uint32_t LeafIndexToHeapIndex(uint32_t leafIndex) const
    {
        uint32_t heapIndex = 1u;
        while (GetData(heapIndex) > 1u)
        {
            uint32_t leftChild = LeftChildIndex(heapIndex);
            uint32_t leftChildValue = GetData(leftChild);
            uint32_t bit = leafIndex < leftChildValue ? 0 : 1;

            heapIndex = leftChild | bit;
            leafIndex -= bit * leftChildValue;
        }
        return heapIndex;
    }

    // return true if the node is at the bottom of the tree and can't be split further.
    bool IsCeilNode(uint32_t heapIndex)
    {
        uint32_t msb;
        assert(BitOperations::MostSignificantBit(heapIndex, &msb));
        return msb == GetMaxDepth();
    }

    static bool IsRootNode(uint32_t heapIndex)
    {
        return heapIndex == 1u;
    }

    uint32_t NumNodes() const
    {
        return GetData(1);
    }

    uint32_t GetMaxDepth() const
    {
        uint32_t maxDepth;
        assert(BitOperations::LeastSignificantBit(Storage[0], &maxDepth));
        return maxDepth;
    }

    uint32_t NumBitfieldBits() const
    {
        return 1u << GetMaxDepth();
    }

    uint32_t GetMemoryUse() const
    {
        return (uint32_t)Storage.size() * sizeof(StorageType);
    }

    static uint32_t LeftChildIndex(uint32_t heapIndex)
    {
        return heapIndex << 1;
    }

    static uint32_t RightChildIndex(uint32_t heapIndex)
    {
        return (heapIndex << 1) | 1;
    }

    static uint32_t ParentIndex(uint32_t heapIndex)
    {
        return heapIndex >> 1;
    }

    static uint32_t SiblingIndex(uint32_t heapIndex)
    {
        return heapIndex ^ 1;
    }

    static uint32_t GetDepth(uint32_t heapIndex)
    {
        uint32_t msb;
        BitOperations::MostSignificantBit(heapIndex, &msb);
        return msb;
    }

    const void* GetData() const
    {
        return Storage.data();
    }

    void* GetData()
    {
        return Storage.data();
    }

private:
    std::vector<uint32_t> Storage;
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
            // if the base triangle is (v0, v1, v2), the longest edge bisection results in two triangles:
            // b = 0, c = 1: (v0, mid(v0,v2), v1), b=1, c=0: (v1, mid(v0,v2), v2)
            // the first vertex selects between v0 and v1, the last vertex selects between v1 and v2.
            float b = (float)bitSet;
            float c = 1.0f - b;
            return DirectX::XMFLOAT3X3(
                c,    b,    0.0f,
                0.5f, 0.0f, 0.5f,
                0.0f, c,    b);
        }

        inline Matrix GetWindingMatrix(uint32_t bit)
        {
            // if the base triangle is (v0, v1, v2), the winding order is to swap v0 and v2 depending on the depth being odd or even.
            // the even depths (0, 2, 4, ...) swap vertices, the odd depths (1, 3, 5, ...) keep the order.
            float b = (float)bit;
            float c = 1.0f - b;
            return DirectX::XMFLOAT3X3(
                c,    0.0f, b,
                0.0f, 1.0f, 0.0f,
                b,    0.0f, c);
        }

        inline Matrix GetSquareMatrix(uint32_t quadBit)
        {
            // quadrant selection: the left half nodes of one depth level from tree represent the left triangle of the square,
            // the right half nodes represent the right triangle of the square.
            float b = float(quadBit);
            float c = 1.0f - b;
            return DirectX::XMFLOAT3X3(
                c, 0.0f, b,
                b, c,    b,
                b, 0.0f, c);
        }
    }

    inline Matrix GetMatrix(uint32_t heapIndex)
    {
        uint32_t d;
        BitOperations::MostSignificantBit(heapIndex, &d);

        int32_t bitID = Math::Max(0, (int32_t)d - 1);
        Matrix m = Private::GetSquareMatrix(Private::GetBitValue(heapIndex, bitID));

        for (bitID = d - 2; bitID >= 0; bitID--)
        {
            m = Private::GetSplitMatrix(Private::GetBitValue(heapIndex, bitID)) * m;
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

    struct NeighborIDs
    {
        uint32_t Left;
        uint32_t Right;
        uint32_t Edge;
        uint32_t Current;
    };

    inline NeighborIDs GetNeighbors(const NeighborIDs& neighbors, uint32_t bit)
    {
        uint32_t n1 = neighbors.Left;
        uint32_t n2 = neighbors.Right;
        uint32_t n3 = neighbors.Edge;
        uint32_t n4 = neighbors.Current;

        uint32_t b2 = n2 == 0u ? 0u : 1u;
        uint32_t b3 = n3 == 0u ? 0u : 1u;

        if (bit == 0)
        {
            return NeighborIDs{ (n4 << 1u) | 1, (n3 << 1u) | b3, (n2 << 1u) | b2, (n4 << 1u) };
        }
        else
        {
            return NeighborIDs{ (n3 << 1u), (n4 << 1u), (n1 << 1u), (n4 << 1u) | 1u };
        }
    }

    inline NeighborIDs GetNeighbors(uint32_t heapIndex)
    {
        int32_t depth = (int32_t)CBT::GetDepth(heapIndex);
        int32_t bitID = depth > 0u ? depth - 1u : 0u;
        uint32_t b = Private::GetBitValue(heapIndex, bitID);
        NeighborIDs neighbors{ 0u, 0u, 3u - b, 2u + b };    // heapIndex 2 and 3 at the depth 1.

        for (bitID = depth - 2; bitID >= 0; bitID--)
        {
            uint32_t bitValue = Private::GetBitValue(heapIndex, bitID);
            neighbors = GetNeighbors(neighbors, bitValue);
        }

        return neighbors;
    }

    inline uint32_t GetEdgeNeighbor(uint32_t heapIndex)
    {
        return GetNeighbors(heapIndex).Edge;
    }

    struct DiamondIDs
    {
        uint32_t Base;
        uint32_t Top;
    };

    inline DiamondIDs GetDiamond(uint32_t heapIndex)
    {
        uint32_t parent = CBT::ParentIndex(heapIndex);
        uint32_t edge = GetEdgeNeighbor(parent);
        edge = edge > 0u ? edge : parent;
        return DiamondIDs{ parent, edge };
    }

    inline void CBTSplitConformed(CBT& cbt, uint32_t heapIndex)
    {
        if (!cbt.IsCeilNode(heapIndex))
        {
            const uint32_t minNodeID = 1u;

            cbt.SplitNode(heapIndex);

            uint32_t edgeNeighbor = GetEdgeNeighbor(heapIndex);
            while (edgeNeighbor > minNodeID)
            {
                cbt.SplitNode(edgeNeighbor);
                edgeNeighbor = CBT::ParentIndex(edgeNeighbor);
                if (edgeNeighbor > minNodeID)
                {
                    cbt.SplitNode(edgeNeighbor);
                    edgeNeighbor = GetEdgeNeighbor(edgeNeighbor);
                }
            }
        }
    }

    inline void CBTMergeConformed(CBT& cbt, uint32_t heapIndex)
    {
        if (cbt.GetDepth(heapIndex) > 1)
        {
            DiamondIDs diamond = GetDiamond(heapIndex);
            if (cbt.GetData(diamond.Base) <= 2 && cbt.GetData(diamond.Top) <= 2)
            {
				cbt.MergeNode(heapIndex);
                // if splitting/merging is not alternated, it causes bugs and this extra hack was necessary
				// cbt.MergeNode(cbt.RightChildIndex(diamond.Top));
            }
        }
    }

    inline bool PointInTriangle(const Vector2& pt, uint32_t heapIndex, float scale)
    {
        float d1, d2, d3;
        bool has_neg, has_pos;

        Vector3 a, b, c;
        GetTriangleVertices(heapIndex, a, b, c);
        Vector2 v1(a), v2(b), v3(c);
        v1 *= scale;
        v2 *= scale;
        v3 *= scale;

        auto sign = [](const Vector2& p1, const Vector2& p2, const Vector2& p3) -> float
        {
            return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
        };

        d1 = sign(pt, v1, v2);
        d2 = sign(pt, v2, v3);
        d3 = sign(pt, v3, v1);

        has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
        has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);

        return !(has_neg && has_pos);
    }
}
