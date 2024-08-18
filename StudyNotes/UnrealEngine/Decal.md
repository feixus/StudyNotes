# DBuffer decals before BasePas

- FDeferredShadingSceneRenderer::Render(FRDGBuilder& GraphBuilder)
    create DBufferTextures:DBufferA/DBufferB/DBufferC(RDG resources)
    call FCompositionLighting::ProcessBeforeBasePass

- FCompositionLighting::ProcessBeforeBasePass(FRDGBuilder& GraphBuilder, FDBufferTextures& DBufferTextures)
    iterate views
    if bEnableDBuffer, then get FDeferredDecalPassTextures and call AddDeferredDecalPass
    FDeferredDecalPassTextures: first GraphBuilder.AllocParameters with FDecalPassUniformParameters, set data for GBufferA/SceneDepth/CustomDepth/EyeAdaptationTexture.  then using GraphBuilder.CreateUniformBuffer to convert to uniformBuffer(DecalPassUniformBuffer). then set Depth/Color/GBufferA/GBufferB/GBufferC/GBufferE/DBufferTextures.

- AddDeferredDecalPass( FRDGBuilder& GraphBuilder, const FViewInfo& View, const FDeferredDecalPassTextures& PassTextures, EDecalRenderStage DecalRenderStage)
    首先若存在Scene.Decals, GraphBuilder.AllocObject分配SortedDecals(FTransientDecalRenderDataList). 遍历所有的Scene.Decals, 筛选出在视野范围内的Decals, 依据Proxy.SortOrder/BlendDesc.bWriteNormal/BlendDesc.Packed/MaterialProxy/Proxy.Component来排序.
    其次若存在View.MeshDecalBatches, 则调用RenderMeshDecals.
    若存在排序过的Decals, 则调用RenderDecals.

- RenderDecals(uint32 DecalIndexBegin, uint32 DecalIndexEnd, EDecalRenderTargetMode RenderTargetMode)
    获取PassParameters, 经由GraphBuilder.AllocParameters分配的FDeferredDecalPassParameters, 并初始化.
    调用GraphBuilder::AddPass, 将pass加入RDG.
    当RDG执行到时, 遍历所有可视的Decals, 为每个Decal设置GraphicsPSO的参数(RHICmdList.SetGraphicsPipelineState). 其次设置vertex/pixel shader parameters( RHICmdList.SetShaderParameter). 设置stream source(RHICmdList.SetStreamSource). 最终RHICmdList.DrawIndexedPrimitive

- RenderMeshDecals( FRDGBuilder& GraphBuilder, const FViewInfo& View, const FDeferredDecalPassTextures& DecalPassTextures, EDecalRenderStage DecalRenderStage)
    - 依据DecalRenderStage,采用不同的DecalRenderTargetMode.
    - 调用 DrawDecalMeshCommands
    - 调用 AddDrawDynamicMeshPass
      - 设置前置任务GraphBuilder.AddSetupTask: 构造FMeshDecalMeshProcessor, 遍历View.MeshDecalBatches, 将FMeshBatch转换为FMeshDrawCommand.
            - FMeshDecalMeshProcessor::AddMeshBatch
            - FMeshDecalMeshProcessor::TryAddMeshBatch
            - FMeshDecalMeshProcessor::Process: 根据EDecalRenderStage选择适当的PS类型, 获取VertexShader/PixelShader; 设置SorKey.BasePass
            - FMeshPassProcessor::BuildMeshDrawCommands
      - 添加可执行的Pass:GraphBuilder.AddPass
            设置Viewport
            调用DrawDynamicMeshPassPrivate
                - 调用ApplyViewOverridesToMeshDrawCommands
                    若 View.bReverseCulling或者View.bRenderSceneTwoSided为真时,需遍历VisibleMeshDrawCommand,修改CullMode,最终重载VisiblemeshDrawCommand.
                - 调用SortAndMergeDynamicPassMeshDrawCommands
                    根据SortKey或StateBucketId排序VisibleMeshDrawCommand, 若bUseGPUScene为真, 此路径下默认bDynamicInstancing为假. 从GPrimitiveIdVertexBufferPool分配一个容量为最大图元数*VisibleMeshDrawCommands数量的PrimitiveIdVertexBuffer, 并RHICmdList.LockBuffer.  遍历meshDrawCommands, 构建PrimitiveIdBuffer.
                - 调用SubmitMeshDrawCommandsRange
                    再次确认 bDynamicInstancing为假,遍历VisibleMeshDrawCommands, 调用FMeshDrawCommand::SubmitDraw
                - 调用 FMeshDrawCommand::SubmitDraw
                - 调用 FMeshDrawCommand::SubmitDrawBegin
                    RHICmdList.SetGraphicsPipelineState: 从PipelineStateCache中查询或创建一个GraphicsPipelineState并初始化PipelineState/ShaderBinding/StencilRef.
                    RHICmdList.SetStencilRef
                    遍历MeshDrawCommand.VertexStreams, 设置RHICmdList.SetStreamSource.
                    FMeshDrawShaderBindings::SetOnCommandList: 遍历ShaderLayouts,为存在的着色器Vertex/Pixel/Geometry设置shaderBindings(uniformBuffers/sampler/srv/texture/shaderParameter)
                - 调用FMeshDrawCommand::SubmitDrawEnd
                   若MeshDrawCommand.IndexBuffer存在, 则调用RHICmdList.DrawIndexedPrimitive或者RHICmdList.DrawIndexedPrimitiveIndirect. 否则RHICmdList.DrawPrimitive或者RHICmdList.DrawPrimitiveIndirect

# decals after BasePas: pre-lighting composition lighting stage
    FCompositionLighting::ProcessAfterBasePass(FRDGBuilder& GraphBuilder, EProcessAfterBasePassMode Mode)
    EDecalRenderStage::BeforeLighting / EDecalRenderStage::Emissive / EDecalRenderStage::AmbientOcclusion


``` c++
enum class EDecalRenderStage : uint8
{
    None = 0,
    //DBuffer decal pass.
    BeforeBasePass = 1,
    //GBuffer decal pass
    BeforeLighting = 2,

    // mobile decal pass with limited functionality.
    Mobile = 3,
    // mobile decal pass for mobile deferred platforms.
    MobileBeforeLighting = 4,

    //Emissive decal pass.
    //DBuffer decals with an emissive component will use this pass.
    Emissive = 5,
    //Ambient occlusion decal pass.
    // a decal can write regular attributes in another pass and then AO in this pass.
    AmbientOcclusion = 6,

    Num,
}
```

``` c++
/** Enumeration of the render target layouts for decal rendering. */
enum class EDecalRenderTargetMode : uint8
{
	None = 0,

	DBuffer = 1,
	SceneColorAndGBuffer = 2,
	// GBuffer with no normal is necessary for decals sampling the normal from the GBuffer.
	SceneColorAndGBufferNoNormal = 3,
	SceneColor = 4,
	AmbientOcclusion = 5,
};
```