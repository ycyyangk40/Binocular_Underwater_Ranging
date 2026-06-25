cmake_minimum_required(VERSION 3.20)

project(CppOpenCVWebView LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_YOLO "Enable YOLO/ONNX Runtime integration" ON)
set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/onnxruntime-win-x64-gpu-1.24.4/onnxruntime-win-x64-gpu-1.24.4" CACHE PATH "ONNX Runtime root directory")

if(ONNXRUNTIME_ROOT STREQUAL "")
	set(_LOCAL_ORT_ROOT "${CMAKE_SOURCE_DIR}/onnxruntime-win-x64-gpu-1.24.4/onnxruntime-win-x64-gpu-1.24.4")
	if(EXISTS "${_LOCAL_ORT_ROOT}/include/onnxruntime_cxx_api.h" AND EXISTS "${_LOCAL_ORT_ROOT}/lib/onnxruntime.lib")
		set(ONNXRUNTIME_ROOT "${_LOCAL_ORT_ROOT}" CACHE PATH "ONNX Runtime root directory" FORCE)
		set(ENABLE_YOLO ON CACHE BOOL "Enable YOLO/ONNX Runtime integration" FORCE)
	endif()
endif()

set(OpenCV_DIR "D:/OpenCV/opencv/build" CACHE PATH "OpenCV build directory")
find_package(OpenCV REQUIRED)

set(OPENCV_RUNTIME_DIR "${OpenCV_DIR}/x64/vc16/bin" CACHE PATH "OpenCV runtime bin directory")
set(OPENCV_RUNTIME_DLL_RELEASE "${OPENCV_RUNTIME_DIR}/opencv_world4120.dll")
set(OPENCV_RUNTIME_DLL_DEBUG "${OPENCV_RUNTIME_DIR}/opencv_world4120d.dll")

function(add_opencv_runtime_copy target)
	add_custom_command(TARGET ${target} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"$<IF:$<CONFIG:Debug>,${OPENCV_RUNTIME_DLL_DEBUG},${OPENCV_RUNTIME_DLL_RELEASE}>"
		"$<TARGET_FILE_DIR:${target}>"
	)
endfunction()

function(add_onnxruntime_runtime_copy target)
	set(ORT_DLL "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll")
	set(ORT_SHARED_DLL "${ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_shared.dll")
	set(ORT_CUDA_DLL "${ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_cuda.dll")
	set(ORT_TRT_DLL "${ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_tensorrt.dll")
	if(EXISTS "${ORT_DLL}")
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_DLL}" "$<TARGET_FILE_DIR:${target}>"
		)
	endif()
	if(EXISTS "${ORT_SHARED_DLL}")
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_SHARED_DLL}" "$<TARGET_FILE_DIR:${target}>"
		)
	endif()
	if(EXISTS "${ORT_CUDA_DLL}")
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_CUDA_DLL}" "$<TARGET_FILE_DIR:${target}>"
		)
	endif()
	if(EXISTS "${ORT_TRT_DLL}")
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_TRT_DLL}" "$<TARGET_FILE_DIR:${target}>"
		)
	endif()
endfunction()

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
	set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_ROOT}/include")
	set(ONNXRUNTIME_LIB_DIR "${ONNXRUNTIME_ROOT}/lib")
	if(NOT EXISTS "${ONNXRUNTIME_INCLUDE_DIR}/onnxruntime_cxx_api.h" OR NOT EXISTS "${ONNXRUNTIME_LIB_DIR}/onnxruntime.lib")
		message(FATAL_ERROR "ONNX Runtime headers or libs not found under ${ONNXRUNTIME_ROOT}. Update ONNXRUNTIME_ROOT or disable ENABLE_YOLO.")
	endif()
		
target_compile_definitions(opencv_cli PRIVATE YOLO_USE_ONNXRUNTIME)
	target_include_directories(opencv_cli PRIVATE "${ONNXRUNTIME_INCLUDE_DIR}")
	target_link_directories(opencv_cli PRIVATE "${ONNXRUNTIME_LIB_DIR}")
	target_link_libraries(opencv_cli PRIVATE onnxruntime)
	add_onnxruntime_runtime_copy(opencv_cli)
	message(STATUS "YOLO support enabled with ONNX Runtime at ${ONNXRUNTIME_ROOT}")
endif()

add_opencv_runtime_copy(opencv_cli)

set(WEBVIEW2_VERSION "1.0.2210.55")
set(WEBVIEW2_NUGET_URL "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${WEBVIEW2_VERSION}")
set(WEBVIEW2_NUGET_DIR "${CMAKE_BINARY_DIR}/_deps/webview2")
set(WEBVIEW2_NUPKG "${WEBVIEW2_NUGET_DIR}/webview2.nupkg")
set(WEBVIEW2_SDK_DIR "${WEBVIEW2_NUGET_DIR}/sdk")
set(WEBVIEW2_INCLUDE_DIR "${WEBVIEW2_SDK_DIR}/build/native/include")
set(WEBVIEW2_LIB_DIR "${WEBVIEW2_SDK_DIR}/build/native/x64")

if(NOT EXISTS "${WEBVIEW2_INCLUDE_DIR}/WebView2.h")
	file(MAKE_DIRECTORY "${WEBVIEW2_NUGET_DIR}")
	if(NOT EXISTS "${WEBVIEW2_NUPKG}")
		file(DOWNLOAD "${WEBVIEW2_NUGET_URL}" "${WEBVIEW2_NUPKG}" SHOW_PROGRESS)
	endif()
	file(MAKE_DIRECTORY "${WEBVIEW2_SDK_DIR}")
	execute_process(
		COMMAND "${CMAKE_COMMAND}" -E tar xvf "${WEBVIEW2_NUPKG}"
		WORKING_DIRECTORY "${WEBVIEW2_SDK_DIR}"
		RESULT_VARIABLE expand_result
	)
	if(NOT expand_result EQUAL 0)
		message(FATAL_ERROR "Failed to extract WebView2 NuGet package.")
	endif()
endif()

add_executable(opencv_ui WIN32
	src/webview_main.cpp
	src/webview_app.cpp
)

target_include_directories(opencv_ui PRIVATE src "${WEBVIEW2_INCLUDE_DIR}")

target_link_libraries(opencv_ui PRIVATE
	"${WEBVIEW2_LIB_DIR}/WebView2LoaderStatic.lib"
	user32
	ole32
	oleaut32
	shlwapi
)

target_compile_definitions(opencv_ui PRIVATE UNICODE _UNICODE)

target_link_libraries(opencv_ui PRIVATE ${OpenCV_LIBS})

target_include_directories(opencv_ui PRIVATE ${OpenCV_INCLUDE_DIRS})

if(ENABLE_YOLO AND ONNXRUNTIME_ROOT)
	set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_ROOT}/include")
	set(ONNXRUNTIME_LIB_DIR "${ONNXRUNTIME_ROOT}/lib")
	file(GLOB ONNXRUNTIME_RUNTIME_DLLS "${ONNXRUNTIME_LIB_DIR}/onnxruntime*.dll")
	foreach(_dll IN LISTS ONNXRUNTIME_RUNTIME_DLLS)
		add_custom_command(TARGET opencv_cli POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:opencv_cli>"
		)
		add_custom_command(TARGET opencv_ui POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:opencv_ui>"
		)
	endforeach()
endif()

add_opencv_runtime_copy(opencv_ui)

add_custom_command(TARGET opencv_ui POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
	"${CMAKE_SOURCE_DIR}/web" "$<TARGET_FILE_DIR:opencv_ui>/web"
)
