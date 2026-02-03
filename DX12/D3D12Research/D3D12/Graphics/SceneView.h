#pragma once
#include "Core/DescriptorHandle.h"
#include "Core/Bitfield.h"
#include "ShaderCommon.h"

class GraphicsTexture;
class GraphicsBuffer;
class Camera;
class CommandContext;
struct SubMesh;

struct Batch
{
	enum class Blending
	{
		Opaque = 1,
		AlphaMask = 2,
		AlphaBlend = 4,
	};

	int Index{0};
	Blending BlendMode{ Blending::Opaque };
	const SubMesh* pMesh{nullptr};
	Matrix WorldMatrix;
	BoundingBox LocalBounds;
	BoundingBox Bounds;
	float Radius{0};
	int Material{0};
};
DECLARE_BITMASK_TYPE(Batch::Blending);

using VisibilityMask = BitField<2048>;

struct SceneView
{
	GraphicsTexture* pDepthBuffer{ nullptr };
	GraphicsTexture* pResolvedDepth{ nullptr };
	GraphicsTexture* pRenderTarget{ nullptr };
	GraphicsTexture* pResolvedTarget{ nullptr };
	GraphicsTexture* pPreviousColor{ nullptr };
	GraphicsTexture* pNormals{ nullptr };
	GraphicsTexture* pResolvedNormals{ nullptr };
	GraphicsTexture* pAO{ nullptr };
	std::vector<Batch> Batches;
	DescriptorHandle GlobalSRVHeapHandle{};
	GraphicsBuffer* pLightBuffer{ nullptr };
	GraphicsBuffer* pMaterialBuffer{ nullptr };
	GraphicsBuffer* pMeshBuffer{ nullptr };
	GraphicsBuffer* pMeshInstanceBuffer{ nullptr };
	Camera* pCamera{ nullptr };
	ShaderInterop::ShadowData* pShadowData{ nullptr };
	int SceneTLAS{ 0 };
	int FrameIndex{ 0 };
	VisibilityMask VisibilityMask;
};

void DrawScene(CommandContext& context, const SceneView& scene, const VisibilityMask& visibility, Batch::Blending blendModes);
void DrawScene(CommandContext& context, const SceneView& scene, Batch::Blending blendModes);
