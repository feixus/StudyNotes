1. Spherical Harmonics(SH) for diffuse reflectance on light probes  
	https://www.youtube.com/watch?v=5PMqf3Hj-Aw  
	https://google.github.io/filament/Filament.html#toc9.6  
	https://beatthezombie.github.io/sh_post_1/  
	https://github.com/Beatthezombie/SphericalHarmonicsFromScratch/tree/main  
	https://beatthezombie.github.io/sh_post_2/  
	
2. LinearTransformCosine for area light  
	https://eheitzresearch.wordpress.com/415-2/  
	https://drive.google.com/file/d/0BzvWIdpUpRx_d09ndGVjNVJzZjA/view?resourcekey=0-21tmiqk55JIZU8UoeJatXQ  
	https://learnopengl.com/Guest-Articles/2022/Area-Lights  
	
3. ImageBasedLighting(IBL)  
	https://google.github.io/filament/Filament.html#toc9.2  
	https://google.github.io/filament/Filament.html#importancesamplingfortheibl  
	
4. PBR on mobile  
	https://community.arm.com/cfs-file/__key/communityserver-blogs-components-weblogfiles/00-00-00-20-66/  siggraph2015_2D00_mmg_2D00_renaldas_2D00_slides.pdf  
	https://www.unrealengine.com/en-US/blog/physically-based-shading-on-mobile  

5. PDF(Probability Density Function)/CDF(Cumulative distribution function)/Importance Sampling  
	a continuous function that specify the probability of the continuous random variable falling with a particular range of values.  such as normal distribution.  
   
   https://stats.libretexts.org/Courses/Saint_Mary's_College_Notre_Dame/MATH_345__-_Probability_(Kuter)  
  
   https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-20-gpu-based-importance-sampling  
   https://www.tobias-franke.eu/log/2014/03/30/notes_on_importance_sampling.html  
   https://github.com/google/filament/blob/main/libs/ibl/src/CubemapIBL.cpp  
   https://webee.technion.ac.il/shimkin/MC15/MC15lect4-ImportanceSampling.pdf  
   https://ib.berkeley.edu/labs/slatkin/eriq/classes/guest_lect/mc_lecture_notes.pdf  
   https://medium.com/@msuhail153/importance-sampling-9d115e43923  
   https://medium.com/@liuec.jessica2000/importance-sampling-explained-end-to-end-a53334cb330b  

6. Visibility Octree  

7. round-robin occlusion queries  
   
8. Distance Field  
  
9.  Stupid Spherical Harmonics (SH) Tricks  

10. randomized algorithms: Monte Carlo(eg. Monte Carlo integration) and Las Vegas(eg. quickSort)

11. shadow depth map  
	CSM to alleviate perspective aliasing and projective aliasing.  
	Depth bias or slope scaled depth bias to mitigate shadow acne and erroneous self-shadowing.  
	tightly fitting the light's projection to the view frustum increses the shadow map coverage.  
	https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps
	
	depth precision(reverse-Z)
    https://developer.nvidia.com/content/depth-precision-visualized
	https://iolite-engine.com/blog_posts/reverse_z_cheatsheet
	
12. computer shader  
	DX12: thread->thread group->dispatch, SV_GroupID/SV_GroupThreadID/SV_DispatchThreadID/SV_GroupIndex, the max numThreads is 1024. 
	https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sm5-attributes-numthreads  
	https://www.stefanpijnacker.nl/article/compute-with-directx12-part-1/  
	https://logins.github.io/graphics/2020/10/31/D3D12ComputeShaders.html  

13. Render Dependency Graph(RDG)  
		fences are expensive so to build a deferred barrier system.  
		use right resource shader visibility on resource views and samplers.  
		modify Root Signature and PSOs is expensive, so cache root signatures and PSOs as much as possible, and reduce changes to such objects to the minimum.  
  		
	RDG 101 A Crsah Course(https://epicgames.ent.box.com/s/ul1h44ozs0t2850ug0hrohlzm53kxwrz)  
	https://www.gdcvault.com/play/1024656/Advanced-Graphics-Tech-Moving-to  
	https://logins.github.io/graphics/2021/05/31/RenderGraphs.html  
	https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine?application_version=5.5  
	https://github.com/staticJPL/Render-Dependency-Graph-Documentation/tree/main  
	https://mcro.de/c/rdg  
	
14. Raytracing  
	https://landelare.github.io/2023/02/18/dxr-tutorial.html  
	https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html  
	https://github.com/microsoft/DirectX-Graphics-Samples/tree/master  
    https://developer.nvidia.com/rtx/raytracing/dxr/DX12-Raytracing-tutorial-Part-1  
	  
15. Sky  
	A Practical Analytic Model for Daylight (https://dl.acm.org/doi/pdf/10.1145/311535.311545)  
	
16. Monte Carlo Integration  
	PDF为求和公式(Monte Carlo estimator)的分母的原因 (https://www.pbr-book.org/4ed/Monte_Carlo_Integration/Monte_Carlo_Basics)  
	
	
	
	
  
	
	
	
   


