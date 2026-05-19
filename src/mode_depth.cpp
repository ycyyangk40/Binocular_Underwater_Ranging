#include "mode_depth.hpp"

#include "common.hpp"

#include <opencv2/calib3d.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
struct DepthClickContext {
	cv::Mat disp32f;
	cv::Mat validMask;
	double baselineMeters = 0.0;
	double fx = 0.0;
	int sampleRadius = 2;
};

void onDepthClick(int event, int x, int y, int, void* userdata) {
	if (event != cv::EVENT_LBUTTONDOWN || userdata == nullptr) {
		return;
	}
	DepthClickContext* ctx = static_cast<DepthClickContext*>(userdata);
	if (ctx->disp32f.empty()) {
		std::cout << "Depth data not ready.\n";
		return;
	}
	if (x < 0 || y < 0 || x >= ctx->disp32f.cols || y >= ctx->disp32f.rows) {
		return;
	}

	int x0 = std::max(0, x - ctx->sampleRadius);
	int x1 = std::min(ctx->disp32f.cols - 1, x + ctx->sampleRadius);
	int y0 = std::max(0, y - ctx->sampleRadius);
	int y1 = std::min(ctx->disp32f.rows - 1, y + ctx->sampleRadius);

	std::vector<float> dispSamples;
	dispSamples.reserve((x1 - x0 + 1) * (y1 - y0 + 1));
	int validCount = 0;
	for (int yy = y0; yy <= y1; ++yy) {
		for (int xx = x0; xx <= x1; ++xx) {
			if (!ctx->validMask.empty() && ctx->validMask.at<uchar>(yy, xx) == 0) {
				continue;
			}
			float d = ctx->disp32f.at<float>(yy, xx);
			if (std::isfinite(d) && d > 1e-4f) {
				dispSamples.push_back(d);
				++validCount;
			}
		}
	}

	int totalCount = (x1 - x0 + 1) * (y1 - y0 + 1);
	if (dispSamples.size() < 8) {
		std::cout << "Invalid point at (" << x << ", " << y << "): disparity invalid, valid "
			<< validCount << "/" << totalCount << " in window.\n";
		return;
	}

	size_t mid = dispSamples.size() / 2;
	std::nth_element(dispSamples.begin(), dispSamples.begin() + mid, dispSamples.end());
	float dispMedian = dispSamples[mid];

	double sum = 0.0;
	for (float d : dispSamples) {
		sum += d;
	}
	double dispMean = sum / static_cast<double>(dispSamples.size());
	double var = 0.0;
	for (float d : dispSamples) {
		double diff = static_cast<double>(d) - dispMean;
		var += diff * diff;
	}
	double dispStd = std::sqrt(var / static_cast<double>(dispSamples.size()));

	double depthMeters = (ctx->baselineMeters * ctx->fx) / static_cast<double>(dispMedian);
	double validRatio = static_cast<double>(validCount) / static_cast<double>(totalCount);
	std::cout << "Point(" << x << ", " << y << ") disp_median=" << dispMedian
		<< " px, distance=" << depthMeters << " m (" << depthMeters * 100.0 << " cm), "
		<< "valid=" << validCount << "/" << totalCount << " (" << validRatio * 100.0 << "%), "
		<< "disp_std=" << dispStd << " px\n";
}
}

void stereoDepthMode(int cameraDeviceNo, bool fastMode, bool showGray, bool enhance, bool showLeft, bool showRight, bool showDisp, bool showDepth) {
	cv::Mat cameraMatrixLeft, distCoeffsLeft;
	cv::Mat cameraMatrixRight, distCoeffsRight;
	cv::Mat R, T;
	float calibSquareSize = 0.0f;
	if (!loadIntrinsics("data/left_intrinsics.yml", cameraMatrixLeft, distCoeffsLeft)) {
		return;
	}
	if (!loadIntrinsics("data/right_intrinsics.yml", cameraMatrixRight, distCoeffsRight)) {
		return;
	}
	if (!loadStereoRT("data/stereo.yml", R, T, &calibSquareSize)) {
		return;
	}

	cv::VideoCapture capture;
	capture.open(cameraDeviceNo);
	capture.set(cv::CAP_PROP_FRAME_WIDTH, 2560);
	capture.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
	capture.set(cv::CAP_PROP_FPS, 30);
	capture.set(cv::CAP_PROP_FOURCC, cv::CAP_OPENCV_MJPEG);

	bool showLR = showLeft || showRight;

	if (showLeft) {
		cv::namedWindow("left", cv::WINDOW_GUI_EXPANDED);
		cv::resizeWindow("left", 1280, 720);
	}
	if (showRight) {
		cv::namedWindow("right", cv::WINDOW_GUI_EXPANDED);
		cv::resizeWindow("right", 1280, 720);
	}
	if (showDisp) {
		cv::namedWindow("disp", cv::WINDOW_GUI_EXPANDED);
		cv::resizeWindow("disp", 1280, 720);
	}
	if (showDepth) {
		cv::namedWindow("depth", cv::WINDOW_GUI_EXPANDED);
		cv::resizeWindow("depth", 1280, 720);
	}

	double unitToMeter = 0.01;
	if (calibSquareSize > 0.0f && calibSquareSize < 0.5f) {
		unitToMeter = 1.0;
	} else if (calibSquareSize >= 10.0f) {
		unitToMeter = 0.001;
	}
	double baselineMeters = cv::norm(T) * unitToMeter;
	double fx = cameraMatrixLeft.at<double>(0, 0);
	std::cout << "Stereo baseline=" << baselineMeters << " m, fx=" << fx << " px\n";

	bool mapsReady = false;
	cv::Rect roi1, roi2;
	cv::Mat mapLx, mapLy, mapRx, mapRy;
	cv::Mat Q;

	const float depthVisMinMeters = 0.08f;
	const float depthVisMaxMeters = 3.0f;
	const float temporalAlpha = 0.2f;
	bool hasFilteredDisp = false;
	cv::Mat disp32fFiltered;
	DepthClickContext clickCtx;
	clickCtx.baselineMeters = baselineMeters;
	clickCtx.fx = fx;
	if (showDepth) {
		cv::setMouseCallback("depth", onDepthClick, &clickCtx);
	}

	auto stereoBM = cv::StereoBM::create(256, 9);
	stereoBM->setMinDisparity(0);
	stereoBM->setNumDisparities(fastMode ? 128 : 256);
	stereoBM->setBlockSize(fastMode ? 7 : 9);
	stereoBM->setPreFilterType(cv::StereoBM::PREFILTER_XSOBEL);
	stereoBM->setPreFilterSize(5);
	stereoBM->setPreFilterCap(31);
	stereoBM->setTextureThreshold(10);
	stereoBM->setUniquenessRatio(10);
	stereoBM->setSpeckleWindowSize(fastMode ? 50 : 100);
	stereoBM->setSpeckleRange(32);
	stereoBM->setDisp12MaxDiff(1);

	while (true) {
		cv::Mat frame;
		if (!capture.read(frame)) {
			continue;
		}

		cv::Size combinedImageSize = frame.size();
		auto leftImage = frame(cv::Rect(0, 0, combinedImageSize.width / 2, combinedImageSize.height));
		auto rightImage = frame(cv::Rect(combinedImageSize.width / 2, 0, combinedImageSize.width / 2, combinedImageSize.height));
		bool doEnhance = enhance && !fastMode;
		cv::Mat leftBase = doEnhance ? enhanceUnderwaterImage(leftImage) : leftImage;
		cv::Mat rightBase = doEnhance ? enhanceUnderwaterImage(rightImage) : rightImage;

		cv::Mat leftGray, rightGray;
		cv::cvtColor(leftBase, leftGray, cv::COLOR_BGR2GRAY);
		cv::cvtColor(rightBase, rightGray, cv::COLOR_BGR2GRAY);

		if (!mapsReady) {
			cv::Mat R1, R2, P1, P2;
			cv::Size imageSize = leftGray.size();
			cv::stereoRectify(
				cameraMatrixLeft, distCoeffsLeft,
				cameraMatrixRight, distCoeffsRight,
				imageSize, R, T,
				R1, R2, P1, P2, Q,
				cv::CALIB_ZERO_DISPARITY,
				1, imageSize, &roi1, &roi2);
			cv::initUndistortRectifyMap(cameraMatrixLeft, distCoeffsLeft, R1, P1, imageSize, CV_16SC2, mapLx, mapLy);
			cv::initUndistortRectifyMap(cameraMatrixRight, distCoeffsRight, R2, P2, imageSize, CV_16SC2, mapRx, mapRy);
			stereoBM->setROI1(roi1);
			stereoBM->setROI2(roi2);
			mapsReady = true;
		}

		cv::Mat leftRectify, rightRectify;
		cv::remap(leftGray, leftRectify, mapLx, mapLy, cv::INTER_LINEAR);
		cv::remap(rightGray, rightRectify, mapRx, mapRy, cv::INTER_LINEAR);

		cv::Mat leftDisplay, rightDisplay;
		if (showLeft || showRight) {
			if (showGray) {
				leftDisplay = leftRectify;
				rightDisplay = rightRectify;
			} else {
				if (showLeft) {
					cv::remap(leftBase, leftDisplay, mapLx, mapLy, cv::INTER_LINEAR);
				}
				if (showRight) {
					cv::remap(rightBase, rightDisplay, mapRx, mapRy, cv::INTER_LINEAR);
				}
			}
		}

		cv::Mat disp16, disp32f;
		stereoBM->compute(leftRectify, rightRectify, disp16);
		disp16.convertTo(disp32f, CV_32F, 1.0 / 16.0);
		cv::Mat validMaskRaw = disp32f > 0;
		if (!hasFilteredDisp) {
			disp32fFiltered = disp32f.clone();
			hasFilteredDisp = true;
		} else {
			cv::addWeighted(disp32fFiltered, 1.0 - temporalAlpha, disp32f, temporalAlpha, 0.0, disp32fFiltered);
		}

		cv::Mat dispVis;
		cv::Mat validMask = validMaskRaw.clone();
		const float dispVisMax = static_cast<float>(stereoBM->getNumDisparities());
		disp32fFiltered.convertTo(dispVis, CV_8U, 255.0 / dispVisMax);
		dispVis.setTo(0, ~validMask);

		cv::Mat depthVis;
		if (showDepth) {
			cv::Mat points3d;
			cv::reprojectImageTo3D(disp32fFiltered, points3d, Q, true);
			std::vector<cv::Mat> channels;
			cv::split(points3d, channels);
			cv::Mat depth = channels[2];
			cv::Mat depthMeters = cv::abs(depth) * unitToMeter;
			clickCtx.disp32f = disp32f;
			clickCtx.validMask = validMaskRaw;

			cv::Mat finiteMask = (depthMeters == depthMeters);
			cv::Mat depthRangeMask = (depthMeters >= depthVisMinMeters) & (depthMeters <= depthVisMaxMeters);
			cv::Mat depthMask = validMask & finiteMask & depthRangeMask;

			cv::Mat depthNorm;
			depthMeters.convertTo(
				depthNorm, CV_32F,
				1.0 / (depthVisMaxMeters - depthVisMinMeters),
				-depthVisMinMeters / (depthVisMaxMeters - depthVisMinMeters));
			cv::min(depthNorm, 1.0, depthNorm);
			cv::max(depthNorm, 0.0, depthNorm);
			depthNorm.setTo(0, ~depthMask);

			cv::Mat depthVisGray;
			depthNorm.convertTo(depthVisGray, CV_8U, 255.0);
			cv::applyColorMap(255 - depthVisGray, depthVis, cv::COLORMAP_TURBO);
			depthVis.setTo(cv::Scalar(0, 0, 0), ~depthMask);
		}

		if (showLeft && !leftDisplay.empty()) {
			cv::imshow("left", leftDisplay);
		}
		if (showRight && !rightDisplay.empty()) {
			cv::imshow("right", rightDisplay);
		}
		if (showDisp) {
			cv::imshow("disp", dispVis);
		}
		if (showDepth) {
			cv::imshow("depth", depthVis);
		}

		auto res = cv::waitKey(1);
		if (res == 'Q' || res == 'q') {
			capture.release();
			return;
		}
	}
}
