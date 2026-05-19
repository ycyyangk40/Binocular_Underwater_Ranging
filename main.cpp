#include <cstdlib>
#include <iostream>
#include <string>

#include <opencv2/core.hpp>

#include "src/mode_calib.hpp"
#include "src/mode_depth.hpp"
#include "src/mode_rectify.hpp"
#include "src/mode_yolo.hpp"
#include "src/mode_video.hpp"

int main(int argc, char** argv) {
	int cameraDeviceNo = 0;
	bool useBinaryThreshold = false;
	bool fastMode = false;
	bool showGray = false;
	bool enhance = true;
	bool showLeft = true;
	bool showRight = true;
	bool showDisp = true;
	bool showDepth = true;
	bool hasShowFlags = false;
	std::string view = "both";
	std::string mode = "depth";
	std::string modelPath = "models/yolov8l.onnx";
	int boardHeight = 6;
	int boardWidth = 9;
	float squareSize = 2.97f;

	int argi = 1;
	if (argi < argc && argv[argi][0] != '-') {
		cameraDeviceNo = std::atoi(argv[argi]);
		++argi;
	}
	if (argi < argc && argv[argi][0] != '-') {
		mode = argv[argi];
		++argi;
	}

	for (int i = argi; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "--binary") {
			useBinaryThreshold = true;
		} else if (arg == "--fast") {
			fastMode = true;
		} else if (arg == "--gray") {
			showGray = true;
		} else if (arg == "--no-enhance") {
			enhance = false;
		} else if (arg == "--show-left") {
			showLeft = true;
			hasShowFlags = true;
		} else if (arg == "--hide-left") {
			showLeft = false;
			hasShowFlags = true;
		} else if (arg == "--show-right") {
			showRight = true;
			hasShowFlags = true;
		} else if (arg == "--hide-right") {
			showRight = false;
			hasShowFlags = true;
		} else if (arg == "--show-disp") {
			showDisp = true;
			hasShowFlags = true;
		} else if (arg == "--hide-disp") {
			showDisp = false;
			hasShowFlags = true;
		} else if (arg == "--show-depth") {
			showDepth = true;
			hasShowFlags = true;
		} else if (arg == "--hide-depth") {
			showDepth = false;
			hasShowFlags = true;
		} else if (arg == "--view" && i + 1 < argc) {
			view = argv[++i];
		} else if (arg == "--mode" && i + 1 < argc) {
			mode = argv[++i];
		} else if (arg == "--model" && i + 1 < argc) {
			modelPath = argv[++i];
		} else if (arg == "--board-height" && i + 1 < argc) {
			boardHeight = std::atoi(argv[++i]);
		} else if (arg == "--board-width" && i + 1 < argc) {
			boardWidth = std::atoi(argv[++i]);
		} else if (arg == "--square-size" && i + 1 < argc) {
			squareSize = static_cast<float>(std::atof(argv[++i]));
		}
	}

	if (mode == "binary") {
		useBinaryThreshold = true;
		mode = "video";
	}
	if (!hasShowFlags && view != "both") {
		showLeft = view != "right";
		showRight = view != "left";
	}

	std::cout << "Mode: " << (mode.empty() ? "(empty)" : mode) << "\n";
	std::cout.setf(std::ios::unitbuf);
	std::cerr.setf(std::ios::unitbuf);

	try {
		if (mode == "calib") {
			InteractiveCalib(cv::Size(boardWidth, boardHeight), squareSize);
		} else if (mode == "rectify") {
			stereoRectifyMode(cameraDeviceNo);
		} else if (mode == "depth") {
			stereoDepthMode(cameraDeviceNo, fastMode, showGray, enhance, showLeft, showRight, showDisp, showDepth);
		} else if (mode == "yolo") {
			yoloStereoMode(cameraDeviceNo, fastMode, showGray, enhance, showLeft, showRight, showDepth, modelPath);
		} else {
			videoCaptureTest(cameraDeviceNo, useBinaryThreshold, showLeft, showRight, showGray, enhance);
		}
	} catch (const cv::Exception& ex) {
		std::cerr << "OpenCV error: " << ex.what() << std::endl;
		return 1;
	} catch (const std::exception& ex) {
		std::cerr << "Error: " << ex.what() << std::endl;
		return 1;
	}

	return 0;
}