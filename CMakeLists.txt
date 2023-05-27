project(Loutre)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS")

add_executable(Refraction
	Refraction.cpp
	WinMain.cpp)
target_link_libraries(Refraction PRIVATE
	d3d12.lib
	dxgi.lib
	dxguid.lib
	D3DCompiler.lib)