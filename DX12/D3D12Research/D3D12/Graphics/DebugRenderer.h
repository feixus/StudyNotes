#pragma once

class GraphicsDevice;
class RootSignature;
class PipelineState;
class RGGraph;
struct Light;
class GraphicsTexture;

struct IntColor
{
	IntColor(const Color& color) : Color(Math::EncodeRGBA(color)) {}
	IntColor(uint32_t color = 0) : Color(color) {}
	operator uint32_t() const { return Color; }
	operator Color() const { return Math::DecodeRGBA(Color); }

	uint32_t Color;
};

class DebugRenderer
{
private:

	struct DebugLine
	{
		DebugLine(const Vector3& start, const Vector3& end, const uint32_t color)
			: Start(start), ColorA(color), End(end), ColorB(color)
		{}

		Vector3 Start;
		uint32_t ColorA;
		Vector3 End;
		uint32_t ColorB;
	};

	struct DebugTriangle
	{
		DebugTriangle(const Vector3& a, const Vector3& b, const Vector3& c, const uint32_t color)
			: A(a), ColorA(color), B(b), ColorB(color), C(c), ColorC(color)
		{}

		Vector3 A;
		uint32_t ColorA;
		Vector3 B;
		uint32_t ColorB;
		Vector3 C;
		uint32_t ColorC;
	};

public:
    static DebugRenderer* Get();

    void Initialize(GraphicsDevice* pParent);
	void Shutdown();
    void Render(RGGraph& graph, const Matrix& viewProjection, GraphicsTexture* pTarget, GraphicsTexture* pDepth);

    void AddLine(const Vector3& start, const Vector3& end, const IntColor& color);
    void AddRay(const Vector3& start, const Vector3& direction, const IntColor& color);
    void AddTriangle(const Vector3& a, const Vector3& b, const Vector3& c, const IntColor& color, bool solid = true);
    void AddPolygon(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, const IntColor& color);
    void AddBox(const Vector3& position, const Vector3& extents, const IntColor& color, bool solid = false);
    void AddBoundingBox(const BoundingBox& boundingBox, const IntColor& color, bool solid = false);
    void AddBoundingBox(const BoundingBox& boundingBox, const Matrix& transform, const IntColor& color, bool solid = false);
    void AddSphere(const Vector3& position, float radius, int slices, int stacks, const IntColor& color, bool solid = false);
    void AddFrustrum(const BoundingFrustum& frustrum, const IntColor& color);
    void AddAxisSystem(const Matrix& transform, float lineLength = 1.0f);
    void AddWireCylinder(const Vector3& position, const Vector3& direction, float height, float radius, int segments, const IntColor& color);
    void AddWireCone(const Vector3& position, const Vector3& direction, float height, float angle, int segments, const IntColor& color);
    void AddBone(const Matrix& matrix, float length, const IntColor& color);
    void AddLight(const Light& light, const IntColor& color = Colors::Yellow);

private:
    DebugRenderer() = default;

    std::vector<DebugLine> m_Lines;
    std::vector<DebugTriangle> m_Triangles;

    PipelineState* m_pTrianglesPSO{nullptr};
    PipelineState* m_pLinesPSO{nullptr};
    std::unique_ptr<RootSignature> m_pRS;
};
