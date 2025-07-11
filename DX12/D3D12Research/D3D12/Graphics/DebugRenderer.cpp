#include "stdafx.h"
#include "DebugRenderer.h"
#include "GraphicsBuffer.h"
#include "Graphics.h"
#include "Shader.h"
#include "Scene/Camera.h"
#include "Mesh.h"
#include "Light.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "RenderGraph/RenderGraph.h"
#include "CommandContext.h"

DebugRenderer::DebugRenderer(Graphics* pGraphics)
    : m_pGraphics(pGraphics)
{
    D3D12_INPUT_ELEMENT_DESC inputElements[] = {
        D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        D3D12_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // shaders
    Shader vertexShader("Resources/Shaders/DebugRenderer.hlsl", Shader::Type::VertexShader, "VSMain");
    Shader pixelShader("Resources/Shaders/DebugRenderer.hlsl", Shader::Type::PixelShader, "PSMain");

    // root signature
    m_pRS = std::make_unique<RootSignature>();
    m_pRS->FinalizeFromShader("Diffuse", vertexShader, pGraphics->GetDevice());

    // opaque
    m_pTrianglesPSO = std::make_unique<GraphicsPipelineState>();
    m_pTrianglesPSO->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
    m_pTrianglesPSO->SetRootSignature(m_pRS->GetRootSignature());
    m_pTrianglesPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
    m_pTrianglesPSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
    m_pTrianglesPSO->SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    m_pTrianglesPSO->SetDepthWrite(true);
    m_pTrianglesPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
    m_pTrianglesPSO->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
    m_pTrianglesPSO->Finalize("Debug Triangles", pGraphics->GetDevice());
}

DebugRenderer::~DebugRenderer()
{
}

void DebugRenderer::Render(RGGraph& graph)
{
    int totalPrimitives = m_LinePrimitives + m_TrianglePrimitives;
    if (totalPrimitives == 0 || m_pCamera == nullptr)
    {
        return;
    }

    graph.AddPass("Debug Rendering", [&](RGPassBuilder& builder)
    {
        builder.NeverCull();
        return [=](CommandContext& context, const RGPassResource& resources)
        {
            context.InsertResourceBarrier(m_pGraphics->GetDepthStencil(), D3D12_RESOURCE_STATE_DEPTH_WRITE);

            context.BeginRenderPass(RenderPassInfo(m_pGraphics->GetCurrentRenderTarget(), RenderPassAccess::Load_Store, m_pGraphics->GetDepthStencil(), RenderPassAccess::Load_Store));

            context.SetViewport(FloatRect(0.0f, 0.0f, (float)m_pGraphics->GetWindowWidth(), (float)m_pGraphics->GetWindowHeight()));
            context.SetGraphicsRootSignature(m_pRS.get());

            const Matrix& projectionMatrix = m_pCamera->GetViewProjection();
            context.SetDynamicConstantBufferView(0, &projectionMatrix, sizeof(Matrix));

            if (m_LinePrimitives != 0)
            {
                context.SetDynamicVertexBuffer(0, m_LinePrimitives, sizeof(Vector3) + sizeof(Color), m_Lines.data());
                context.SetGraphicsPipelineState(m_pLinesPSO.get());
                context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
                context.Draw(0, m_LinePrimitives);
            }
            if (m_TrianglePrimitives != 0)
            {
                context.SetDynamicVertexBuffer(0, m_TrianglePrimitives, sizeof(Vector3) + sizeof(Color), m_Triangles.data());
                context.SetGraphicsPipelineState(m_pTrianglesPSO.get());
                context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                context.Draw(0, m_TrianglePrimitives);
            }

            context.EndRenderPass();
        };
    });
}

void DebugRenderer::EndFrame()
{
    m_LinePrimitives = 0;
    m_TrianglePrimitives = 0;
    m_Lines.clear();
    m_Triangles.clear();
}

void DebugRenderer::AddLine(const Vector3& start, const Vector3& end, const Color& color)
{
    AddLine(start, end, color, color);
}

void DebugRenderer::AddLine(const Vector3& start, const Vector3& end, const Color& colorStart, const Color& colorEnd)
{
    m_Lines.push_back(DebugLine(start, end, colorStart, colorEnd));
    m_LinePrimitives += 2;
}

void DebugRenderer::AddRay(const Vector3& start, const Vector3& direction, const Color& color)
{
    AddLine(start, start + direction, color);
}

void DebugRenderer::AddTriangle(const Vector3& a, const Vector3& b, const Vector3& c, const Color& color, const bool solid)
{
    AddTriangle(a, b, c, color, color, color, solid);
}

void DebugRenderer::AddTriangle(const Vector3& a, const Vector3& b, const Vector3& c, const Color& colorA, const Color& colorB, const Color& colorC, const bool solid)
{
    if (solid)
    {
        m_Triangles.push_back(DebugTriangle(a, b, c, colorA, colorB, colorC));
        m_TrianglePrimitives += 3;
    }
    else
    {
        AddLine(a, b, colorA);
        AddLine(b, c, colorB);
        AddLine(c, a, colorC);
    }
}

void DebugRenderer::AddPolygon(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, const Color& color)
{
    AddTriangle(a, b, c, color);
    AddTriangle(a, c, d, color);
}

void DebugRenderer::AddBoundingBox(const BoundingBox& boundingBox, const Color& color, const bool solid)
{
    Vector3 minV(boundingBox.Center.x - boundingBox.Extents.x, boundingBox.Center.y - boundingBox.Extents.y, boundingBox.Center.z - boundingBox.Extents.z);
    Vector3 maxV(boundingBox.Center.x + boundingBox.Extents.x, boundingBox.Center.y + boundingBox.Extents.y, boundingBox.Center.z + boundingBox.Extents.z);

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

void DebugRenderer::AddBoundingBox(const BoundingBox& boundingBox, const Matrix& transform, const Color& color, const bool solid)
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

void DebugRenderer::AddSphere(const Vector3& position, const float radius, const int slices, const int stacks, const Color& color, const bool solid)
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

                AddPolygon(p2, p1, p3, p4, Color(0, 0, 1, 1));
            }
        }
    }
}

void DebugRenderer::AddFrustrum(const BoundingFrustum& frustrum, const Color& color)
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

void DebugRenderer::AddAxisSystem(const Matrix& transform, const float lineLength)
{
    Matrix newMatrix = Matrix::CreateScale(Math::ScaleFromMatrix(transform));
    newMatrix.Invert(newMatrix);
    newMatrix *= Matrix::CreateScale(Vector3::Distance(m_pCamera->GetViewInverse().Translation(), transform.Translation()) / 5.0f);
    newMatrix *= transform;

    Vector3 origin(Vector3::Transform(Vector3::Zero, transform));
    Vector3 x(Vector3::Transform(Vector3(lineLength, 0, 0), newMatrix));
    Vector3 y(Vector3::Transform(Vector3(0, lineLength, 0), newMatrix));
    Vector3 z(Vector3::Transform(Vector3(0, 0, lineLength), newMatrix));

    AddLine(origin, x, Color(1, 0, 0, 1));
    AddLine(origin, y, Color(0, 1, 0, 1));
    AddLine(origin, z, Color(0, 0, 1, 1));
}

void DebugRenderer::AddWireCylinder(const Vector3& position, const Vector3& direction, const float height, const float radius, const int segments, const Color& color)
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

void DebugRenderer::AddWireCone(const Vector3& position, const Vector3& direction, const float height, const float angle, const int segments, const Color& color)
{
    Vector3 d;
    direction.Normalize(d);

    float radius = tan(Math::ToRadians * angle) * height;
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

void DebugRenderer::AddBone(const Matrix& matrix, const float length, const Color& color)
{
    float boneSize = 2;
    Vector3 start = Vector3::Transform(Vector3::Zero, matrix);
    Vector3 a = Vector3::Transform(Vector3(-boneSize, boneSize, boneSize), matrix);
    Vector3 b = Vector3::Transform(Vector3(boneSize, boneSize, boneSize), matrix);
    Vector3 c = Vector3::Transform(Vector3(boneSize, -boneSize, boneSize), matrix);
    Vector3 d = Vector3::Transform(Vector3(-boneSize, -boneSize, boneSize), matrix);
    Vector3 tip = Vector3::Transform(Vector3(0, 0, -boneSize * length), matrix);

    AddTriangle(start, d, c, color, color, color, false);
    AddTriangle(start, a, d, color, color, color, false);
    AddTriangle(start, b, a, color, color, color, false);
    AddTriangle(start, c, b, color, color, color, false);
    AddTriangle(d, tip, c, color, color, color, false);
    AddTriangle(a, tip, d, color, color, color, false);
    AddTriangle(b, tip, a, color, color, color, false);
    AddTriangle(c, tip, b, color, color, color, false);
}

void DebugRenderer::AddLight(const Light& light)
{
    switch (light.LightType)
    {
        case Light::Type::Directional:
        {
            AddWireCylinder(light.Position, light.Direction, 200.0f, 50.0f,10, Color(1, 1, 0, 1));
            break;
        }
        case Light::Type::Point:
        {
            AddSphere(light.Position, light.Range, 8, 8, Color(1, 1, 0, 1), false);
            break;
        }
        case Light::Type::Spot:
        {
            AddWireCone(light.Position, light.Direction, light.Range, Math::ToDegrees * acos(light.CosHalfAngle), 10, Color(1, 1, 0, 1));
            break;
        }
    }
}

