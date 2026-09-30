# Headless simulation harness for TacticalDeployment.
#
# Compiles the project's real engine-light headers and sources (Source/TacticalDeployment) against
# a minimal UE-core shim (Shim/) and runs scenario tests for every architectural pillar.
#
#   cmake -S Tools/Simulation -B Tools/Simulation/Build -DCMAKE_BUILD_TYPE=Release
#   cmake --build Tools/Simulation/Build -j
#   ctest --test-dir Tools/Simulation/Build --output-on-failure

cmake_minimum_required(VERSION 3.20)
project(TacticalSimulation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(GAME_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/../../Source/TacticalDeployment)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/Generated)

# UnrealHeaderTool output stubs: every public header includes "<Name>.generated.h".
file(GLOB_RECURSE GAME_PUBLIC_HEADERS ${GAME_SOURCE}/Public/*.h)
foreach(Header ${GAME_PUBLIC_HEADERS})
	get_filename_component(HeaderName ${Header} NAME_WE)
	set(Stub ${GENERATED_DIR}/${HeaderName}.generated.h)
	if(NOT EXISTS ${Stub})
		file(WRITE ${Stub} "// UHT stub for the simulation harness.\n#pragma once\n")
	endif()
endforeach()

add_executable(TacticalSimulation
	# Real game sources.
	${GAME_SOURCE}/Private/Core/TacticalTypes.cpp
	${GAME_SOURCE}/Private/Weapons/WeaponStats.cpp
	# Harness.
	Tests/SimTest.cpp
	Tests/Main.cpp
	Tests/Test_FogOfWar.cpp
	Tests/Test_LagCompensation.cpp
	Tests/Test_Gunplay.cpp
	Tests/Test_Movement.cpp
	Tests/Test_RoundsAndSpike.cpp
	Tests/Test_CameraAim.cpp
	Tests/Test_Presentation.cpp
)

target_include_directories(TacticalSimulation PRIVATE
	Shim
	${GENERATED_DIR}
	${GAME_SOURCE}/Public
	Sim
	Tests
)

target_compile_definitions(TacticalSimulation PRIVATE WITH_EDITOR=0 UE_WITH_IRIS=0)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	target_compile_options(TacticalSimulation PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-variable)
endif()

enable_testing()
add_test(NAME TacticalSimulation COMMAND TacticalSimulation)
