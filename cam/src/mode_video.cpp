#include "mode_video.hpp"

#include "common.hpp"

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <cstdlib>

void videoCaptureTest(int cameraDeviceNo, bool useBinaryThreshold, bool showLeft, bool showRight, bool showGray, bool enhance) {
#ifdef _WIN32
	auto res = _putenv("OPENCV_VIDEOIO_MSMF_ENABLE_HW_TRANSFORMS=0");
	(void)res;
#endif

	cv::VideoCapture capture;
	if (!openStereoCamera(capture, cameraDeviceNo)) return;

	if (showLeft) {
		cv::namedWindow("left", cv::WINDOW_GUI_EXPANDED);
		cv::resizeWindow("left", 1280, 720);
	}
	if (showRight) {
		cv::namedWindow("right", cv::WINDOW_GUI_EXPANDED);
		cv::resizeWindow("right", 1280, 720);
	}

	cv::Mat frame;
	cv::Size combinedImageSize;
	while (true) {
		auto result = capture.read(frame);
		combinedImageSize = frame.size();
		if (result) {
			auto leftImage = frame(cv::Rect(0, 0, combinedImageSize.width / 2, combinedImageSize.height));
			auto rightImage = frame(cv::Rect(combinedImageSize.width / 2, 0, combinedImageSize.width / 2, combinedImageSize.height));
			cv::Mat leftBase = enhance ? enhanceUnderwaterImage(leftImage) : leftImage;
			cv::Mat rightBase = enhance ? enhanceUnderwaterImage(rightImage) : rightImage;

			cv::Mat leftDisplay = leftBase;
			cv::Mat rightDisplay = rightBase;
			if (showGray || useBinaryThreshold) {
				cv::cvtColor(leftBase, leftDisplay, cv::COLOR_BGR2GRAY);
				cv::cvtColor(rightBase, rightDisplay, cv::COLOR_BGR2GRAY);
				if (useBinaryThreshold) {
					cv::threshold(leftDisplay, leftDisplay, 127, 255, cv::THRESH_BINARY);
					cv::threshold(rightDisplay, rightDisplay, 127, 255, cv::THRESH_BINARY);
				}
			}

			if (showLeft && !leftDisplay.empty()) {
				cv::imshow("left", leftDisplay);
			}
			if (showRight && !rightDisplay.empty()) {
				cv::imshow("right", rightDisplay);
			}

			auto res = cv::waitKey(1);
			if (res == 'Q' || res == 'q') {
				capture.release();
				return;
			}
		}
	}
}
