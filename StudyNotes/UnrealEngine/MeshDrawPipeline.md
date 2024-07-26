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
  对于从场景数据中手动提取图元数据的vertex factory, 必须为空. 此时使用FPrimitiveSceneProxy::UniformBuffer.(GPUScene)

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
  网格元素的批次.所有网格元素拥有相同的mesh和vertex buffer
- TArray<FMeshBatchElement,TInlineAllocator<1> > Elements
  FMeshBatchElements批次. TInlineAllocator<1>表明Elements数组至少有1个元素.
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
- bUseSelectionOutline
- bSelectable
- bDitheredLODTransition
- bRenderToVirtualTexture
- RuntimeVirtualTextureMaterialType : RuntimeVirtualTexture::MaterialType_NumBits
- bOverlayMaterial
- CastRayTracedShadow
  