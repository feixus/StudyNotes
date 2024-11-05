[延迟渲染管线](https://www.cnblogs.com/timlly/p/14732412.html)

<br>

- [ShaderPrint](#shaderprint)
- [ShadingEnergyConservation](#shadingenergyconservation)
- [FScene::UpdateAllPrimitiveSceneInfos](#fsceneupdateallprimitivesceneinfos)
- [UpdateGPUScene](#updategpuscene)

<small><i><a href='http://ecotrust-canada.github.io/markdown-toc/'>Table of contents generated with markdown-toc</a></i></small>


# ShaderPrint

# ShadingEnergyConservation

# FScene::UpdateAllPrimitiveSceneInfos
- FSceneRenderer::WaitForCleanUpTasks  
等待渲染线程的所有任务执行完毕,如WaitOutstandingTasks, 所有EMeshPass及ShadowDepthPass的mesh command setup task, 并删除SceneRenderer.
- VirtualShadowCacheManagers  
    删除primitives,更新Instances或Transform, addpass to VirtualSmInvalidateInstancePagesCS in compute shader(VirtualShadowMapCacheManagement.usf).
- RemovePrimitiveSceneInfos  
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
        - 若bUpdateStaticDrawLists,  不再需要stati mesh update, deallocate potential OIT dynamic index buffer, remove from staticMeshes/staticMeshRelevances/CachedMeshDrawCommands/CachedNaniteDrawCommands.  
    - FreeGPUSceneInstances  
    - <span style="color: green;">GPUScene.AddPrimitiveToUpdate->EPrimitiveDirtyState::Removed</span>
    - CachedShadowMapData.InvalidateCachedShadow  
    - DistanceFieldSceneData.RemovePrimitive  
    - LumenRemovePrimitive  
    - PersistentPrimitiveIdAllocator.Free  
    - ShadowMapData.StaticShadowSubjectMap  


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

- AddPrimitiveSceneInfos   
    PersistentPrimitiveIdAllocator(FSpanAllocator)执行合并.
    扩展各个数组大小(Primitives/PrimitiveTransforms/PrimitiveSceneProxies/PrimitiveBounds...)  
    循环处理待添加的primitives
    从后往前处理,合并处理TypeHash相同的primitive, 各个PrimitiveSceneProxy的派生类的TypeHash相同(采用静态变量的地址).  
    为各个数组(Primitives/PrimitiveTransform/PrimitiveBounds...)添加primitive数据.  
    为PrimitiveSceneInfo分配PackedIndex(PrimitiveSceneProxies的索引)和PersistentIndex(由PersistentPrimitiveIdAllocator分配). PersistentPrimitiveIdToIndexMap记录PersistentIndex到PackedIndex的映射.  
    <span style="color: green;">GPUScene.AddPrimitiveToUpdate->EPrimitiveDirtyState::AddedMask</span>  

    从TypeOffsetTable中寻找Proxy的TypeHash匹配类型, 若遭遇新类型,添加进去.TypeOffsetTable中的每个元素表示TypeHash相同的primitive数量.  
    根据TypeOffsetTable,为每个新添加的primitive在各个数组中排序. 每次和相同类型的末尾交换,以构成相同TypeHash的primitive排列在一起的数组.  
    <span style="color: green;">对于primitiveIds(packedIndex)交换位置的,需要通知GPUScene, 添加入PrimitivesToUpdate, 且交换PrimitiveDirtyState.</span>  

    ``` c++
    // 增加图元示意图：先将被增加的元素放置列表末尾，然后依次和相同类型的末尾交换。
    // PrimitiveSceneProxies[0,0,0,6,6,6,6,6,2,2,2,2,1,1,1,7,4,8,6]
    // PrimitiveSceneProxies[0,0,0,6,6,6,6,6,6,2,2,2,1,1,1,7,4,8,2]
    // PrimitiveSceneProxies[0,0,0,6,6,6,6,6,6,2,2,2,2,1,1,7,4,8,1]
    // PrimitiveSceneProxies[0,0,0,6,6,6,6,6,6,2,2,2,2,1,1,1,4,8,7]
    // PrimitiveSceneProxies[0,0,0,6,6,6,6,6,6,2,2,2,2,1,1,1,7,8,4]
    // PrimitiveSceneProxies[0,0,0,6,6,6,6,6,6,2,2,2,2,1,1,1,7,4,8]
    ```

    加入LightingAttachmentRoot.  
    AllocateGPUSceneInstances: 支持the GPUScene instance data buffer, InstanceStaticMesh/NaniteResources/SkeletalMesh/LandscapeComponentSceneProxy  
    FPrimitiveSceneInfo::AddToScene:  
        IndirectLightingCacheUniformBuffers(point/volume),  
        IndirectLightingCacheAllocation(track a primitive allocation in the volume texture atlas that stores indirect lighting),  
        LightmapDataOffset,  
        ReflectionCaptures(mobile/forwardShading, reflectionCapture/planarReflection),  
        AddStaticMeshes(DrawStaticElements, UpdateSceneArrays, CacheMeshDrawCommands/CacheNaniteDrawCommands/CacheRayTracingPrimitives),  
        AddToPrimitiveOctree,  
        UpdateBounds,  
        UpdateVirtualTexture,  
        find lights that affect the primitives in the light octree(local light/non-local(directional) shadow-casting lights),  
        levelNotifyPrimitives.  
    velocityData/DistanceFieldSceneData/LumenSceneData/flush runtime virtual texture/link LOD parent component/update scene LOD tree.  

- UpdatePrimitiveTransform  
    WorldBounds/LocalBounds/LocalToWorld/AttachmentRootPosition  
    RemoveFromScene: remove the primitive from the scene at its old location. 若是update static draw list, 才会需要删除对应的cache mesh draw command.  
    veloityData updateTransform  
    update primitive transform  
    mark indirect lighting cache buffer dirty  
    <span style="color: green;">GPUScene.AddPrimitiveToUpdate->EPrimitiveDirtyState::ChangedTransform</span>  
    DistanceFieldSceneData/lumen update primitive  
    AddToScene: Re-add the primitive to the scene with the new transform  

- UpdatePrimitiveInstances  
    PrimitiveSceneProxy/CmdBuffer/WorldBounds/LocalBounds/StaticMeshBounds  
    需更新StaticDrawLists的(mesh draw command 存储在FScene::CachedMeshDrawCommandStateBuckets/CachedDrawLists)或者instance count有增加或减少(CmdBuffer), cached mesh draw commands才需要更新. 即RemoveFromScene(true). 更新意味着cached mesh draw command需要删除再添加. 其他的是RemoveFromScene(false).  
    FStaticMeshSceneProxy(static mesh)/FSceneProxy(nanite resources)/FNaniteGeometryCollectionSceneProxy: static elements总是使用proxy primitive uninform buffer, 而不是static draw lists.  
    更新proxy data.  
    标记IndirectLightingCacheBuffer Dirty  
    若instance数量有增加或减少(cmdBuffer), 重新分配GPUSceneInstance, 重新加入DistanceFieldSceneData/Lumen scene data. 否则仅AddPrimitiveToUpdate/distance field scene data update/lumen scene data update.  
    final re-add the primitive to the scene with new transform.  

- UpdatedAttachmentRoots/UpdatedCustomPrimitiveParams/DistanceFieldSceneDataUpdates/UpdatedOcclusionBoundsSlacks  

- DeletePrimitiveSceneInfo  


# UpdateGPUScene  
- 若GGPUSceneUploadEveryFrame(用于调试)或者bUpdateAllPrimitives(shift scene data), 需要更新所有primitives.  
- 过滤PrimitivesToUpdate中已删除的primitive.  
- 为primitivesToUpdate/PrimitiveDirtyState 使用rdg builder 分配对象(FUploadDataSourceAdapterScenePrimitives), 兼容scene primitves和dynamic primitives, 用于上传primitive data 的 UploadGeneral function. dynamic primitives是在initViews期间收集的.  
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
    - 分配对象TaskContext(FTaskContext), 构造PrimitiveUploadInfos/PrimitiveUploader/InstancePayloadUploader/InstanceSceneUploader/InstanceBVHUploader/LightmapUploader/NaniteMaterialUploaders, 每个元素都是FRDGScatterUploader结构.  
    - 构建Task(TaskLambda), lock buffer.  
        构造InstanceUpdates(FInstanceBatcher), 遍历PrimitiveData, 计算Batches及BatchItems(ItemIndex/FirstInstance/NumInstances)  
        更新primitives, 并行执行TaskContext.NumPrimitiveDataUploads次, 将primitive scene data填充至TaskContext.PrimitiveUploader.  
        更新instances, 并行执行InstanceUpdates.UpdateBatches次, 构建每个primitive的每个instance(FInstanceSceneShaderData),添加入TaskContext.  InstanceSceneUploader. 若存在PayloadData(InstancePayloadDataStride), 经由InstanceFlags和PayloadPosition, 为TaskContext.InstancePayloadUploader按序添加数据(HIERARCHY_OFFSET/LOCAL_BOUNDS/EDITOR_DATA/LIGHTSHADOW_UV_BIAS/CUSTOM_DATA).  
        更新instance BVH(FBVHNode)  
        更新lightmap, 仅scene primitives,即静态图元才有light cache data.  
    - AddCommandListSetupTask: Task加入ParallelSetupEvents,等待执行.  
    - PrimitiveUploadBuffer/InstancePayloadUploadBuffer/InstanceSceneUploadBuffer/InstanceBVHUploadBuffer/LightmapUploadBuffer/NaniteMaterialUploaders 各自运行一个scatter upload pass(FRDGScatterCopyCS).  
    - FRDGAsyncScatterUploadBuffer 构造FRDGScatterUploader  
        FRDGAsyncScatterUploadBuffer::Begin: 设置ScatterBytes/UploadBytes, 经由rdg buffer desc为ScatterBuffer/UploadBuffer分配rdg buffer.  
        FRDGAsyncScatterUploadBuffer::End: 构造FRDGScatterCopyCS::FParameters, 根据不同资源类型(ByteBuffer/StructedBuffer/Buffer/Texture),设置不同数据成员. 获取FRDGScatterCopyCS, 根据ComputeConfig.NumLoop, 执行多次scatter upload pass.