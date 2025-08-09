#pragma once

class Graphics;
class Buffer;
class Camera;
class RootSignature;
class PipelineState;
class RGGraph;
struct Light;

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

struct DebugRay
{
    DebugRay(const Vector3& start, const Vector3& direction, const uint32_t color)
        : Start(start), Direction(direction), Color(color)
    {}

    Vector3 Start;
    Vector3 Direction;
    uint32_t Color;
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

struct DebugSphere
{
    DebugSphere(const Vector3& center, float radius)
        : Center(center), Radius(radius)
    {}

    Vector3 GetPoint(const float theta, const float phi) const
    {
        return Center + GetLocalPoint(theta, phi);
    }

    Vector3 GetLocalPoint(const float theta, const float phi) const
    {
        return Vector3(
            Radius * sin(theta) * sin(phi),
            Radius * cos(phi),
            Radius * cos(theta) * sin(phi)
        );
    }

    Vector3 Center;
    float Radius;
};

class DebugRenderer
{
public:
    static DebugRenderer& Instance();

    void Initialize(Graphics* pGraphics);
    void Render(RGGraph& graph);
    void EndFrame();

    void SetCamera(const Camera* pCamera) { m_pCamera = pCamera; }

    void AddLine(const Vector3& start, const Vector3& end, const Color& color);
    void AddLine(const Vector3& start, const Vector3& end, const Color& colorStart, const Color& colorEnd);
    void AddRay(const Vector3& start, const Vector3& direction, const Color& color);
    void AddTriangle(const Vector3& a, const Vector3& b, const Vector3& c, const Color& color, const bool solid = true);
    void AddTriangle(const Vector3& a, const Vector3& b, const Vector3& c, const Color& colorA, const Color& colorB, const Color& colorC, const bool solid = true);
    void AddPolygon(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, const Color& color);
    void AddBox(const Vector3& position, const Vector3& extents, const Color& color, const bool solid = false);
    void AddBoundingBox(const BoundingBox& boundingBox, const Color& color, const bool solid = false);
    void AddBoundingBox(const BoundingBox& boundingBox, const Matrix& transform, const Color& color, const bool solid = false);
    void AddSphere(const Vector3& position, const float radius, const int slices, const int stacks, const Color& color, const bool solid = false);
    void AddFrustrum(const BoundingFrustum& frustrum, const Color& color);
    void AddAxisSystem(const Matrix& transform, const float lineLength = 1.0f);
    void AddWireCylinder(const Vector3& position, const Vector3& direction, const float height, const float radius, const int segments, const Color& color);
    void AddWireCone(const Vector3& position, const Vector3& direction, const float height, const float angle, const int segments, const Color& color);
    void AddBone(const Matrix& matrix, const float length, const Color& color);
    void AddLight(const Light& light);

private:
    DebugRenderer() = default;

    Graphics* m_pGraphics;
    const Camera* m_pCamera{nullptr};

    int m_LinePrimitives{0};
    std::vector<DebugLine> m_Lines;
    int m_TrianglePrimitives{0};
    std::vector<DebugTriangle> m_Triangles;

    std::unique_ptr<PipelineState> m_pTrianglesPSO;
    std::unique_ptr<PipelineState> m_pLinesPSO;
    std::unique_ptr<RootSignature> m_pRS;
};