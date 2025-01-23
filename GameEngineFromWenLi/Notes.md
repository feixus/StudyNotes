1. clang in Docker on linux, 编译c文件时,clang寻找不到xcb/xcb.h, 但能在/usr/include/xcb中查找到. 通过apt安装的clang或gcc却可以编译成功.  
  此Dockerfile中并没有加入xcb, 可以在此Dockerfile的基础上再自定义一个新的Dockerfile.  
  https://stackoverflow.com/questions/58191215/how-to-add-python-libraries-to-docker-image  
   
    
2. OpenGL API 是一种规范, 具体实现由显卡的驱动程序实现,而显卡的驱动程序因厂商不同而不同,同一厂商也有不同版本的驱动程序. 因此需要在运行时动态查找这些API所在的位置,即API在内存上的地址.   
  
3. 顶点的数据结构, GPU在运行时会挨个读入内部寄存器,交给Shader执行器来执行Shader代码. 复杂的数据结构会占用更多的内部寄存器,减少了可以并行执行的shader, 导致渲染时间的增加. 如何将顶点数据中不需要的顶点属性删除?  
  
4. 索引数据如何排列才能利用Cache来跳过重复索引的计算?  
   

5. explicit constructor function for struct /  noexcept function
   explicit constructor prevents implicit conversions, only for direct/copy/direct initialization.  
  


