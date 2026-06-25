cmake_minimum_required(VERSION 3.16)

project(BinocularUnderwaterRanging LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- OpenCV ----
# Jetson: usually auto-detected at /usr/lib/aarch64-linux-gnu/cmake/opencv4
# If cmake can't find it, pass: -DOpenCV_DIR=/path/to/opencv/build
find_package(OpenCV REQUIRED)

# ---- YOLO backends (optional) ----
option(ENABLE_YOLO "Enable YOLO integration" ON)
option(ENABLE_ONNXRUNTIME "Enable ONNX Runtime backend" ON)
option(ENABLE_TENSORRT "Enable TensorRT backend" ON)
set(ONNXRUNTIME_ROOT "" CACHE PATH "ONNX Runtime root directory (e.g. ~/onnxruntime-linux-aarch64-1.20.1)")

# ---- ONNX Runtime detection ----
set(ORT_ENABLED OFF)
if(ENABLE_YOLO AND ENABLE_ONNXRUNTIME)
	if(NOT ONNXRUNTIME_ROOT)
		file(GLOB _ORT_DIRS
			"$ENV{HOME}/onnxruntime-linux-aarch64-*"
			"$ENV{HOME}/Desktop/onnxruntime-linux-aarch64-*")
		list(SORT _ORT_DIRS)
		list(REVERSE _ORT_DIRS)
		if(_ORT_DIRS)
			list(GET _ORT_DIRS 0 ONNXRUNTIME_ROOT)
		endif()
	endif()
	if(ONNXRUNTIME_ROOT)
		message(STATUS "ONNX Runtime root: ${ONNXRUNTIME_ROOT}")
	else()
		message(STATUS "ONNX Runtime root not set. Pass -DONNXRUNTIME_ROOT=/path/to/ort")
	endif()

	if(ONNXRUNTIME_ROOT)
		set(ORT_INC "${ONNXRUNTIME_ROOT}/include")
		set(ORT_LIB "${ONNXRUNTIME_ROOT}/lib")
		if(NOT EXISTS "${ORT_INC}/onnxruntime_cxx_api.h")
			message(WARNING "onnxruntime_cxx_api.h not found in ${ORT_INC}. ONNX Runtime backend disabled.")
		else()
			find_library(ORT_LIBRARY onnxruntime PATHS "${ORT_LIB}" NO_DEFAULT_PATH)
			if(NOT ORT_LIBRARY)
				file(GLOB _ORT_SO "${ORT_LIB}/libonnxruntime.so*")
				if(_ORT_SO)
					list(GET _ORT_SO 0 ORT_LIBRARY)
				endif()
			endif()
			if(ORT_LIBRARY)
				set(ORT_ENABLED ON)
				message(STATUS "ONNX Runtime backend enabled: ${ORT_LIBRARY}")
			else()
				message(WARNING "libonnxruntime.so not found in ${ORT_LIB}. ONNX Runtime backend disabled.")
			endif()
		endif()
	else()
		message(STATUS "ONNX Runtime not found. Set -DONNXRUNTIME_ROOT=/path/to/ort")
	endif()
endif()

# ---- TensorRT detection ----
set(TRT_ENABLED OFF)
if(ENABLE_YOLO AND ENABLE_TENSORRT)
	find_path(TENSORRT_INCLUDE_DIR NvInfer.h
		PATHS
			/usr/include/aarch64-linux-gnu
			/usr/include
	)

	find_library(TENSORRT_NVINFER nvinfer
		PATHS
			/usr/lib/aarch64-linux-gnu
	)

	find_library(TENSORRT_PLUGIN nvinfer_plugin
		PATHS
			/usr/lib/aarch64-linux-gnu
	)

	find_library(CUDART_LIBRARY cudart
		PATHS
			/usr/local/cuda/lib64
			/usr/lib/aarch64-linux-gnu
	)

	if(TENSORRT_INCLUDE_DIR AND TENSORRT_NVINFER AND CUDART_LIBRARY)
		set(TRT_ENABLED ON)
		message(STATUS "TensorRT backend enabled")
		message(STATUS "  TensorRT include: ${TENSORRT_INCLUDE_DIR}")
		message(STATUS "  TensorRT nvinfer: ${TENSORRT_NVINFER}")
		message(STATUS "  CUDA runtime:    ${CUDART_LIBRARY}")
	else()
		message(WARNING "TensorRT/CUDA not found. TensorRT backend disabled.")
	endif()
endif()

# Disable YOLO entirely if no backend is available
if(ENABLE_YOLO AND NOT ORT_ENABLED AND NOT TRT_ENABLED)
	set(ENABLE_YOLO OFF)
	message(STATUS "No YOLO backend available. YOLO disabled.")
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

# ---- Link ONNX Runtime ----
if(ORT_ENABLED)
	target_compile_definitions(opencv_cli PRIVATE YOLO_USE_ONNXRUNTIME)
	target_include_directories(opencv_cli PRIVATE "${ORT_INC}")
	target_link_libraries(opencv_cli PRIVATE "${ORT_LIBRARY}")
endif()

# ---- Link TensorRT / CUDA ----
if(TRT_ENABLED)
	target_compile_definitions(opencv_cli PRIVATE YOLO_USE_TENSORRT)
	target_include_directories(opencv_cli PRIVATE
		"${TENSORRT_INCLUDE_DIR}"
		/usr/local/cuda/include
	)
	target_link_libraries(opencv_cli PRIVATE
		"${TENSORRT_NVINFER}"
		"${CUDART_LIBRARY}"
	)
	if(TENSORRT_PLUGIN)
		target_link_libraries(opencv_cli PRIVATE "${TENSORRT_PLUGIN}")
	endif()
endif()

# ---- Copy web/ to output dir ----
add_custom_command(TARGET opencv_cli POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
	"${CMAKE_SOURCE_DIR}/web" "$<TARGET_FILE_DIR:opencv_cli>/web"
)
