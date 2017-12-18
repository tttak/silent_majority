cmake_minimum_required(VERSION 2.8)

find_package( OpenMP )
if (OPENMP_FOUND)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
endif()

set(CMAKE_CXX_COMPILER "clang++")
add_definitions("-std=c++14 -fno-exceptions -fno-rtti -Wextra -Ofast -MMD -MP")
add_definitions("-Wno-unused-parameter")
set(CMAKE_EXE_LINKER_FLAGS "-Wl,-s -v")

if(NOT DEFINED target)
   set(target "bmi2")
endif()
if(target STREQUAL bmi2)
  add_definitions("-DNDEBUG -DHAVE_SSE4 -DHAVE_SSE42 -DHAVE_BMI2 -msse4.2 -mbmi2 -DHAVE_AVX2 -mavx2 -march=corei7-avx")
elseif(target STREQUAL sse)
  add_definitions("-DNDEBUG -DHAVE_SSE4 -DHAVE_SSE42 -msse4.2 -march=corei7")
elseif(DEFINED target)
  message(FATAL_ERROR "invalid target")
endif()

if (NOT "$ENV{MSYSTEM}" STREQUAL "")
  add_definitions("-fno-threadsafe-statics")
endif()

set(sources
	benchmark.cpp
	benchmark.hpp
	bitboard.cpp
	bitboard.hpp
	book.cpp
	book.hpp
	color.hpp
	common.cpp
	common.hpp
	evalList.cpp
	evalList.hpp
	evaluate.cpp
	evaluate.hpp
	generateMoves.cpp
	generateMoves.hpp
	hand.cpp
	hand.hpp
	ifdef.hpp
	init.cpp
	init.hpp
	learner.hpp
	main.cpp
	move.cpp
	move.hpp
	movePicker.cpp
	movePicker.hpp
	mt64bit.cpp
	mt64bit.hpp
	overloadEnumOperators.hpp
	piece.hpp
	pieceScore.cpp
	pieceScore.hpp
	position.cpp
	position.hpp
	score.hpp
	search.cpp
	search.hpp
	square.cpp
	square.hpp
	thread.cpp
	thread.hpp
	timeManager.cpp
	timeManager.hpp
	tt.cpp
	tt.hpp
	usi.cpp
	usi.hpp
)
add_executable(apery-by-clang ${sources})

if (NOT "$ENV{MSYSTEM}" STREQUAL "")
  target_link_libraries(apery-by-clang -static pthread ${OpenCV_LIBS})
else()
  target_link_libraries(apery-by-clang stdc++fs pthread ${OpenCV_LIBS})
endif()
