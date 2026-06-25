cmake_minimum_required(VERSION 3.16)

project(BinocularUnderwaterRanging LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- OpenCV ----
# Jetson: usually auto-detected at /usr/lib/aarch64-linux-gnu/cmake/opencv4
# If cmake can't find it, pass: -DOpenCV_DIR=/path/to/opencv/build
find_package(OpenCV REQUIRED)

# ---- YOLO / ONNX Runtime (optional) ----
option(ENABLE_YOLO "Enable YOLO/ONNX Runtime integration" ON)
set(ONNXRUNTIME_ROOT "" CACHE PATH "ONNX Runtime root directory (e.g. ~/onnxruntime-linux-aarch64-1.20.1)")

if(ENABLE_YOLO)
	if(NOT ONNXRUNTIME_ROOT)
		# Try common locations
		file(GLOB _ORT_DIRS "$ENV{HOME}/onnxruntime-linux-aarch64-*")
		list(SORT _ORT_DIRS)
		list(REVERSE _ORT_DIRS)
		if(_ORT_DIRS)
			list(GET _ORT_DIRS 0 ONNXRUNTIME_ROOT)
			message(STATUS "Auto-detected ONNX Runtime at ${ONNXRUNTIME_ROOT}")
		endif()
	endif()

	if(ONNXRUNTIME_ROOT)
		set(ORT_INC "${ONNXRUNTIME_ROOT}/include")
		set(ORT_LIB "${ONNXRUNTIME_ROOT}/lib")
		if(NOT EXISTS "${ORT_INC}/onnxruntime_cxx_api.h")
			message(WARNING "onnxruntime_cxx_api.h not found in ${ORT_INC}. YOLO disabled.")
			set(ENABLE_YOLO OFF)
		else()
			find_library(ORT_LIBRARY onnxruntime PATHS "${ORT_LIB}" NO_DEFAULT_PATH)
			if(NOT ORT_LIBRARY)
				# Try soname-based symlink
				file(GLOB _ORT_SO "${ORT_LIB}/libonnxruntime.so*")
				if(_ORT_SO)
					list(GET _ORT_SO 0 ORT_LIBRARY)
				endif()
			endif()
			if(NOT ORT_LIBRARY)
				message(WARNING "libonnxruntime.so not found in ${ORT_LIB}. YOLO disabled.")
				set(ENABLE_YOLO OFF)
			endif()
		endif()
	else()
		message(STATUS "ONNX Runtime not found. YOLO disabled. Set -DONNXRUNTIME_ROOT=/path/to/ort")
		set(ENABLE_YOLO OFF)
	endif()
endif()

# ---- CLI target ----
add_executable(opencv_cli
	main.cpp
	src/common.cpp
	src/mode_calib.cpp
	src/mode_depth.cpp
	src/mode_rectify.cpp
	src/mode_sonar.cpp
	src/mode_yolo.cpp
	src/mode_video.cpp
	src/sonar_reader.cpp
)

target_include_directories(opencv_cli PRIVATE src)
target_link_libraries(opencv_cli PRIVATE ${OpenCV_LIBS})

if(ENABLE_YOLO)
	target_compile_definitions(opencv_cli PRIVATE YOLO_USE_ONNXRUNTIME)
	target_include_directories(opencv_cli PRIVATE "${ORT_INC}")
	target_link_libraries(opencv_cli PRIVATE "${ORT_LIBRARY}")
	message(STATUS "YOLO support enabled: ${ORT_LIBRARY}")
endif()

# ---- Copy web/ to output dir ----
add_custom_command(TARGET opencv_cli POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
	"${CMAKE_SOURCE_DIR}/web" "$<TARGET_FILE_DIR:opencv_cli>/web"
)
