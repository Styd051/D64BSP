cmake_minimum_required(VERSION 3.10)
project(D64BSP C)

if(MSVC)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
	# x87 floating point: the seg angles of the maps of the remaster, see the README
	if(CMAKE_SIZEOF_VOID_P EQUAL 4)
		add_compile_options(/arch:IA32)
	endif()
endif()

add_executable(D64BSP
	analyze.c
	blockmap.c
	display.c
	glbsp.c
	level.c
	main.c
	node.c
	reject.c
	seg.c
	system.c
	util.c
	wad.c
)

if(UNIX)
	target_link_libraries(D64BSP m)
endif()
