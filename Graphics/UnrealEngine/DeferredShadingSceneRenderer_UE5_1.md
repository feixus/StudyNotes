[延迟渲染管线](https://www.cnblogs.com/timlly/p/14732412.html)

<br>

- [FDeferredShadingSceneRenderer::Render](#fdeferredshadingscenerendererrender)
  - [light function atlas](#light-function-atlas)
  - [Virtual Texture](#virtual-texture)
  - [FSceneRenderer::OnRenderBegin](#fscenerendereronrenderbegin)
    - [UpdateAllPrimitiveSceneInfos](#updateallprimitivesceneinfos)
      - [FScene::FUpdateParameters](#fscenefupdateparameters)
    - [FVisibilityTaskData](#fvisibilitytaskdata)
    - [FDeferredShadingSceneRenderer::InitViews](#fdeferredshadingscenerendererinitviews)
    - [UpdateGPUScene](#updategpuscene)
  - [Detail Code](#detail-code)
        - [EMeshPass](#emeshpass)
        - [FInstanceCullingManager](#finstancecullingmanager)
        - [FRelevancePacket](#frelevancepacket)
        - [FPerViewPipelineState](#fperviewpipelinestate)
        - [FFamilyPipelineState](#ffamilypipelinestate)
        - [TPipelineState](#tpipelinestate)
  - [Debug Switch](#debug-switch)

<small><i><a href='http://ecotrust-canada.github.io/markdown-toc/'>Table of contents generated with markdown-toc</a></i></small>

# FDeferredShadingSceneRenderer::Render  
- 在渲染开始之前, 处理场景更新: 更新所有的图元信息/光源信息/所有视图的可见性测试/GPUSceneData上传到GPU.  
- FInitViewTaskDatas 收集到场景更新后的VisibilityTaskData  
- GPUScene 启动FGPUScene::BeginRender,通过Scope会自动析构所有数据.  
- VirtualTexture初始化  
- commit all the pipeline states(FamilyPipelineState/ViewPipelineState).  
- initialize global system textures(FSystemTextures::InitializeTextures).  
- launch light function atlas task
    
    
## light function atlas  
light function 和 lights casting dynamic shadows一样使用同样昂贵的rendering pass. 为了减轻此过程昂贵的GPU消耗, light function atlas用于将animated light function烘培到制作成图集的tiles(2D textures)中.  
这些2D textures存储然后投射到场景, 可以极大的节省GPU时间.  
虽然有一些限制, 但可用于chromatic light function 或者 MegaLights.  
  
(https://dev.epicgames.com/documentation/en-us/unreal-engine/using-light-functions-in-unreal-engine)   
  



## Virtual Texture  
virtual texturing仅运行在ERendererOutput::BasePass | ERendererOutput::FinalSceneColor  
- FVirtualTextureSystem::BeginUpdate  
- VirtualTextureFeedbackBegin  
- virtual texture是否支持, 依据r.VirtualTextures, 若为mobile platform, 还需依据r.Mobile.VirtualTextures  
      
    
## FSceneRenderer::OnRenderBegin  
场景的数据更新通过一系列由RDG builder创建的任务来执行,如:  
GPUSkinCacheTask/UpdateUniformExpressionsTask/AddToScene/AddStaticMeshesTask/LightPrimitiveInteractionsTask/CacheMeshDrawCommandsTask/CacheNaniteMaterialBinsTask/VisibilityTask/GPUSceneUpdate/DeletePrimitiveSceneInfo...     
  
- 在scene update之前清空virtual texture system的回调信息, 以避免和mesh draw command caching tasking产生竞争情况  
- OIT::OnRenderBegin 在scene update之前清空OITSceneData  
- FScene::Update  
	- 构建FScene::FUpdateParameters  
	- 若允许<u>GPUSkinCache</u>, 添加GPUSkinCacheTask(异步并行执行FGPUSkinCache::DoDispatch)  
	- add interface to render graph builder blackboard to receive scene updates from compute, submit updates to modify GPU-scene  
	- FComputeGraphTaskWorker::SubmitWork: submit enqueued compute graph work for ComputeTaskExecutionGroup of EndOfFrameUpdate  
	- WaitForCleanUpTasks/WaitForAsyncExecuteTask: 等待前一帧的scene render结束及 async RDG execution tasks  
	- FMaterialRenderProxy::Update<u>DeferredCachedUniformExpressions</u>  
	- UpdateAllLightSceneInfos  
	- UpdateAllPrimitiveSceneInfos  
  
  

### UpdateAllPrimitiveSceneInfos  
- 使用RDG builder 分配对象FSceneUpdateChangeSetStorage, 此对象则拥有render graph lifetime, 可以被RDG tasks安全引用.  
- FSceneUpdateChangeSetStorage对象持有Added/Removed/Updated(Transforms/Instances) primitive scene infos.  
- 在scene update之前的操作:  
	- 填充PrimitivesToUpdate/PrimitiveDirtyState in GPUScene  
	- 创建并填充SceneUniformBuffer in GPUScene  
	- 重新创建FSceneCulling, 处理所有的removed primitives  
	- RDG builder 分配对象FSceneExtensionsUpdaters, 调用其PreSceneUpdate  
- RemovePrimitiveSceneInfos  
	- <u>FLODSceneTree</u> 清除掉待删除的primitives  
	- 循环处理待删除的primitives, 配合TypeOffsetTable和PrimitiveSceneProxies, 将每个primitive交换移动到各个数组的末端, 并执行删除.  
     - VelocityData.RemoveFromScene  
     - UnlinkAttachmentGroup  
     - UnlinkLODParentComponent  
     - FlushRuntimeVirtualTexture  
     - Remove from scene:  
        - delete the list of lights affecting this primitives.  
        - remove the primitive from the octree.  
        - 若允许GPUScene, 根据lightmapDataOffset和NumLightmapDataEntries,从场景的lightmap data buffer中删除.  
        - remove from potential capsule shadow casters (DynamicIndirectCasterPrimitives).  
        - mark indirect lighting cache buffer dirty, if movable object.  
        - 若bUpdateStaticDrawLists,  不再需要static mesh update, deallocate potential OIT dynamic index buffer, remove from staticMeshes/staticMeshRelevances/CachedMeshDrawCommands/CachedNaniteDrawCommands.  
		- 若注册到virtual texture system, 需销毁  
		- remove from level update notification  
     - FreeGPUSceneInstances  
     - DistanceFieldSceneData.RemovePrimitive  
     - LumenRemovePrimitive  
     - PersistentPrimitiveIdAllocator.Free  

    ``` c++
    // 删除图元示意图：会依次将被删除的元素交换到相同类型的末尾，直到列表末尾
    // PrimitiveSceneProxies[0,0,0,6,X,6,6,6,2,2,2,2,1,1,1,7,4,8]
    // PrimitiveSceneProxies[0,0,0,6,6,6,6,X,2,2,2,2,1,1,1,7,4,8]
    // PrimitiveSceneProxies[0,0,0,6,6,6,6,2,2,2,2,X,1,1,1,7,4,8]
    // PrimitiveSceneProxies[0,0,0,6,6,6,6,2,2,2,2,1,1,1,X,7,4,8]
    // PrimitiveSceneProxies[0,0,0,6,6,6,6,2,2,2,2,1,1,1,7,X,4,8]
    // PrimitiveSceneProxies[0,0,0,6,6,6,6,2,2,2,2,1,1,1,7,4,X,8]
    // PrimitiveSceneProxies[0,0,0,6,6,6,6,2,2,2,2,1,1,1,7,4,8,X]
    ```  
    
- RDG builder分配对象SceneInfosWithAddToScene/SceneInfosWithStaticDrawListUpdate. 收集待分配InstanceId的primitives(added/updateInstances)  
- allocator consolidation for <u>FSpanAllocator</u>(InstanceSceneData/InstancePayloadData/LightmapData/PersistentPrimitiveId)  
- AddPrimitiveSceneInfos  
	- 扩展各个数组大小(Primitives/PrimitiveTransforms/PrimitiveSceneProxies/PrimitiveBounds...)  
    - 循环处理待添加的primitives  
	 - 从后往前处理,合并处理TypeHash相同的primitive, 各个PrimitiveSceneProxy的派生类的TypeHash相同(采用静态变量的地址). 
	 - 为各个数组(Primitives/PrimitiveTransform/PrimitiveBounds...)添加primitive scene info关联数据.  
     - 为每个PrimitiveSceneInfo分配PackedIndex(PrimitiveSceneProxies的索引)和PersistentIndex(由PersistentPrimitiveIdAllocator分配). PersistentPrimitiveIdToIndexMap记录PersistentIndex到PackedIndex的映射.  
	 - 从TypeOffsetTable中寻找Proxy的TypeHash匹配类型, 若遭遇新类型,添加进去.TypeOffsetTable中的每个元素表示TypeHash相同的primitive数量.  
     - 根据TypeOffsetTable,为每个新添加的primitive在各个数组中排序. 每次和相同类型的末尾交换,以构成相同TypeHash的primitive排列在一起的数组.  
	 - 加入LightingAttachmentRoot. 
	 - 对于未采用GPUScene的primitive创建uniform buffer for vector factories(FPrimitiveUniformShaderParameters -> FPrimitiveSceneData)  
	 - 对于moveable的对象, 若支持velocity rendering(mobile需支持desktop Gen4 TAA), 更新localtoworld for velocityData.  
	 - 加入DistanceFieldSceneData  
	 - 加入LumenSceneData  
	 - pending flush virtual texture  
	 - pending add to scene  
	 - pending add static meshes  
	
	``` c++
    // 增加图元示意图：先将被增加的元素放置列表末尾，然后依次和相同类型的末尾交换。
    // PrimitiveSceneProxies[0,0,0,6,6,6,6,6,2,2,2,2,1,1,1,7,4,8,6]
    // PrimitiveSceneProxies[0,0,0,6,6,6,6,6,6,2,2,2,1,1,1,7,4,8,2]
    // PrimitiveSceneProxies[0,0,0,6,6,6,6,6,6,2,2,2,2,1,1,7,4,8,1]
    // PrimitiveSceneProxies[0,0,0,6,6,6,6,6,6,2,2,2,2,1,1,1,4,8,7]
    // PrimitiveSceneProxies[0,0,0,6,6,6,6,6,6,2,2,2,2,1,1,1,7,8,4]
    // PrimitiveSceneProxies[0,0,0,6,6,6,6,6,6,2,2,2,2,1,1,1,7,4,8]
    ```  
  	
- UpdatePrimitiveTransforms  
	- pending add to scene
	- 若允许updateStaticDrawLists, 则 pending add static meshes  
	- RemoveFromScene: remove the primitive from the scene at its old location. 若是update static draw list, 才会需要删除对应的cache mesh draw command.  
	- pending flush virtual texture  
	- 若支持velocity rendering, 更新localtoworld to VelocityData  
	- update primitive transform(WorldBounds/LocalBounds/LocalToWorld/AttachmentRootPosition)  
	- 若不支持<u>VolumeTexture</u>(mobile device) 并且 movable object 则 mark indirect lighting cache buffer dirty  
	- DistanceFieldSceneData/lumen update primitive  
	- overrides the primitive previous localToWorld matrix for this frame only in VelocityData  
  
- UpdatePrimitiveInstances  
	- pending flush virtual texture  
	- pending add to scene  
	- remove from scene, remove static meshes for update static draw list  
	- pending add static meshes for update static draw list  
	- update instances in render thread(WorldBounds/LocalBounds)  
	- 若不支持VolumeTexture(mobile device) 并且 movable object 则 mark indirect lighting cache buffer dirty  
	- 若允许GPUScene且Instance scene data buffer offset发生变化, 则 DistanceFieldSceneData/LumenSceneData需要先删除再重新添加
	否则DistanceFieldSceneData/LumenSceneData直接更新当前数据, 更新PrimitivesToUpdate/PrimitiveDirtyState in GPU Scene  
  
- allocate all GPUScene instances slots(added + updateInstances) in FPrimitiveSceneInfo::AllocateGPUSceneInstances.  
- real add to scene(FPrimitiveSceneInfo::AddToScene)  
	- IndirectLightingCacheUniformBuffers,    
	- IndirectLightingCacheAllocation,    
	- LightmapDataOffset,  
	- ReflectionCaptures(cached closest reflectionCaptureProxy/planarReflectionProxy | mobile HQ rflections),    
	- AddToPrimitiveOctree,  
	- UpdateBounds,  
	- LevelNotifyPrimitives.    
- level commands  
	- add command, 查找对应level关联的所有primitives, pending add static meshes | remove static meshes | 对于NaniteMesh, add PrimitivesToUpdate/PrimitiveDirtyState for GPUScene.    
	- remove command, 查找对应level关联的所有primitives, 对于NaniteMesh, add PrimitivesToUpdate/PrimitiveDirtyState for GPUScene.    
- 对应新增的primitives和待删除的primitives    
	- FSceneCulling::FUpdater::OnPostSceneUpdate  
	- FGPUScene::OnPostSceneUpdate  
	- FScene::UpdateCachedShadowState  
	- FShadowScene::PostSceneUpdate  
	- SceneExtensionsUpdaters.PostSceneUpdate  
- RDG builder 新增任务addStaticMeshesTasks, 前置任务UpdateUniformExpressionsTask.  
	- 可标记bAsyncCacheMeshDrawCommands    
  - 若无前置任务, 则立即执行, 否则为异步执行启动一个任务, 必须在前置任务后执行.  
  - 任务优先级为High  
	- real add static meshes(FPrimitiveSceneInfo::AddStaticMeshes)  
		- 并行执行每个primitive scene proxy的<u>DrawStaticElements</u>  
		- 遍历所有的scene infos, 将每个primitive的所有static meshes添加如 staticMeshes in scene, allocate OIT index buffer for sorted triangles in translucent material  
	- update virtual textures  
	- flush runtime virtual texture  
- update reflection scene data  
- pending update static meshes  
	- 若skylight state(render skylight in basePass)(FScene::ShouldRenderSkylightInBasePass)前一帧和当前帧不一致,场景需要重新创建static draw lists, 即强制更新所有的mesh draw commands  
	- 若前一帧和当前帧的BasePassDepthStencilAccess不一致, 也需要更新所有的static draw lists  
	- 若 <u>pipeline variable rate shading(VRS)</u> 状态发生变更(r.VRS.Enable), 对所有使用non-1x1 per-material shading rates的primitives更新static meshes  
	- 对于待更新的static meshes, 移除cachedMeshDrawCommands/cachedNaniteMaterialBins  
- RDG builder新增任务:CreateLightPrimitiveInteractionsTask, 此类型任务 synced prior to graph execution, 若不允许并行执行, 则立即执行.  
  - 任务优先级为Normal    
	- nanite primitives 默认跳过LightPrimitiveInteraction(ShouldSkipNaniteLPIs)  
	- local light primitive interaction(LocalShadowCastingLightOctree), LPI creation需要在static mesh update之后启动  
    - r.Visibility.LocalLightPrimitiveInteraction 判别局部光是否需要进行图元交互, 禁止可以加速render thread time  
    - mobile platform 不进行此处理  
    - bound test: 使用fast box-box intersection来遍历light octree, 若有交互, create light interaction and add to light/primitive lists  
	- non-local(directional) shadow-casting lights  
	- 可选的异步执行此LightPrimitiveInteraction任务  
- update nanite primitives that need re-caching in GPU Scene(GPUScene.AddPrimitiveToUpdate).  
- real update static meshes  
	- 新增CacheMeshDrawCommandsTask(FPrimitiveSceneInfo::CacheMeshDrawCommands)  
	- 前置任务 AddStaticMeshesTask 以及 mobile platform限定的CreateLightPrimitiveInteractionsTasks  
	- 可选的异步执行CacheMeshDrawCommands  
- 新增 CacheNaniteMaterialBinsTask(FPrimitiveSceneInfo::CacheNaniteMaterialBins), 前置任务AddStaticMeshesTask, 可选的异步执行  
- 收集需更新custom primitive data的primitives  
- nanite materials updater(FMaterialsSceneExtension::FUpdater::PostCacheNaniteMaterialBins)  
- nanite skinning updater(FSkinningSceneExtension::FUpdater::PostMeshUpdate) 
- 对于新增的primitives, set LOD parent information(FPrimitiveSceneInfo::LinkLODParentComponent), update scene LOD tree(FLODSceneTree::UpdateNodeSceneInfo)  
- unlink/link attachment group  
- distance field scene data update  
- primitive occlusion bounds update  
- instance cull distance update  
- draw distance update  
- 执行PostStaticMeshUpdate of FUpdateParameters, 此时启动VisibilityTask, 可视任务的前置任务:AddStaticMeshesTask, 设置GPUSceneUpdateTask的前置任务:ComputeRelevance in VisibilityTask.  
- update uniform buffers  
	- update PrimitivesToUpdate/PrimitiveDirtyState in GPUScene for PrimitivesNeedingUniformBufferUpdate  
	- RDG builder 新增任务, 处理每个需更新的proxy(FPrimitiveSceneProxy::UpdateUniformBuffer), 过滤vertex factories使用GPUScene处理primitive data的primitives  
  
- <u>update GPU scene(FGPUScene::Update)</u>, pull all pending updates from scene and upload primitive & instance data.  
  
- SceneExtensionsUpdaters.PostGPUSceneUpdate  
- RDG builder新增任务: 删除DeletedPrimitiveSceneInfos, HitProxies需要在game thread释放  
- AddStaticMeshesTask.Wait()  
      
    
#### FScene::FUpdateParameters  
- 收集UpdateAllPrimitiveSceneInfos的异步操作集:  
	- 异步创建light和primitive的交互(r.AsyncCreateLightPrimitiveInteractions)    
	- 异步缓存MeshDrawCommands(r.AsyncCacheMeshDrawCommands)  
	- 异步缓存MaterialUniformExpressions(r.AsyncCacheMaterialUniformExpressions 且 非移动平台)    
- GPUSceneUpdateTaskPrerequisites 初始化为空任务  
- Callbacks.PostStaticMeshUpdate  
	- view extension per render view  
	- <u>LensDistortion && PaniniProjection</u>: add a RDG pass to generate the lens distortion LUT  
	- setups FViewInfo::ViewRect according to ViewFamilly's ScreenPercentageInterface  
	- initializes a scene textures config instance from the view family  
	- custom render passes have own view family structure  
	- prepare view state for visibility  
	- hair strands(strands, cards, meshes) (r.HairStrands.Enable)  
		- create hair strands bookmark parameters  
		- wait for GPU skin cache task  
		- select appropriate LOD & geometry type(ProcessHairStrandsBookmark)    
	- LightFunctionAtlas::OnRenderBegin, Lighting 在ERendererOutput::DepthPrepassOnly or ERendererOutput::BasePass时会跳过执行  
	- <u>LaunchVisibilityTasks</u>    
		- SceneRenderer.Allocator(linear bulk allocator in scene renderer)创建对象FVisibilityTaskData  
		- FVisibilityTaskData::LaunchVisibilityTasks  
	- 若允许ParallelSetup(r.RDG.ParallelSetup), GPUSceneUpdateTaskPrerequisites添加前置任务VisibilityTaskData->GetComputeRelevanceTask  
	- trigger GPUSceneUpdateTaskPrerequisites, 这是为了确保visibility处理完成    
    

### FVisibilityTaskData  
此类管理指定的scene renderer关联的所有views与visibility computation相关的所有states.  
当处于parallel mode时, 一个复杂的task graph处理每个visibility stage, pipelines results从一个stage转向另一个stage.  
这避免了除当前局限于render thread的dynamic mesh elements gather之外的major join/fork sync points.  
对于不能从并行性收益或者不支持的平台,render-thread centric mode也支持在渲染线程处理visibility,通过一些并行来支持task threads.  
Visibility 为所有视图单独处理, 每一个view执行多个阶段的pipelined task work.    
view stages:  
	Frustum Cull  		   - primitives经历frustum/distance culled, visible primitive传入下一阶段  
	Occlusion Cull       - primitives 沿着场景中的occluders进行culled, visible primitives传入下一阶段  
	Compute Relevance    - 查询primitives以获取view relevance information, 标识出的dynamic elements 传入下一阶段, static meshes 过滤入各个mesh passes  
接下来的阶段为所有视图执行:    
	Gather Dynamic Mesh Elements(GDME)		- 使用view mask查询标识为dynamic relevance的primitives,以支持dynamic meshes     
	Setup Mesh Passes 						        - 启动任务以生成 static | dynamic meshes的mesh draw command       
    
visibility pipeline使用command pipes, 每个视图有两个pipes: OcclusionCull和Relevance  
当渲染器仅有一个view时, GDME会利用一个command pipe尽可能快的处理relevance requests,实现与相关性的一定重叠,以减少关键路径  
多个views时, 需事先同步Relevance, 因gather需要每个dynamic primitive的view mask  
    
- LaunchVisibilityTasks  
  - 遍历每个view
    - 构建FVisibilityViewPacket,每个view都有自己的visibility task packet, 包含管理task graph的所有状态.  
    - Tasks收集每个view的可见性的各个对应的阶段的任务, LightVisibility/FrustumCull/OcclusionCull/ComputeRelevance, 放入前置  
    - <u>Decompress precomputed occlusion</u>(FSceneViewState::ResolvePrecomputedVisibilityData)  
  - 每个relevance task需等待CacheMeshDrawCommandsTask  
  - GDME 需要等待 GPUSkinCacheTask  
  - 若Visibility是作为async task graph来处理的, 除了dynamic mesh element gather仍在render thread,在这种模式调度下:  
    - 实例化的多视图拥有的secondary view会将visibility data注入primary view. 因此所有视图在启动MergeSecondaryViewsTask(merge secondary visibility maps into the primary view)任务之前必须首先完成culling, 而此merge task也变成relevance pipe commands的前置任务, 因此所有视图共享单个relevance pipe  
    - 若仅有一个视图, gather dynamic mesh elements被推入一个pipe, 在渲染线程执行, 允许和compute relevance work有一些重叠. 单个视图的parallel mode不需要primitive view masks.  
  - 对于多视图, 需要分配PrimitiveViewMasks(FDynamicPrimitiveViewMasks)  
  - Tasks.LightVisibility添加前置任务: FSceneRenderer::ComputeLightVisibility  
  - 若任务可以并行调度  
    - 等待OcclusionTests(FSceneRenderer::WaitOcclusionTests): wait OcclusionSubmittedFence  
    - parallel occlusion culling 不支持 mobile  
    - multi-viewport instanced stereo rendering, 需将所有primitive重定向到primary view的relevance command pipe, secondary viewports因此需要reference  
    - 遍历ViewPackets, parrallel occlusion cull执行Map(FGPUOcclusionParallel::Map), Tasks.BeginInitVisibility添加前置任务: FVisibilityViewPacket::BeginInitVisibility  
    - Tasks.FinalizeRelevance新增任务: FinalizeStaticRelevance, 前置任务事件Tasks.ComputeRelevance  
    - 当前所有的task events连接到prerequisites, 可以安全触发: BeginInitVisibility/LightVisibility/FrustumCull/OcclusionCull/ComputeRelevance  

    
  
    


  
  
	 





### FDeferredShadingSceneRenderer::InitViews
initialize scene's views. Check visibility, build visible mesh commands, etc.  

- prior to visibility  
  - r.StencilForLODDither: Optional stencil dithering optimization during prepasses  
      是否在prepass中使用stencil tests及在base pass中使用depth-equal tests以实现 LOD dithering.  
      若禁止,LOD dithering将会在prepass和base pass中通过clip() instructions使用. 此时会禁止EarlyZ.  
      若允许, 强制一个full prepass.  

  - r.DoLazyStaticMeshUpdate  
      若允许, 可以不将meshes添加到static mesh draw lists, 直到这些meshes是可视的.  

  - Hair Strands(strands/cards/meshes)  
    EHairStrandsBookmark::ProcessLODSelection  
    EHairStrandsBookmark::ProcessGuideInterpolation 不支持mobile  

  - FX System (FNiagaraGpuComputeDispatch/FFXSystemSet)  

  - Draw lines to lights affecting this mesh if its selected in editor  

  - setup motion blur parameters, TAA/TSR(TemporalAASampleIndex, TemporalAASamples, TemporalJitterPixels)...  

- Compute View Visibility  
  - ComputeLightVisibility(异步执行)  
   为每个视图view构造数组VisibleLightInfos(FVisibleLightViewInfo)  
   遍历场景中的每个光源, view frustum cull and distance cull lights in each view. directional light默认无剔除, 并设置MobileLightShaft. draw shapes for reflection capture of non-directional static lighting.  
   initialized the fog constants for each view(exponential fog components).  

  - UpdateReflectionSceneData  
    pack visible reflection captures into the uniform buffer, each with an index to its cubemap array entry.  
    GPUScene primitive data stores closest reflection capture as index into this buffer, so this index which must be invalidate every time outSortData contents change.  
    若SortedCaptures有变更,在forward renderer情形下,所有场景图元需要更新, 是因为将index存储进sorted reflection capture uniform buffer.  
    若有新注册的reflection capture或者注册的reflection capture有transform变更,则标记所有primitives以进行reflection proxy update. mobile则需要重新缓存所有mesh commands.  

  - ConditionalUpdateStaticMeshes  
    场景中收集的不需要visibility check的static meshes. 重新缓存meshDrawCommands/NaniteDrawCommands  

  - scene renderer views  
    - 遍历每个视图view, 重置view的PrimitiveVisibilityMap/StaticMeshVisibilityMap等数组结构.  
    - 对于visibility child views/views with frozen visibility, 更新view的PrimitiveVisibilityMap. 这些视图不需要frustum culling.  
    - 大多数视图是需要标准frustum culling.   
    - update HLOD transition/visibility states to allow use during distance culling.  

    - PrimitiveCull  
      - 设置FPrimitiveCullingFlags (ShouldVisibilityCull/UserCustomCulling/AlsoUseSphereTest/UseFastIntersect/UseVisibilityOctree/NaniteAlwaysVisible/HasHiddenPrimitives/HasShowOnlyPrimitives).  
      - 若使用VisibilityOctree, cull octree, 获取visible nodes.  
      - 并行执行(ParallelFor) PrimitiveVisibilityMap, frustum cull and distance cull.  

    - UpdatePrimitiveFading  
      对于相机移动很大的距离或者一会内不会渲染某个view的帧,最好禁用fading,以便看不到无预期的object transitions. PrimitiveFadeUniformBuffers/PrimitiveFadeUniformBufferMap  

    - OcclusionCull  
      在OpenGL平台禁止HZB, 以避免rendering artifacts.  
      precomputed visibility data, 根据场景的PrimitiveVisibilityIds(FPrimitiveVisibilityId)来设置. <span style="color: yellow;">r.VisualizeOccludedPrimitives</span>可绘制被遮挡图元为box.  
      Map HZB Results(WaitingForGPUQuery)  
      执行 round-robin occlusion queries, 对于stereo views,偶数帧right eye执行occlusion querying, 奇数帧left eye执行occlusion querying. recycle old queries.  
      并行执行(ParallelFor)FetchVisibilityForPrimitives, 收集QueriesToRelease/HZBBoundsToAdd/QueriesToAdd/SubIsOccluded/PrimitiveOcclusionHistory.  
      add/release query ops use stored PrimitiveHistory pointers.  
      HZB Unmap Results  

    - 再次conditional update static meshes in primitiveVisibilityMap | PrimitivesNeedingStaticMeshUpdate  
  
    - ComputeAndMarkRelevanceForViewParallel  
      - 构造FMarkRelevantStaticMeshesForViewData, 设置ViewOrigin/ForcedLODLevel/LODScale(r.StaticMeshLODDistanceScale)/MinScreenRadiusForCSMDepthSquared/MinScreenRadiusForDepthPrepassSquared/bFullEarlyZPass(mobile必须满足场景的EarlyZPassMode为DDM_AllOpaque).  
      - 根据View.StaticMeshVisibilityMap的数量分配MarkMasks, 额外增加31, some padding to simplify the high speed transpose(?), 并全部置为0.  
      - 设置Packets, 每个Packet容纳127个primitive.  
      - 并行执行所有的Packets(ParallelFor), 每个Packet(FRelevancePacket), 执行AnyThreadTask, 在此进行ComputeRelevance和MarkRelevant.  
      - 遍历Packets, 每个Packet执行RenderThreadFinalize.  
      - TransposeMeshBit  

    - GatherDynamicMeshElements: gather FMeshBatches from scene proxies  
    - <span style="color: yellow;">DumpPrimitives</span> 控制台命令可将所有scene primitives(如InstancedStaticMeshComponent/LandscapeComponent/StaticMeshComponent...)打印到csv file  
    - SetupMeshPass: FMeshBatches to FMeshDrawCommands for dynamic primitives  
    - 等待ComputeLightVisibilityTask  
  
- updateSkyIrradianceGpuBuffer  
  skylights with static lighting已经将diffuse contribution烘培到lightmaps, 不需要再上传Irradiance  
  若允许RealTimeCapture, the buffer将会在GPU直接设置  
  GRenderGraphResourcePool分配FRDGPooledBuffer至SkyIrradianceEnvironmentMap  
  RHICmdList.Transition, 设置先前的状态为ERHIAccess::Unknown,新状态为ERHIAccess::SRVMask(SRVCompute | SRVGraphics)  
  对于不上传Irradiance的,需确保sky irradiance SH buffer包含合理的初始值(即0)  
  对于上传Irradiance的, 上传数据为8个FVector4f, 从SkyLight->IrradianceEnvironmentMap提取3阶Spherical Harmonics(SH) coefficients, 共27个系数. 最后一个FVector4f填充SkyLight->AverageBrightness.  
  
- init skyAtmosphere/view resources before the view global uniform buffer is built  
  利用GRenderTargetPool创建IPooledRenderTarget: SkyAtmosphere.TransmittanceLut | SkyAtmosphere.MultiScatteredLuminanceLut | SkyAtmosphere.DistantSkyLightLut  
  r.SkyAtmosphere.TransmittanceLUT 可以配置采样数(10),LUT低质量图片格式R8G8B8A8,LUT的尺寸(256*64)  
  r.SkyAtmosphere.MultiScatteringLUT 可以配置采样数(15), LUT高质量(64采样), LUT尺寸(32*32)  
  r.SkyAtmosphere.DistantSkyLightLUT: sky ambient lighting, 可以设置海拔(Altitude-6km), 采集天空样本的高度以集成到sky lighting.  
  
- postVisibilityFrameSetup  
  排序View.MeshDecalBatches, 裁剪上一帧的light shafts的Temporal AA result  
  gather reflection capture light mesh elements  
  start a task to update the indirect lighting cache  
  indirect lighting cache(interpolates and caches indirect lighting for dynamic objects) primitive update: (r.IndirectLightingCache | r.AllowStaticLighting | precomputed light volumes)  
  precomputedLightVolumes used for interpolating dynamic object lighting, typically one per streaming level, store volume lighting samples computed by lightmass.  
  
- initViewsBeforePrePass  
  shadow culling tasks(r.EarlyInitDynamicShadows)  and DynamicShadows flag(ViewFamily.EngineShowFlags.DynamicShadows)  
  point light shadow不支持mobile, mobile可以配置movable spot light shadow(r.Mobile.EnableMovableSpotlightsShadow), movable directional light(r.Mobile.AllowMovableDirectionalLights)  
  为受单个光源影响的所有primitives创建一个projected shadow, 可以缓存ShadowMap, 可以使用VirtualShadowMap.  
  hair strands可以cast shadow(non-directional light)  
  允许movable和stationary lights创建CSM, 或者 static lights that are unbuilt. mobile renderer: light仅可为dynamic objects创建CSM.  
  creatint per-object shadow, including opaque and translucent shadows, for a given light source and primitive interaction.  
  gathers the list of primitives used to draw various shadow types.  
  
- initRHIResources  
  updatePreExposure/UpdateHairResources/initialize per-view uniform buffer  
  
- start render  
  初始化GVisualizeTexture, 可通过<span style="color: yellow;">VisualizeTexture/Vis</span> <RDGResourceName> 查看RDG资源  
  
- RHICmdList.ImmediateFlush for EImmediateFlushType::DispatchToRHIThread  


    




### UpdateGPUScene  
- BeginRender  
GPUScene在渲染器的入口函数  
缓存FGPUSceneDynamicContext的引用, 此类用来包含和控制场景渲染收集的任何dynamic primitive的生命周期, 和SceneRenderer共享生命周期  
调用RegisterBuffers, 将PrimitiveBuffer/InstanceSceneDataBuffer/InstancePayloadBuffer/LightmapDataBuffer/LightDataBuffer注册为RDG的external buffer,以便被render graph追踪  
  
- FGPUScene::Update  主要更新场景中的primitives.  
  - 若GGPUSceneUploadEveryFrame(用于调试)或者bUpdateAllPrimitives(shift scene data), 需要更新所有primitives.  
  - 过滤PrimitivesToUpdate中已删除的primitive.  
  - 为primitivesToUpdate/PrimitiveDirtyState 使用rdg builder 分配对象(FUploadDataSourceAdapterScenePrimitives), 兼容scene primitves和dynamic primitives of view(存储方式不同), 用于上传primitive data 的 UploadGeneral function. scene primitves包含如static mesh/skeletal mesh/particle...  
  - UpdateBufferState(GPU-Scene使用的Buffers)  
    设置BufferState:  
      PrimitiveBuffer: 匹配FPrimitiveUniformShaderParameters, FPrimitiveSceneData(SceneData.ush)  
      InstanceSceneDataBuffer: 匹配FInstanceSceneShaderData, FInstanceSceneData(sceneData.ush), (struct of arrays).  
      InstancePayloadDataBuffer: 匹配FVector4f.  
      InstanceBVHBuffer: 匹配FBVHNode.  
      LightmapDataBuffer: 匹配FLightmapSceneShaderData, FPrecomputedLightingUniformParameters  
    设置ShaderParameters(FGPUSceneResourceParameters), 通过rdg builder为各个buffer创建SRV descriptor  

  - run a pass that clears any instances(sets ID to invalid) that need it. (FGPUSceneSetInstancePrimitiveIdCS). 即将InstanceRangesToClear范围内的数据primitive id 设置为无效的  
  - 抽出仅需要更新primitive id的instances, 即在PrimitiveDirtyState对应元素标记为EPrimitiveDirtyState::ChangedId. 添加一个pass. (FGPUSceneSetInstancePrimitiveIdCS)  
  - 重置PrimitivesToUpdate,PrimitiveDirtyState  
  - UploadGeneral (此处针对FUploadDataSourceAdapterScenePrimitives)  
      - 分配对象TaskContext(FTaskContext), 构造PrimitiveUploadInfos/PrimitiveUploader/InstancePayloadUploader/InstanceSceneUploader/InstanceBVHUploader/LightmapUploader/NaniteMaterialUploaders, 每个元素都是FRDGScatterUploader结构.启动FRDGAsyncScatterUploadBuffer::Begin.  
      - 构建Task(TaskLambda), lock buffer.  
          构造InstanceUpdates(FInstanceBatcher), 遍历PrimitiveData, 计算Batches及BatchItems(ItemIndex/FirstInstance/NumInstances)  
          更新primitives, 并行执行TaskContext.NumPrimitiveDataUploads次, 将primitive scene data填充至TaskContext.PrimitiveUploader.  
          更新instances, 并行执行InstanceUpdates.UpdateBatches次, 构建每个primitive的每个instance(FInstanceSceneShaderData),添加入TaskContext.InstanceSceneUploader. 若存在PayloadData(InstancePayloadDataStride), 经由InstanceFlags和PayloadPosition, 为TaskContext.InstancePayloadUploader按序添加数据(HIERARCHY_OFFSET/LOCAL_BOUNDS/EDITOR_DATA/LIGHTSHADOW_UV_BIAS/CUSTOM_DATA).  
          更新instance BVH(FBVHNode)  
          更新lightmap, 仅scene primitives中的static primitives才有light cache data.  
      - FRDGBuilder::AddCommandListSetupTask: Task加入ParallelSetupEvents待执行.    
      - 启动FRDGAsyncScatterUploadBuffer::End(PrimitiveUploadBuffer/InstancePayloadUploader/InstanceBVHUploader/LightmapUploader/NaniteMaterialUploaders), PrimitiveUploadBuffer/InstancePayloadUploadBuffer/InstanceSceneUploadBuffer/InstanceBVHUploadBuffer/LightmapUploadBuffer/NaniteMaterialUploaders 各自运行一个scatter upload pass(FRDGScatterCopyCS, ByteBuffer.usf:ScatterCopyCS).  
      - FRDGAsyncScatterUploadBuffer 构造FRDGScatterUploader  
          FRDGAsyncScatterUploadBuffer::Begin: 设置ScatterBytes/UploadBytes, 经由rdg buffer desc为ScatterBuffer/UploadBuffer分配rdg buffer.  
          FRDGAsyncScatterUploadBuffer::End: 构造FRDGScatterCopyCS::FParameters, 根据不同资源类型(ByteBuffer/StructedBuffer/Buffer/Texture),设置不同数据成员. 获取FRDGScatterCopyCS, 根据ComputeConfig.NumLoop, 执行多次scatter upload pass.  

  - 将InstanceSceneDataBuffer/InstancePayloadDataBuffer/PrimitiveBuffer/LightmapDataBuffer/InstanceBVHBuffer 添加入 ExternalAccessQueue(FRDGExternalAccessQueue), 等待提交到RDG, 以作为外部可访问资源.  

- FGPUScene::UploadDynamicPrimitiveShaderDataForView  上传来自view.DynamicPrimitiveCollector的primitives  
    - 获取View.DynamicPrimitiveCollector(FGPUScenePrimitiveCollector).  
    - FGPUScenePrimitiveCollector::Commit: GPUScene分配的范围,为上传入队数据.此方法调用后,不再允许添加新的,仅在FGPUScene::Begin/EndRender block内允许. 赋值于 FGPUScenePrimitiveCollector::PrimitiveIdRange, 这便是GPUScene专门为dynamic primitives分配的范围.  
    - 获取FGPUScenePrimitiveCollector::UploadData, 一次性上传.  
        构造FUploadDataSourceAdapterDynamicPrimitives  
        更新BufferState  
        FGPUScene::UseInternalAccessMode, 此BufferState内的所有数据通知GraphBuilder,设置为InternalAccessMode.  
        run a pass that clears (Sets ID to invalid) any instances that need it.  
        UploadGeneral: 针对(FUploadDataSourceAdapterDynamicPrimitives)  

    - update view uniform buffer(FViewUniformShaderParameters): primitiveSceneData,LightmapSceneData,InstancePayloadData, InstanceSceneData, InstanceSceeDataSOAStride.  
    - 执行任意instance data GPU writer回调. 若存在FUploadData::GPUWritePrimitives, 遍历每个PrimitiveIndex,判别任意GPU writers是即刻执行,还是延迟到稍后的GPU write pass. 将此BufferState数据设置为ExternalAccessMode,若ExternalAccessQueue已存在此buffer会忽略.  

- FGPUScene::DebugRender 
    启动<span style="color: yellow;">r.GPUScene.DebugMode</span>. 根据FInstanceSceneData的LocalBoundsCenter和LocalBoundsExtent绘制 scene primitives的范围盒, 可以将选中的scene primitives打印在屏幕上(primitiveId,instanceId,shadow,velocity,customData,DynamicData,name).  

- FInstanceCullingManager::BeginDeferredCulling    

- FScene::UpdatePhysicsField (Physics Field)


## Detail Code  
    
##### EMeshPass  
- DepthPass  
- SecondStageDepthPass  
- BasePass  
- AnisotropyPass  
- SkyPass  
- SingleLayerWaterPass  
- SingleLayerWaterDepthPrepass  
- CSMShadowDepth  
- VSMShadowDepth  
- OnePassPointLightShadowDepth  
- Distortion  
- Velocity  
- TranslucentVelocity  
- TranslucencyStandard  
- TranslucencyStandardModulate  
- TranslucencyAfterDOF  
- TranslucencyAfterDOFModulate  
- TranslucencyAfterMotionBlur  
- TranslucencyHoldout  
- TranslucencyAll  
- LightmapDensity  
- DebugViewMode  
- CustomDepth  
- MobileBasePassCSM  
- VirtualTexture  
- LumenCardCapture  
- LumenCardNanite  
- LumenTranslucencyRadianceCacheMark  
- LumenFrontLayerTranslucencyGBuffer  
- DitheredLODFadingOutMaskPass  
- NaniteMeshPass  
- MeshDecal_DBuffer  
- MeshDecal_SceneColorAndGBuffer  
- MeshDecal_SceneColorAndGBufferNoNormal  
- MeshDecal_SceneColor  
- MeshDecal_AmbientOcclusion  
- WaterInfoTextureDepthPass  
- WaterInfoTexturePass  
- HitProxy  
- HitProxyOpaqueOnly  
- EditorLevelInstance  
- EditorSelection  
    

##### FInstanceCullingManager
  为所有instanced draws管理indirect arguments和culling jobs的分配(使用GPU Scene culling).  

- BeginDeferredCulling  
    添加一个deferred, batched, gpu culling pass. 每个batch表达来自一个mesh pass的BuildRenderingCommands调用. 当完成main render setup及调用BuildRenderingCommands时收集的Batches, 会在RDG执行或Drain被调用时处理.  
    build rendering commands中的views referenced需要在BeginDeferredCulling之前注册. 调用FlushRegisteredViews来上传registered views至GPU.  


##### FRelevancePacket

- ComputeRelevance  
  - 遍历此Packet收集的Primitives, 从各个FPrimitiveSceneProxy收集StaticRelevance/DrawRelevance/DynamicRelevance/ShadowRelevance/EditorRelevance/EditorVisualizeLevelInstanceRelevance/EditorSelectionRelevance/TranslucentRelevance.  
  - 根据不同的Relevance类型, 将每个primitive划分到不同的集合.  
  
- MarkRelevant  仅执行static relevance primitives  
  - 遍历RelevantStaticPrimitives中缓存的static primitives, 计算每个primitive的LOD, HLODFading/HLODFadingOut/LODDithered等  
    - 遍历每个primitive的static mesh relevances  
      - overlay mesh拥有自己的cull distance, 短于primitive cull distance, 执行distance culled.  
      - 根据HLODFading/HLODFadingOut/LODDithered计算出HLOD是否替换以隐藏mesh LOD levels.  
      - 参与primitive distance cull fading(View.PrimitiveFadeUniformBufferMap)或者MeshDitheringLOD的不能缓存mesh command.  
      - 增加visible mesh draw command(FVisibleMeshDrawCommand), 针对缓存mesh command(static mesh)的primitives, 根据图元的FMeshDrawCommand及StaticMeshCommandInfos来构造VisibleMeshDrawCommand, 针对dynamic mesh, 仅收集信息    
        - Velocity/TranslucentVelocity/DepthPass/DitheredLODFadingOutMaskPass  
        - mobile: BasePass/MobileBasePassCSM 或者 SkyPass, 其他平台: BasePass/SkyPass/SingleLayerWaterPass/SingleLayerWaterDepthPrepass  
        - AnisotropyPass/CustomDepth/LightmapDensity  
        - Editor: DebugViewMode/HitProxy/HitProxyOpaqueOnly  
        - TranslucencyStandard/TranslucencyAfterDOF/TranslucencyAfterDOFModulate/TranslucencyAfterMotionBlur 或者 TranslucencyAll  
        - LumenTranslucencyRadianceCacheMark/LumenFrontLayerTranslucencyGBuffer  
        - Distortion  
        - EditorLevelInstance/EditorSelection  
      - 具备VolumeMaterialDomain, 收集到VolumetricMeshBatches  
      - 具备UsesSkyMaterial, 收集到SkyMeshBatches  
      - translucency relevance且material支持sorted triangles, 收集到SortedTrianglesMeshBatches. 在渲染translucent material时, 允许dynamic triangle重新排序可以移除/减少排序问题(order-independent-transparency).  
      - mesh decal 收集到MeshDecalBatches  
  
- RenderThreadFinalize 将计算标记的primitives数据写入View  
  - 针对NotDrawRelevant, 设置对应的view.PrimitiveVisibilityMap  
  - add hit proxies from EditorVisualizeLevelInstancePrimitives/EditorSelectedPrimitives  
  - 设置属性: ShadingModelMask/GlobalDistanceField/LightingChannel/TranslucentSurfaceLighting/SceneDepth/SkyMaterial/SingleLayerWterMaterial/TranslucencySeparateModulation/VisibleDynamicPrimitives/DistortionPrimitives/CustomDepthPrimitives/CustomDepth/CustomStencil  
  - 设置数组: VisibleDynamicPrimitivesWithSimpleLights/TranslucentPrimCount/CustomDepthStencil/DirtyIndirectLightCacheBufferPrimitives/MeshDecalBatches/VolumetricMeshBatches/SkyMeshBatches/SortedTrianglesMeshBatches  
  - 对于ReflectionCapturePrimitives/LazyUpdatePrimitives, 更新每个图元的uniform shader parameters, 加入GPUScene.AddPrimitiveToUpdate  
  - View.PrimitivesLODMask
  - 遍历EMeshPass, 填充View.MeshCommands/DynamicMeshCommandBuildRequests  
  - translucent self shadow uniform buffers. translucency shadow projection uniform buffer containing data needed for Fourier opacity maps(simulate volumetric effects).  
  
  
##### FPerViewPipelineState
FViewInfo在延迟着色管线中包含最终状态的结构体
  
- diffuse indirect method: Disabled/SSGI/Lumen/Plugin  
- ScreenSpaceDenoiserMode for diffuse indirect: Disable/DefaultDenoiser/ThirdPartyDenoiser  
- ambient occlusion method: Disabled/SSAO/SSGI/RTAO  
- reflection method: Disables/SSR/Lumen  
- reflection on water: Disables/SSR/Lumen  
- whether there is planar reflection to compose to the reflection  
- whether need to generate HZB from the depth buffer  
RTGI and RTR is already deprecated in UE5.5  
    
##### FFamilyPipelineState
FSceneViewFamily  
whether bNanite or bHZBOcclusion is enables  
  
##### TPipelineState  
封装处理大量维度的渲染器的pipeline state. 通过结构体内的内存偏移的排序来确保维度内没有循环引用.  
如 FPerViewPipelineState/FFamilyPipelineState  
  
  
## Debug Switch  
- r.MeshDrawCommands.LogDynamicInstancingStats: 在下一帧打印dynamic instance stats  
- DumpPrimitives: 将场景内所有primitive names写入到CSV file  
- DumpDetailedPrimitives: 将场景内所有primitive details写入到CSV file  
- r.SkinCache.Visualize overview/memory/off: 可视化GPU skin cache 数据  (仅可在编辑器下可视)  
  list skincacheusage 可打印出每个骨骼网格的skincache使用类型  
- r.HZBOcclusion 选择occlusion system: 0-hardware occlusion queries, 1-HZB occlusion system(default, less GPU and CPU cost, 更保守的结果) 2-force HZB occlusion system(覆盖渲染平台的偏好设置)  
      
- r.Visibility.LocalLightPrimitiveInteraction 局部光和图元交互的开关  
- r.Visibility.DynamicMeshElements.NumMainViewTasks 控制gather dynamic mesh elements tasks的数量(默认为4),以便在view visibility期间异步运行  
- r.Visibility.FrustumCull.Enabled 视锥体剔除开关  
- 