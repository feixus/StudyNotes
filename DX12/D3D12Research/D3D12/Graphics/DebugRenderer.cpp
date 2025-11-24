#include "stdafx.h"
#include "DebugRenderer.h"
#include "Light.h"
#include "RenderGraph/RenderGraph.h"
#include "Core/GraphicsBuffer.h"
#include "Core/Graphics.h"
#include "Core/Shader.h"
#include "Core/RootSignature.h"
#include "Core/PipelineState.h"
#include "Core/CommandContext.h"


struct DebugSphere
{
	DebugSphere(const Vector3& center, float radius)
		: Center(center), Radius(radius)
	{}

	Vector3 GetPoint(float theta, float phi) const
	{
		return Center + GetLocalPoint(theta, phi);
	}

	Vector3 GetLocalPoint(float theta, float phi) const
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

DebugRenderer* DebugRenderer::Get()
{
    static DebugRenderer instance;
    return &instance;
}

void DebugRenderer::Initialize(Graphics* pGraphics)
{
    VertexElementLayout inputLayout;
    inputLayout.AddVertexElement("POSITION", DXGI_FORMAT_R32G32B32_FLOAT);
    inputLayout.AddVertexElement("COLOR", DXGI_FORMAT_R32_UINT);

	// shaders
	Shader* pVertexShader = pGraphics->GetShaderManager()->GetShader("DebugRenderer.hlsl", ShaderType::Vertex, "VSMain");
	Shader* pPixelShader = pGraphics->GetShaderManager()->GetShader("DebugRenderer.hlsl", ShaderType::Pixel, "PSMain");

	// root signature
	m_pRS = std::make_unique<RootSignature>(pGraphics);
	m_pRS->FinalizeFromShader("Diffuse", pVertexShader);

	// opaque
    PipelineStateInitializer psoDesc;
	psoDesc.SetInputLayout(inputLayout);
	psoDesc.SetRootSignature(m_pRS->GetRootSignature());
	psoDesc.SetVertexShader(pVertexShader);
	psoDesc.SetPixelShader(pPixelShader);
	psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	psoDesc.SetDepthWrite(true);
	psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
	psoDesc.SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, pGraphics->GetMultiSampleCount());
	psoDesc.SetName("Triangles DebugRenderer PSO");
	m_pTrianglesPSO = pGraphics->CreatePipeline(psoDesc);

    psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE);
    psoDesc.SetName("Lines DebugRenderer PSO");
    m_pLinesPSO = pGraphics->CreatePipeline(psoDesc);
}

void DebugRenderer::Render(RGGraph& graph, const Matrix& viewProjection, GraphicsTexture* pTarget, GraphicsTexture* pDepth)
{
    int linePrimitives = (int)m_Lines.size() * 2;
    int trianglePrimitives = (int)m_Triangles.size() * 3;
    int totalPrimitives = linePrimitives + trianglePrimitives;
    if (totalPrimitives == 0)
    {
        return;
    }

    RGPassBuilder debugRendering = graph.AddPass("Debug Rendering");
	debugRendering.Bind([=](CommandContext& context, const RGPassResource& resources)
		{
			context.InsertResourceBarrier(pDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
			context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);

			context.BeginRenderPass(RenderPassInfo(pTarget, RenderPassAccess::Load_Store, pDepth, RenderPassAccess::Load_Store, false));
			context.SetGraphicsRootSignature(m_pRS.get());

			context.SetGraphicsDynamicConstantBufferView(0, viewProjection);

            constexpr uint32_t VertexStride = (uint32_t)(sizeof(DebugLine) * 0.5f);
			if (linePrimitives != 0)
			{
				context.SetDynamicVertexBuffer(0, linePrimitives, VertexStride, m_Lines.data());
				context.SetPipelineState(m_pLinesPSO);
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
				context.Draw(0, linePrimitives);
			}
			if (trianglePrimitives != 0)
			{
				context.SetDynamicVertexBuffer(0, trianglePrimitives, VertexStride, m_Triangles.data());
				context.SetPipelineState(m_pTrianglesPSO);
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.Draw(0, trianglePrimitives);
			}

			context.EndRenderPass();
        });

    m_Lines.clear();
    m_Triangles.clear();
}

void DebugRenderer::AddLine(const Vector3& start, const Vector3& end, const IntColor& color)
{
    m_Lines.push_back(DebugLine(start, end, color));
}

void DebugRenderer::AddRay(const Vector3& start, const Vector3& direction, const IntColor& color)
{
    AddLine(start, start + direction, color);
}

void DebugRenderer::AddTriangle(const Vector3& a, const Vector3& b, const Vector3& c, const IntColor& color, bool solid)
{
    if (solid)
    {
        m_Triangles.push_back(DebugTriangle(a, b, c, color));
    }
    else
    {
        AddLine(a, b, color);
        AddLine(b, c, color);
        AddLine(c, a, color);
    }
}

void DebugRenderer::AddPolygon(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, const IntColor& color)
{
    AddTriangle(a, b, c, color);
    AddTriangle(a, c, d, color);
}

void DebugRenderer::AddBox(const Vector3& position, const Vector3& extents, const IntColor& color, bool solid)
{
    Vector3 minV(position.x - extents.x, position.y - extents.y, position.z - extents.z);
    Vector3 maxV(position.x + extents.x, position.y + extents.y, position.z + extents.z);

    Vector3 v1(maxV.x, minV.y, minV.z);
    Vector3 v2(maxV.x, maxV.y, minV.z);
    Vector3 v3(minV.x, maxV.y, minV.z);
    Vector3 v4(minV.x, minV.y, maxV.z);
    Vector3 v5(maxV.x, minV.y, maxV.z);
    Vector3 v6(minV.x, maxV.y, maxV.z);

    if (!solid)
    {
        AddLine(minV, v1, color);
        AddLine(v1, v2, color);
        AddLine(v2, v3, color);
        AddLine(v3, minV, color);
        AddLine(v4, v5, color);
        AddLine(v5, maxV, color);
        AddLine(maxV, v6, color);
        AddLine(v6, v4, color);
        AddLine(minV, v4, color);
        AddLine(v1, v5, color);
        AddLine(v2, maxV, color);
        AddLine(v3, v6, color);
    }
    else
    {
        AddPolygon(minV, v1, v2, v3, color);
        AddPolygon(v4, v5, maxV, v6, color);
        AddPolygon(minV, v4, v6, v3, color);
        AddPolygon(v1, v5, maxV, v2, color);
        AddPolygon(v3, v2, maxV, v6, color);
        AddPolygon(minV, v1, v5, v4, color);
    }
}

void DebugRenderer::AddBoundingBox(const BoundingBox& boundingBox, const IntColor& color, bool solid)
{
    AddBox(boundingBox.Center, boundingBox.Extents, color, solid);
}

void DebugRenderer::AddBoundingBox(const BoundingBox& boundingBox, const Matrix& transform, const IntColor& color, bool solid)
{
    Vector3 minV(boundingBox.Center.x - boundingBox.Extents.x, boundingBox.Center.y - boundingBox.Extents.y, boundingBox.Center.z - boundingBox.Extents.z);
    Vector3 maxV(boundingBox.Center.x + boundingBox.Extents.x, boundingBox.Center.y + boundingBox.Extents.y, boundingBox.Center.z + boundingBox.Extents.z);
    
    Vector3 v0(Vector3::Transform(minV, transform));
    Vector3 v1(Vector3::Transform(Vector3(maxV.x, minV.y, minV.z), transform));
    Vector3 v2(Vector3::Transform(Vector3(maxV.x, maxV.y, minV.z), transform));
    Vector3 v3(Vector3::Transform(Vector3(minV.x, maxV.y, minV.z), transform));
    Vector3 v4(Vector3::Transform(Vector3(minV.x, minV.y, maxV.z), transform));
    Vector3 v5(Vector3::Transform(Vector3(maxV.x, minV.y, maxV.z), transform));
	Vector3 v6(Vector3::Transform(Vector3(minV.x, maxV.y, maxV.z), transform));
    Vector3 v7(Vector3::Transform(maxV, transform));

    if (!solid)
    {
        AddLine(v0, v1, color);
        AddLine(v1, v2, color);
        AddLine(v2, v3, color);
        AddLine(v3, v0, color);
        AddLine(v4, v5, color);
        AddLine(v5, v7, color);
        AddLine(v7, v6, color);
        AddLine(v6, v4, color);
        AddLine(v0, v4, color);
        AddLine(v1, v5, color);
        AddLine(v2, v7, color);
        AddLine(v3, v6, color);
    }
    else
    {
        AddPolygon(v0, v1, v2, v3, color);
        AddPolygon(v4, v5, v7, v6, color);
        AddPolygon(v0, v4, v6, v3, color);
        AddPolygon(v1, v5, v7, v2, color);
        AddPolygon(v3, v2, v7, v6, color);
        AddPolygon(v0, v1, v5, v4, color);
    }
}

void DebugRenderer::AddSphere(const Vector3& position, float radius, int slices, int stacks, const IntColor& color, bool solid)
{
    DebugSphere sphere(position, radius);

	float jStep = Math::PI / slices;
	float iStep = Math::PI / stacks;

    if (!solid)
    {
        for (float j = 0; j < Math::PI; j += jStep)
        {
            for (float i = 0; i < Math::PI * 2; i += iStep)
            {
                Vector3 p1 = sphere.GetPoint(i, j);
                Vector3 p2 = sphere.GetPoint(i + iStep, j);
                Vector3 p3 = sphere.GetPoint(i, j + jStep);
                Vector3 p4 = sphere.GetPoint(i + iStep, j + jStep);

                AddLine(p1, p2, color);
                AddLine(p3, p4, color);
                AddLine(p1, p3, color);
                AddLine(p2, p4, color);
            }
        }
    }
    else
    {
        for (float j = 0; j < Math::PI; j += jStep)
        {
            for (float i = 0; i < Math::PI * 2; i += iStep)
            {
                Vector3 p1 = sphere.GetPoint(i, j);
                Vector3 p2 = sphere.GetPoint(i + iStep, j);
                Vector3 p3 = sphere.GetPoint(i, j + jStep);
                Vector3 p4 = sphere.GetPoint(i + iStep, j + jStep);

                AddPolygon(p2, p1, p3, p4, color);
            }
        }
    }
}

void DebugRenderer::AddFrustrum(const BoundingFrustum& frustrum, const IntColor& color)
{
    std::vector<Vector3> corners(BoundingFrustum::CORNER_COUNT);
    frustrum.GetCorners(corners.data());

    AddLine(corners[0], corners[1], color);
    AddLine(corners[1], corners[2], color);
    AddLine(corners[2], corners[3], color);
    AddLine(corners[3], corners[0], color);
    AddLine(corners[4], corners[5], color);
    AddLine(corners[5], corners[6], color);
    AddLine(corners[6], corners[7], color);
    AddLine(corners[7], corners[4], color);
    AddLine(corners[0], corners[4], color);
    AddLine(corners[1], corners[5], color);
    AddLine(corners[2], corners[6], color);
    AddLine(corners[3], corners[7], color);
}

void DebugRenderer::AddAxisSystem(const Matrix& transform, float lineLength)
{
    Matrix newMatrix = Matrix::CreateScale(Math::ScaleFromMatrix(transform));
    newMatrix.Invert(newMatrix);
    newMatrix *= transform;

    Vector3 origin(Vector3::Transform(Vector3::Zero, transform));
    Vector3 x(Vector3::Transform(Vector3(lineLength, 0, 0), newMatrix));
    Vector3 y(Vector3::Transform(Vector3(0, lineLength, 0), newMatrix));
    Vector3 z(Vector3::Transform(Vector3(0, 0, lineLength), newMatrix));

    AddLine(origin, x, Color(1, 0, 0, 1));
    AddLine(origin, y, Color(0, 1, 0, 1));
    AddLine(origin, z, Color(0, 0, 1, 1));
}

void DebugRenderer::AddWireCylinder(const Vector3& position, const Vector3& direction, float height, float radius, int segments, const IntColor& color)
{
    Vector3 d;
    direction.Normalize(d);

    DebugSphere sphere(position, radius);
    float t = Math::PI * 2 / (segments + 1);

    Matrix world = Matrix::CreateFromQuaternion(Math::LookRotation(d)) * Matrix::CreateTranslation(position - d * (height / 2));
    for (int i = 0; i < segments + 1; i++)
    {
        Vector3 a = Vector3::Transform(sphere.GetLocalPoint(Math::PIDIV2, i * t), world);
        Vector3 b = Vector3::Transform(sphere.GetLocalPoint(Math::PIDIV2, (i + 1) * t), world);
        AddLine(a, b, color);
        AddLine(a + d * height, b + d * height, color);
        AddLine(a, a + d * height, color);
    }
}

void DebugRenderer::AddWireCone(const Vector3& position, const Vector3& direction, float height, float angle, int segments, const IntColor& color)
{
    Vector3 d;
    direction.Normalize(d);

    float radius = tanf(0.5f * angle * Math::DegreesToRadians) * height;
    DebugSphere sphere(position, radius);
    float t = Math::PI * 2 / (segments + 1);

    Matrix world = Matrix::CreateFromQuaternion(Math::LookRotation(d)) * Matrix::CreateTranslation(position);
    for (int i = 0; i < segments + 1; i++)
    {
        Vector3 a = Vector3::Transform(sphere.GetLocalPoint(Math::PIDIV2, i * t), world) + direction * height;
        Vector3 b = Vector3::Transform(sphere.GetLocalPoint(Math::PIDIV2, (i + 1) * t), world) + direction * height;
        AddLine(a, b, color);
        AddLine(a, position, color);
    }
}

void DebugRenderer::AddBone(const Matrix& matrix, float length, const IntColor& color)
{
    float boneSize = 2;
    Vector3 start = Vector3::Transform(Vector3::Zero, matrix);
    Vector3 a = Vector3::Transform(Vector3(-boneSize, boneSize, boneSize), matrix);
    Vector3 b = Vector3::Transform(Vector3(boneSize, boneSize, boneSize), matrix);
    Vector3 c = Vector3::Transform(Vector3(boneSize, -boneSize, boneSize), matrix);
    Vector3 d = Vector3::Transform(Vector3(-boneSize, -boneSize, boneSize), matrix);
    Vector3 tip = Vector3::Transform(Vector3(0, 0, -boneSize * length), matrix);

    AddTriangle(start, d, c, color, false);
    AddTriangle(start, a, d, color, false);
    AddTriangle(start, b, a, color, false);
    AddTriangle(start, c, b, color, false);
    AddTriangle(d, tip, c, color, false);
    AddTriangle(a, tip, d, color, false);
    AddTriangle(b, tip, a, color, false);
    AddTriangle(c, tip, b, color, false);
}

void DebugRenderer::AddLight(const Light& light)
{
    switch (light.Type)
    {
        case LightType::Directional:
        {
            AddWireCylinder(light.Position, light.Direction, 30, 5.0f,10, Color(1, 1, 0, 1));
            AddAxisSystem(Matrix::CreateWorld(light.Position, -light.Direction, Vector3::Up), 1.0f);
            break;
        }
        case LightType::Point:
        {
            AddSphere(light.Position, light.Range, 8, 8, Color(1, 1, 0, 1), false);
            break;
        }
        case LightType::Spot:
        {
            AddWireCone(light.Position, light.Direction, light.Range, light.UmbraAngleDegrees, 10, Color(1, 1, 0, 1));
            break;
        }
    }
}

