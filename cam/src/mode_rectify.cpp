#include "mode_rectify.hpp"

#include "common.hpp"

#include <opencv2/calib3d.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

void stereoRectifyMode(int cameraDeviceNo) {
	cv::Mat cameraMatrixLeft, distCoeffsLeft;
	cv::Mat cameraMatrixRight, distCoeffsRight;
	cv::Mat R, T;
	if (!loadIntrinsics("data/left_intrinsics.yml", cameraMatrixLeft, distCoeffsLeft)) {
		return;
	}
	if (!loadIntrinsics("data/right_intrinsics.yml", cameraMatrixRight, distCoeffsRight)) {
		return;
	}
	if (!loadStereoRT("data/stereo.yml", R, T)) {
		return;
	}

	cv::VideoCapture capture;
	if (!openStereoCamera(capture, cameraDeviceNo)) return;

	cv::namedWindow("left", cv::WINDOW_GUI_EXPANDED);
	cv::namedWindow("right", cv::WINDOW_GUI_EXPANDED);
	cv::resizeWindow("left", 1280, 720);
	cv::resizeWindow("right", 1280, 720);

	bool mapsReady = false;
	cv::Rect roi1, roi2;
	cv::Mat mapLx, mapLy, mapRx, mapRy;
	while (true) {
		cv::Mat frame;
		if (!capture.read(frame)) {
			continue;
		}

		cv::Size combinedImageSize = frame.size();
		auto leftImage = frame(cv::Rect(0, 0, combinedImageSize.width / 2, combinedImageSize.height));
		auto rightImage = frame(cv::Rect(combinedImageSize.width / 2, 0, combinedImageSize.width / 2, combinedImageSize.height));
		cv::Mat leftEnhanced = enhanceUnderwaterImage(leftImage);
		cv::Mat rightEnhanced = enhanceUnderwaterImage(rightImage);

		cv::Mat leftGray, rightGray;
		cv::cvtColor(leftEnhanced, leftGray, cv::COLOR_BGR2GRAY);
		cv::cvtColor(rightEnhanced, rightGray, cv::COLOR_BGR2GRAY);

		if (!mapsReady) {
			cv::Mat R1, R2, P1, P2, Q;
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
			mapsReady = true;
		}

		cv::Mat leftRectify, rightRectify;
		cv::remap(leftGray, leftRectify, mapLx, mapLy, cv::INTER_LINEAR);
		cv::remap(rightGray, rightRectify, mapRx, mapRy, cv::INTER_LINEAR);

		cv::imshow("left", leftRectify);
		cv::imshow("right", rightRectify);

		auto res = cv::waitKey(1);
		if (res == 'Q' || res == 'q') {
			capture.release();
			return;
		}
	}
}
