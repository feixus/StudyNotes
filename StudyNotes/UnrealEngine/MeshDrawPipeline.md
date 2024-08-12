### Primary Classes
- UWorld
- ULevel
- USceneComponent
- UPrimitiveComponent
- ULightComponent

- FScene: UWorld的渲染器版本. 对象(primitives和lights)只有在添加到FScene后才会存在于renderer, 即registering a component. 
          渲染线程拥有FScene中的所有状态, 游戏线程不可直接修改
- FPrimitiveSceneProxy: UPrimitiveComponent的渲染器版本. 为渲染线程镜像UPrimitiveComponent的状态. 存在于engine module
- FPrimitiveSceneInfo: 对应于UPrimitiveComponent和FPrimitiveSceneProxy的内部渲染器状态, 存在于renderer module

- FSceneView: a projection from scene space into a 2d screen region.
              FScene中单个视图地引擎表示. 一个场景可以被不同地视图渲染(FSceneRender::Render), 多个编辑器视口或分屏地多个视图. 每帧都会构造新的View
- FViewInfo: a FSceneView with additional state used by the scene renderer.
             view地内部渲染器表达式, 存在于renderer module
- FSceneViewState: ViewState存储了一个视图地私有渲染器信息,以便跨帧使用. 游戏中每个ULocalPlayer有一个view state
- FSceneRenderer: 每帧创建,封装帧间的临时数据

- FMeshBatchElement: a batch mesh element definition.
- FMeshBatch: a batch of mesh elements, all with the same material and vertex buffer.
- FMeshDrawCommand: 
  完整描述了一个mesh pass draw call, FMeshDrawCommand在Primitive AddToScene时缓存,以便vertex factories使用(没有每帧或每视图的shader binding changes).  
  Dynamic Instancing在FMeshDrawCommand级别运行,以实现鲁棒性.
  增加per-command shader bindings将会减少Dynamic Instancing的效率, 但渲染总是正确的.
  command引用的任何资源必须在command的整个生命周期内保持活跃.FMeshDrawCommand不负责资源的生命周期管理.
  cached FMeshDrawCommand的uniform buffers引用, RHIUpdateUniformBuffer使得在没有changing bindings时可以访问着色器中的每帧数据.
  resource bindings/PSO/Draw command parameters/Non-pipeline state
- FMeshPassProcessor: mesh processors的基类. 将从scene proxy实现接收到的FMeshBatch draw descriptions转换为用于RHI command list的FMeshDrawCommands.
- 在调用图形API之前, 引擎需要做很多操作和优化, 如 Occlusion Culling/Static|Dynamic Batching/Dynamic Instancing/Cache State|Command/Generate Intermidiate Commands for translate to graphic api...
    Dynamic Batching: 引擎将共享材质球的不同对象组合到单个draw call.
    Dynamic Instancing: 引擎将具有相同网格和材质的相同对象的多个实例渲染到单个draw call.
    Static Batching: 将多个静态对象(不可移动/变化频率小)组合成更大的单个网格, 以便渲染时仅单个draw call.

<br/>

| GameThread |         RenderingThread |
| --- | --- |
| UWorld |              FScene |
| UPrimitiveComponent |     FPrimitiveSceneProxy/FPrimitiveSceneInfo |
|                      |  FSceneView/FViewInfo |
| ULocalPlayer          |  FSceneViewState |
| ULightComponent       |  FLightSceneProxy/FLightSceneInfo |

<br/>

|Engine Module | Renderer Module|
| --- | --- |
| UWold | FScene |
| UPrimitiveComponent/FPrimitiveSceneProxy | FPrimitiveSceneInfo |
| FSceneView | FViewInfo |
| ULocalPlayer | FSceneViewState |
| ULightComponent/FLightSceneProxy | FLightSceneInfo |

<br/>

### Mesh Draw Pipeline

##### FMeshBatchElement
  记录单个网格元素的数据, 如primitive uniform buffer/index buffer/user data/primitiveId
  
- FRHIUniformBuffer* PrimitiveUniformBuffer
  对于从场景数据中手动提取图元数据的vertex factory, 必须为空. 此时使用FPrimitiveSceneProxy::UniformBuffer.

- const TUniformBuffer<FPrimitiveUniformShaderParameters>* PrimitiveUniformBufferResource
  Primitive uniform buffer用于渲染, 当PrimitiveUniformBuffer为空时使用. 允许为尚未初始化的uniform buffer设置一个FMeshBatchElement.

- const FIndexBuffer* IndexBuffer

- FMeshBatchElementDynamicIndexBuffer DynamicIndexBuffer
  针对特定view, 用于动态排序三角形的对象.(如 per-object order-independent-transparency).

- uint32* InstanceRuns/FSplineMeshSceneProxy* SplineMeshSceneProxy
  当没有SplineProxy, Instance runs, 由NumInstances指定数量. Run structure是[StartInstanceIndex, EndInstanceIndex]
  当为SplineProxy时, 指向proxy的指针.

- const void* UserData
- void* VertexFactoryUserData
  意图取决于vertex factory. 如FGPUSkinPassthroughVertexFactory: element index in FGPUSkinCache::CachedElements

- FRHIBuffer* IndirectArgsBuffer
- uint32 IndirectArgsOffset

- EPrimitiveIdMode PrimitiveIdMode
  由渲染器分配.
  PrimID_FromPrimitiveSceneInfo: primitiveId将从FMeshBatch对应的FPrimitiveSceneInfo中获取. primitive data将从GPUScene persistent primitiveBuffer中提取.
  PrimID_DynamicPrimitiveShaderData: renderer将会上传primitive data,从FMeshBatchElement的PrimitiveUniformBufferResource上传到GPUScene PrimitiveBuffer的结尾处, 然后分配偏移至DynamicPrimitiveIndex. 用于绘制的primitiveId将计算为Scene->NumPrimitives + FMeshBatchElement的DynamicPrimitiveIndex.
  PrimID_ForceZero: 不支持Instancing. 在此配置中必须设置View.PrimitiveSceneDataOverrideSRV,以便控制在PrimitiveId == 0时提取的什么shader.

- FirstIndex
- NumPrimitives
  当为0时,将会使用IndirectArgsBuffer

- NumInstances
  若InstanceRuns有效, 这个实际是InstanceRuns的runs数量
- BaseVertexIndex
- MinVertexIndex
- MaxVertexIndex
- UserIndex
- MinScreenSize
- MaxScreenSize

- InstancedLODIndex
- InstancedLODRange
- bUserDataIsColorVertexBuffer
- bIsSplineProxy
- bIsInstanceRuns
- bForceInstanceCulling
- bPreserveInstanceOrder

- const FMeshBatchDynamicPrimitiveData* DynamicPrimitiveData
  dynamic primitives的源实例场景数据(source instance scene data)和有效负载数据(payload data). 必须为拥有超过单个instance的dynamic primitives提供.指向对像的生命周期预期会匹配或超过mesh batch自身的生命周期.
- DynamicPrimitiveIndex
- DynamicPrimitiveInstanceSceneDataOffset

- GetNumPrimitives() 

<br/>

##### FMeshBatch
  网格元素的批次.所有网格元素拥有相同的mesh和vertex buffer.

- TArray<FMeshBatchElement,TInlineAllocator<1> > Elements
  FMeshBatchElements批次. TInlineAllocator<1>表明Elements数组至少有1个元素.
- const FVertexFactory* VertexFactory
  渲染所需的vertex factory.
- const FMaterialRenderProxy* MaterialRenderProxy
  渲染所需的material proxy

- const FLightCacheInterface* LCI
  为特定的mesh缓存光照的接口

- FHitProxyId BatchHitProxyId

- TessellationDisablingShadowMapMeshSize
- MeshIdInPrimitive
  相同图元的绘制进行稳定排序.

- LODIndex
  LOD的平滑过渡.
- SegmentIndex

- ReverseCulling
- bDisableBackfaceCulling

- CastShadow
  是否可用于shadow render passes.
- bUseForMaterial
  是否可用于需求material输出的render passes
- bUseForDepthPass
- bUseAsOccluder
- bWireframe
- Type : PT_NumBits
  PT_TriangleList/PT_TriangleStrip/PT_LineList/PT_QuadList/PT_PointList/PT_RectList...
- DepthPriorityGroup : SDPG_NumBits

- bCanApplyViewModeOverrides
  view mode重载是否应用到此mesh,如unlit,wireframe.
- bUseWireframeSelectionColoring
  是否对待batch为特殊viewmodes的选中,如wireframe. 如FStaticMeshSceneProxy
- bUseSelectionOutline
  batch是否接受选中轮廓. proxies which support selection on a per-mesh batch basis.
- bSelectable
  是否可通过编辑器选择来选中mesh batch, 即 hit proxies.
- bDitheredLODTransition
  
- bRenderToVirtualTexture
- RuntimeVirtualTextureMaterialType : RuntimeVirtualTexture::MaterialType_NumBits
  
- bOverlayMaterial
  rendered with overlay material.
- CastRayTracedShadow

- IsTranslucent/IsDecal/IsDualBlend/UseForHairStrands/IsMasked/QuantizeLODIndex/GetNumPrimitives/HasAnyDrawCalls
- PreparePrimitiveUniformBuffer
  若禁止GPU scene,备份使用primitive uniform buffer. mobile上的vertex shader在GPUScene开启时可能仍使用PrimitiveUB
  
##### FPrimitiveSceneProxy -> FMeshBatch
    场景渲染器FSceneRenderer在渲染之初, 执行可见性测试和剔除, 以剔除被遮挡或被隐藏的物体, 在此阶段的末期会调用GatherDynamicMeshElements, 从当前场景所有的FPrimitiveSceneProxy中筛选并构建FMeshBatch, 放置在Collector.


FSceneRenderer::Render (DeferredShadingRenderer/MobileShadingRenderer)
FDeferredShadingSceneRenderer::InitViews
FSceneRenderer::ComputeViewVisibility
FSceneRenderer::GatherDynamicMeshElements
GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector)
  收集图元的dynamic mesh elements. 仅GetViewRelevance声明了dynamic relevance的才会调用. 针对每组可能被渲染的views的渲染线程调用.
  游戏线程的状态如UObjects必须将其属性镜像到 primitiveSceneProxy 以避免race conditions. 渲染线程必须禁止解引用UObjects.
  收集到的mesh elements将会多次使用, 任何内存引用的生命周期必须和Collector一样长(即不应该引用stack memory).
  此函数不应修改proxy,仅简单收集渲染事物的描述. proxy的更新需要从游戏线程或外部事件推送.

FSkeletalMeshSceneProxy::GetDynamicMeshElements
FSkeletalMeshSceneProxy::GetMeshElementsConditionallySelectable
FSkeletalMeshSceneProxy::GetDynamicElementsSection
  根据当前LODIndex,获取LODData, 针对当前LOD的每个LODSection,通过Collector构建FMeshBatch, 设置MeshBatchElement/vertex factory/material/OverlayMaterial等一系列参数.

FMeshElementCollector::AddMesh(int32 ViewIndex, FMeshBatch& MeshBatch)
ComputeDynamicMeshRelevance: 计算当前mesh dynamic element的MeshBatch会被哪些MeshPass引用, 加入到每个View的PrimitiveViewRelevanceMap


##### FMeshBatch -> FMeshDrawCommand
      


FSceneRenderer::SetupMeshPass
  遍历所有的MeshPass类型
   根据着色路径(Mobile/Deferred)和Pass类型来创建对应的FMeshPassProcessor
   获取指定Pass的FParallelMeshDrawCommandPass对象, 根据(r.MeshDrawCommands.LogDynamicInstancingStats)可以打印下一帧MeshDrawCommand实例的统计信息
   并行处理MeshDrawCommand.

FParallelMeshDrawCommandPass::DispatchPassSetup
  调度可见mesh draw command处理任务,为绘制准备此pass. 包括生成dynamic mesh draw commands, draw sorting, draw merging.

  设置FMeshDrawCommandPassSetupTaskContext的数据.
  包括 基础属性, translucency sort key, 交换内存命令列表(MeshDrawCommands/DynamicMeshCommandBuildRequests/MobileBasePassCSMMeshDrawCommands)
  基于最大绘制数量在渲染线程预分配资源(PrimitiveIdBufferData/MeshDrawCommands/TempVisibleMeshDrawCommands)
  若可以并行执行, 若允许按需shaderCreation(IsOnDemandShaderCreationEnabled), 直接添加任务(FMeshDrawCommandPassSetupTask)至TaskGraphic系统. 否则将任务(FMeshDrawCommandPassSetupTask)作为前置, 添加到另一个任务(FMeshDrawCommandInitResourcesTask)中.
  若不可以并行执行, 则直接执行FMeshDrawCommandPassSetupTask任务, 若不允许按需shaderCreation,则再执行FMeshDrawCommandInitResourcesTask任务.

  

FMeshDrawCommandPassSetupTask
  并行设置mesh draw commands的任务. 包含生成dynamic mesh draw command,sorting,merging...

FMeshPassProcessor::AddMeshBatch
  在创建一系列的任务后, 当TaskGraphicSystem并行执行到每个任务时,依据不同的mesh pass processor来转换FMeshBatch为FMeshDrawCommand.

如 FBasePassMeshProcessor::AddMeshBatch
若标记了bUseForMaterial, 查询materialRenderProxy中可以使用的material.
FBasePassMeshProcessor::TryAddMeshBatch: 仅绘制opaque materials. 
    渲染volumetric translucent self-shadowing/point indirect lighting及self shadowing/directional light self shadowing translucency.
FBasePassMeshProcessor::Process
    获取base pass shaders(vertexShader/pixelShader)
    设置render state: blendState/depthStencil/viewUniformBuffer/InstancedViewUniformBuffer/PassUniformer/NaniteUniformBuffer/StencilRef
    设置排序 basePass(VertexShaderHash|PixelShaderHash|Masked), Translucent(MeshIdInPrimitive|Distance|Priority), Generic(VertexShaderHash|PixelShaderHash)

FMeshPassProcessor::BuildMeshDrawCommands
    传入参数: MeshBatch,BatchElementMask,PrimitiveSceneProxy,MaterialRenderProxy,MaterialResource,DrawRenderState,PassShaders,MeshFillMode,MeshCullMode,SortKey,MeshPassFeatures,ShaderElementData.
    
    构造FMeshDrawCommand
      设置StencilRef,PrimitiveType
      为shader bindings分配内存(ShaderLayouts)
      构造没有render target state的FGraphicsMinimalPipelineStateInitializer
        设置PrimitiveType,ImmutableSamplerState.    
        根据vertexInputStreamType(position或者positionAndNormal)获取对应VertexFactory的vertexDeclaration, 以及Shaders(vertex|pixel|geometry shaderResource/shaderIndex)来设置BoundShaderState.     
        根据MeshFillMode和MeshCullMode来设置RasterizerState.    
        设置BlendState/DepthStencilState
        设置DrawShadingRate(EVRSShadingRate). (Variable Rate Shading 允许屏幕的不同部分有不同的着色速率1x1~4x4)
        若PSO precaching开启,计算hash.
      设置vertexStream及PrimitiveIdStreamIndex. 根据VertexStreamType(Default/PositionOnly/PositionAndNormalOnly)来获取不同数据来源.
      设置VertexShader/PixelShader/GeometryShader的shader bindings数据, 从MeshMaterialShader/MaterialShader/LightMapPolicyType从获取各种各样的uniform buffers.
      设置调试数据
      遍历FMeshBatch的所有FMeshBatchElements
        收集每个element的vertexShader/PixelShader/GeometryShader的shader bindings(primitiveUniformBuffer或者PrimitiveUniformBufferResource).
        针对使用GPUScene的vertexFactory, 默认是不允许绑定PrimitiveUniformBuffer, 因Vertex Factory 计算一个PrimitiveId per-instance. 这会打断auto-instancing. shader应使用GetPrimitiveData(PrimitiveId).Member替代Primitive.Member. 但在mobile上允许Primitive uniform buffer for vertex shader.
        获取MeshDrawCommandPrimitiveIdInfo, 根据element的PrimitiveIdMode(PrimID_FromPrimitiveSceneInfo/PrimID_DynamicPrimitiveShaderData). PrimID_FromPrimitiveSceneInfo模式下,是static primitive,从PrimitiveSceneInfo获取drawPrimitiveId和InstanceSceneDataOffset.  PrimID_DynamicPrimitiveShaderData模式下是dynamic primitive, 从element自身获取.    
        调用FMeshPassDrawListContext::FinalizeCommand

如FCachedPassMeshDrawListContextImmediate::FinalizeCommand   
    设置MeshDrawCommand的draw command parameters, PSO
    若bUseGPUScene为真,将FMeshDrawCommand加入到Scene.CachedMeshDrawCommandStateBuckets.
    否则加入到Scene.CachedDrawLists, 一个pass中每个FStaticMesh仅支持一个FMeshDrawCommand. 在lowest free index处分配, 'r.DoLazyStaticMeshUpdate' 可以更有效率的收缩TSparseArray.

FCachedPassMeshDrawListContextDeferred::FinalizeCommand

###### FMeshDrawCommand
    完整的描述了一个mesh pass draw call

//resource bindings
ShaderBindings: 封装单个mesh draw command的shader bindings
VertexStreams: 内联分配vertex input bindings的数量. FLocalVertexFactory bindings符合inline storage.
IndexBuffer

//PSO
CachedPipelineId: 为快速比较,唯一表达FGraphicsMinimalPipelineStateInitializer

//draw command parameters
FirstIndex
NumPrimitives
NumInstances
VertexParams/IndirectArgs
PrimitiveIdStreamIndex

//Non-pipeline state
StencilRef

PrimitiveType: access for dynamic instancing on GPU

MatchesForDynamicInstancing: 动态实例的匹配规则. 
  CachedPipelineId/StencilRef/ShaderBindings/VertexStreams/PrimitiveIdStreamIndex/IndexBuffer/FirstIndex/NumPrimitives/NumInstances
  ShaderBindings比较ShaderFrequencyBits/ShaderLayouts(looseData|sampler|srv|uniformbuffer)
  有图元数量时比较VertexParams, 否则比较IndirectArgs

GetDynamicInstancingHash
InitializeShaderBindings
SetStencilRef
SetDrawParametersAndFinalize

SubmitDrawBegin
SubmitDrawEnd
SubmitDrawIndirectBegin
SubmitDrawIndirectEnd
SubmitDraw

##### FMeshDrawCommandPassSetupTask
  判别是否为mobile base pass. 
  mobile base pass 其最终列表是基于CSM可视性从两个mesh passes中创建出来的.
  
  若为mobileShadingBasePass:
    MergeMobileBasePassMeshDrawCommands: 先合并MeshDrawCommands, 为了选择恰当的shader,基于CSM visibility合并附带BasePassCSM的mobile basePass. 即以MeshCommandsCSM替代MeshCommands.
    GenerateMobileBasePassDynamicMeshDrawCommands: 然后依然基于CSM visibility,使用normal base pass processor或CSM base pass processor来生成mesh draw commands.
    遍历所有的FMeshBatch, 采用MobilePassCSMPassMeshProcessor或者PassMeshProcessor来生成mesh draw commands. 同理处理DynamicMeshCommandBuildRequests.
  若不是mobileShadingBasePass:  为指定的mesh pass type将每个FMeshBatch转换为一组FMeshDrawCommands.

  若生成了MeshDrawCommands, 还有一些后续处理.
    ApplyViewOverridesToMeshDrawCommands: 为已存在的mesh draw commands应用view overrides.(eg. reverse culling mode for rendering planar reflections)
    mobile base pass的mesh commands排序:
      r.Mobile.MeshSortingMethod: 0-按状态排序, 大致front to back(默认) 1-严格front to back排序
    translucent mesh排序: 
        SortByDistance: 基于相机中心点到边界球体的中心点距离(3d)
        SortByProjectedZ: 基于post-projection Z
        SortAlongAxis: 基于投射到固定轴(2d)
        若bInverseSorting为真(OIT), 使用front-to-back替代back-to-front排序.
    若bUseGPUScene为真, 执行FInstanceCullingContext::SetupDrawCommands. 
        为所有的网格分配间接参数slots,以使用instancing, 增加填充间接调用和index&id buffers的命令,隐藏所有共享相同state bucket ID的命令.

##### FMeshDrawCommandInitResourcesTask

##### FMeshDrawCommandPassSetupTaskContext: parallel mesh draw command pass setup task context
View
Scene
ShadingPath
ShaderPlatform
PassType
bUseGPUScene
bDynamicInstancing
bReverseCulling
bRenderSceneTwoSided
BasePassDepthStencilAccess
DefaultBasePassDepthStencilAccess

MeshPassProcessor
MobileBasePassCSMMeshPassProcessor
DynamicMeshElements
DynamicMeshElementsPassRelevance

InstanceFactor
NumDynamicMeshElements
NumDynamicMeshCommandBuildRequestElements

//FVisibleMeshDrawCommand数组,仅用于visibility和sorting
MeshDrawCommands: FMeshCommandOneFrameArray
MobileBasePassCSMMeshDrawCommands

//FStaticMeshBatch 在scene segment构造时通过图元定义的从不改变的mesh
DynamicMeshCommandBuildRequests: TArray<const FStaticMeshBatch*, SceneRenderingAllocator>
MobileBasePassCSMDynamicMeshCommandBuildRequests

//每帧存储mesh draw command的构建, 采用TChunkedArray.
MeshDrawCommandStorage: FDynamicMeshDrawCommandStorage

//一组FGraphicsMinimalPipelineStateInitializer, 是没有render target state的pipeline state, 包含如blendState,rasterizerState,depthStencilState,primitiveType
MinimalPipelineStatePassSet: FGraphicsMinimalPipelineStateSet

NeedsShaderInitialisation

PrimitiveIdBufferData
PrimitiveIdBufferDataSize
TempVisibleMeshDrawCommands

//update translucent mesh sort keys
TranslucencyPass
TranslucentSortPolicy
TranslucentSortAxis
ViewOrigin
ViewMatrix
PrimitiveBounds

VisibleMeshDrawCommandsNum
NewPassVisibleMeshDrawCommandsNum
MaxInstances

InstanceCullingContext
InstanceCullingResult


##### FVisibleMeshDrawCommand
    存储确定可视的mesh draw command的信息, 以进行进一步的visibility processing. 
    此数据仅为initViews操作(visibility, sorting)存储数据, FMeshDrawCommand存储draw submission的数据.


##### FParallelMeshDrawCommandPass
  并行mesh draw command处理和渲染. 封装两个并行任务 mesh command setup task和drawing task.

::IsOnDemandShaderCreationEnabled
    GL rhi 不支持多线程shaderCreation, 但引擎可以配置为除了RT外不能运行mesh drawing tasks.
    FRHICommandListExecutor::UseParallelAlgorithms若为真, 则允许on demand shader creation
  r.MeshDrawCommands.AllowOnDemandShaderCreation: 0-总是在渲染线程创建RHI shaders, 在执行其他MDC任务之前. 1-若RHI支持多线程着色器创建,则在提交绘制时,按需在task threads创建.

##### FMeshPassProcessor
    mesh processor的基类, 从scene proxy实现接收的FMeshBatch绘制描述变换到FMeshDrawCommand, 以便为RHI command list准备.

EMeshPass::Type MeshPassType

const FScene* RESTRICT Scene
ERHIFeatureLevel::Type FeatureLevel

FMeshPassDrawListContext* DrawListContext

AddMeshBatch
CollectPSOInitializers

ComputeMeshFillMode
ComputeMeshCullMode

BuildMeshDrawCommands
AddGraphicsPipelineStateInitializer


##### FBasePassMeshProcessor: FMeshPassProcessor

AddMeshBatch
CollectPSOInitializers

ShouldDraw
TryAddMeshBatch

Process


##### FMeshElementCollector
  封装从各个FPrimitiveSceneProxy classes中收集到的meshes. 在收集完成后可以指定需要等待的任务列表,以实现多线程并行处理的同步


FPrimitiveDrawInterface* GetPDI(int32 ViewIndex)
  访问PDI以绘制lines/sprites...
FMeshBatch& AllocateMesh()
  分配可以被collector安全引用的FMeshBatch(生命周期足够长). 返回的引用不会应进一步的AllocateMesh的调用而失效.
GetDynamicIndexBuffer/GetDynamicVertexBuffer/GetDynamicReadBuffer: dynamic bufer pools
GetMeshBatchCount(uint32 ViewIndex): 给定view收集的MeshBatches的数量.
GetMeshElementCount(uint32 ViewIndex)
AddMesh(int32 ViewIndex, FMeshBatch& MeshBatch)
RegisterOneFrameMaterialProxy(FMaterialRenderProxy* Proxy)
AllocateOneFrameResource: 分配临时资源, FMeshBatch可以安全引用, 以便添加到collector.
ShouldUseTasks/AddTask/ProcessTasks
GetFeatureLevel
DeleteTemporaryProxies
SetPrimitive(const FPrimitiveSceneProxy* InPrimitiveSceneProxy, FHitProxyId DefaultHitProxyId)
ClearViewMeshArrays
AddViewMeshArrays

TChunkedArray<FMeshBatch> MeshBatchStorage: 使用TChunkedArray,新增元素时从不会重新分配.
TArray<TArray<FMeshBatchAndRelevance, SceneRenderingAllocator>*, TInlineAllocator<2, SceneRenderingAllocator> > MeshBatches: 用来渲染的meshes.
TArray<int32, TInlineAllocator<2, SceneRenderingAllocator> > NumMeshBatchElementsPerView
TArray<FSimpleElementCollector*, TInlineAllocator<2, SceneRenderingAllocator> > SimpleElementCollectors: point/line/triangle/sprite等简单元素的收集器.
TArray<FSceneView*, TInlineAllocator<2, SceneRenderingAllocator>> Views
TArray<uint16, TInlineAllocator<2, SceneRenderingAllocator>> MeshIdInPrimitivePerView
TArray<FMaterialRenderProxy*, SceneRenderingAllocator> TemporaryProxies: 添加materi render proxy, 在析构FMeshElementCollector时自动销毁
FSceneRenderingBulkObjectAllocator& OneFrameResources
const FPrimitiveSceneProxy* PrimitiveSceneProxy
FGlobalDynamicIndexBuffer* DynamicIndexBuffer
FGlobalDynamicVertexBuffer* DynamicVertexBuffer
FGlobalDynamicReadBuffer* DynamicReadBuffer
ERHIFeatureLevel::Type FeatureLevel
const bool bUseAsyncTasks
TArray<TFunction<void()>, SceneRenderingAllocator> ParallelTasks
TArray<FGPUScenePrimitiveCollector*, TInlineAllocator<2, SceneRenderingAllocator>> DynamicPrimitiveCollectorPerView
  追踪动态图元数据,用于为每个view上传到GPU Scene


##### EVertexFactoryFlags
  UsedWithMaterials
  SupportsStaticLighting
  SupportsDynamicLighting
  SupportsPrecisePrevWorldPos
  SupportsPositionOnly
  SupportsCachingMeshDrawCommands
  SupportsPrimitiveIdStream
  SupportsNaniteRendering
  SupportsRayTracing
  SupportsRayTracingDynamicGeometry
  SupportsRayTracingProceduralPrimitive
  SupportsLightmapBaking
  SupportsPSOPrecaching
  SupportsManualVertexFetch
  DoesNotSupportNullPixelShader
  SupportsGPUSkinPassThrough
  SupportsComputeShading


##### EFVisibleMeshDrawCommandFlags
  MaterialUsesWorldPositionOffset: 为给定的材质球激活WPO
  HasPrimitiveIdStreamIndex: 支持primitive ID stream(dynamic instancing和GPU-Scene instance culling)
  ForceInstanceCulling: 强制独立于图元来剔除单个实例
  PreserveInstanceOrder: 实例在draw command中保存初始绘制顺序. 目前仅支持non-mobile

##### BlendMode
materials: Opaque/Masked/Translucent/Additive/Modulate/AlphaComposite/AlphaHoldout
strata materials: Opaque/Masked/TranslucentGreyTransmittance/TranslucentColoredTransmittance/ColoredTransmittanceOnly/AlphaHoldout

##### MaterialShadingModel
Unlit
DefaultLit
Subsurface
PreintegratedSkin
ClearCoat
SubsurfaceProfile
Hair
Cloth
Eye
SingleLayerWater
ThinTranslucent
Strata



#### MaterialDomain
Surface: 材质的属性描述3d表面
DeferredDecal: 材质属性描述延迟贴花,将会映射到贴花的frustum
LightFunction: 材质属性描述光照的分布
Volume: 3d volume, only in voxellization pass.
PostProcess: custom post process pass
UI: UMG或Slate UI
RuntimeVirtualTexture: runtime virtual texture(deprecated).

##### MeshPass
DepthPass
BasePass
AnisotropyPass
SkyPass
SingleLayerWaterPass
SingleLayerWaterDepthPrepass
CSMShadowDepth
VSMShadowDepth
Distortion
Velocity
TranslucentVelocity
TranslucencyStandard
TranslucencyAfterDOF
TranslucencyAfterDOFModulate
TranslucencyAfterMotionBlur
TranslucencyAll
LightmapDensity
CustomDepth
MobileBasePassCSM
VirtualTexture
LumenCardCapture
LumenCardNanite
LumenTranslucencyRadianceCacheMark
LumenFrontLayerTranslucencyGBuffer
DitheredLODFadingOutMaskPass
NaniteMeshPass
MeshDecal
HitProxy
HitProxyOpaqueOnly
EditorLevelInstance
EditorSelection




r.MeshDrawCommands.LogDynamicInstancingStats = "1"
LogRenderer: Instancing stats for ShadowDepth WholeScene split0
LogRenderer:    4 Mesh Draw Commands in 4 instancing state buckets
LogRenderer:    Largest 1
LogRenderer:    1.0 Dynamic Instancing draw call reduction factor
LogRenderer: Instancing stats for ShadowDepth WholeScene split1
LogRenderer:    11 Mesh Draw Commands in 11 instancing state buckets
LogRenderer:    Largest 1
LogRenderer:    1.0 Dynamic Instancing draw call reduction factor
LogRenderer: Instancing stats for ShadowDepth WholeScene split2
LogRenderer:    27 Mesh Draw Commands in 19 instancing state buckets
LogRenderer:    Largest 5
LogRenderer:    1.4 Dynamic Instancing draw call reduction factor
LogRenderer: Instancing stats for ShadowDepth WholeScene split3
LogRenderer:    124 Mesh Draw Commands in 86 instancing state buckets
LogRenderer:    Largest 8
LogRenderer:    1.4 Dynamic Instancing draw call reduction factor
LogRenderer: Instancing stats for DepthPass
LogRenderer:    69 Mesh Draw Commands in 57 instancing state buckets
LogRenderer:    Largest 5
LogRenderer:    1.2 Dynamic Instancing draw call reduction factor
LogRenderer: Instancing stats for BasePass
LogRenderer:    71 Mesh Draw Commands in 61 instancing state buckets
LogRenderer:    Largest 4
LogRenderer:    1.2 Dynamic Instancing draw call reduction factor
LogRenderer: Instancing stats for SingleLayerWaterPass
LogRenderer:    4 Mesh Draw Commands in 4 instancing state buckets
LogRenderer:    Largest 1
LogRenderer:    1.0 Dynamic Instancing draw call reduction factor
LogRenderer: Instancing stats for Velocity
LogRenderer:    2 Mesh Draw Commands in 2 instancing state buckets
LogRenderer:    Largest 1
LogRenderer:    1.0 Dynamic Instancing draw call reduction factor
LogRenderer: Instancing stats for TranslucencyAfterDOF
LogRenderer:    126 Mesh Draw Commands in 126 instancing state buckets
LogRenderer:    Largest 1
LogRenderer:    1.0 Dynamic Instancing draw call reduction factor
LogRenderer: Instancing stats for HitProxy
LogRenderer:    201 Mesh Draw Commands in 201 instancing state buckets
LogRenderer:    Largest 1
LogRenderer:    1.0 Dynamic Instancing draw call reduction factor




Cmd: r.MeshDrawCommands.LogMeshDrawCommandMemoryStats 1
r.MeshDrawCommands.LogMeshDrawCommandMemoryStats = "1"
LogRenderer: DepthPass: 548.3Kb for 1674 CachedMeshDrawCommands
LogRenderer:      avg 4.0 bytes PSO
LogRenderer:      avg 120.0 bytes ShaderBindingInline
LogRenderer:      avg 7.4 bytes ShaderBindingHeap
LogRenderer:      avg 64.0 bytes VertexStreamsInline
LogRenderer:      avg 96.0 bytes DebugData
LogRenderer:      avg 28.0 bytes DrawCommandParameters
LogRenderer:      avg 16.0 bytes Other
LogRenderer: BasePass: 561.8Kb for 1707 CachedMeshDrawCommands
LogRenderer:      avg 4.0 bytes PSO
LogRenderer:      avg 120.0 bytes ShaderBindingInline
LogRenderer:      avg 9.0 bytes ShaderBindingHeap
LogRenderer:      avg 64.0 bytes VertexStreamsInline
LogRenderer:      avg 96.0 bytes DebugData
LogRenderer:      avg 28.0 bytes DrawCommandParameters
LogRenderer:      avg 16.0 bytes Other
LogRenderer: CSMShadowDepth: 547.2Kb for 1668 CachedMeshDrawCommands
LogRenderer: Velocity: 1.3Kb for 4 CachedMeshDrawCommands
LogRenderer: VirtualTexture: 853.3Kb for 2664 CachedMeshDrawCommands
LogRenderer: LumenCardCapture: 562.8Kb for 1710 CachedMeshDrawCommands
LogRenderer: HitProxy: 730.8Kb for 2220 CachedMeshDrawCommands
LogRenderer: HitProxyOpaqueOnly: 730.8Kb for 2220 CachedMeshDrawCommands
LogRenderer: sizeof(FMeshDrawCommand) 328
LogRenderer: Total cached MeshDrawCommands 6.073Mb
LogRenderer: Primitive StaticMeshCommandInfos 727.0Kb
LogRenderer: GPUScene CPU structures 0.0Kb
LogRenderer: PSO persistent Id table 90.0Kb 453 elements
LogRenderer: PSO one frame Id 15.7Kb