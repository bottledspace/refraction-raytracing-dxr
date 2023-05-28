project(refraction-raytracing-dxr)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS")

add_executable(refraction-raytracing-dxr
	RefractionDemo.cpp
	Mesh.cpp
	WinMain.cpp)
target_link_libraries(refraction-raytracing-dxr PRIVATE
	d3d12.lib
	dxgi.lib
	dxguid.lib
	D3DCompiler.lib)