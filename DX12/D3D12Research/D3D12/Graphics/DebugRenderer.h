#pragma once

class Graphics;
class RootSignature;
class PipelineState;
class RGGraph;
struct Light;
class GraphicsTexture;

struct DebugLine
{
    DebugLine(const Vector3& start, const Vector3& end, const uint32_t colorStart, const uint32_t colorEnd)
        : Start(start), ColorStart(colorStart), End(end), ColorEnd(colorEnd)
    {}

    Vector3 Start;
    uint32_t ColorStart;
    Vector3 End;
    uint32_t ColorEnd;
};

struct DebugTriangle
{
    DebugTriangle(const Vector3& a, const Vector3& b, const Vector3& c, const uint32_t colorA, const uint32_t colorB, const uint32_t colorC)
        : A(a), ColorA(colorA), B(b), ColorB(colorB), C(c), ColorC(colorC)
    {}

    Vector3 A;
    uint32_t ColorA;
    Vector3 B;
    uint32_t ColorB;
    Vector3 C;
    uint32_t ColorC;
};


class DebugRenderer
{
public:
    static DebugRenderer* Get();

    void Initialize(Graphics* pGraphics);
    void Render(RGGraph& graph, const Matrix& viewProjection, GraphicsTexture* pTarget, GraphicsTexture* pDepth);

    void AddLine(const Vector3& start, const Vector3& end, const Color& color);
    void AddLine(const Vector3& start, const Vector3& end, const Color& colorStart, const Color& colorEnd);
    void AddRay(const Vector3& start, const Vector3& direction, const Color& color);
    void AddTriangle(const Vector3& a, const Vector3& b, const Vector3& c, const Color& color, bool solid = true);
    void AddTriangle(const Vector3& a, const Vector3& b, const Vector3& c, const Color& colorA, const Color& colorB, const Color& colorC, bool solid = true);
    void AddPolygon(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, const Color& color);
    void AddBox(const Vector3& position, const Vector3& extents, const Color& color, bool solid = false);
    void AddBoundingBox(const BoundingBox& boundingBox, const Color& color, bool solid = false);
    void AddBoundingBox(const BoundingBox& boundingBox, const Matrix& transform, const Color& color, bool solid = false);
    void AddSphere(const Vector3& position, float radius, int slices, int stacks, const Color& color, bool solid = false);
    void AddFrustrum(const BoundingFrustum& frustrum, const Color& color);
    void AddAxisSystem(const Matrix& transform, float lineLength = 1.0f);
    void AddWireCylinder(const Vector3& position, const Vector3& direction, float height, float radius, int segments, const Color& color);
    void AddWireCone(const Vector3& position, const Vector3& direction, float height, float angle, int segments, const Color& color);
    void AddBone(const Matrix& matrix, float length, const Color& color);
    void AddLight(const Light& light);

private:
    DebugRenderer() = default;

    std::vector<DebugLine> m_Lines;
    std::vector<DebugTriangle> m_Triangles;

    std::unique_ptr<PipelineState> m_pTrianglesPSO;
    std::unique_ptr<PipelineState> m_pLinesPSO;
    std::unique_ptr<RootSignature> m_pRS;
};