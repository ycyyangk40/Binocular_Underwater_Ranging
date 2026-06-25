#include "mode_calib.hpp"

#include "common.hpp"

#include <opencv2/calib3d.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <filesystem>
#include <iostream>

void InteractiveCalib(cv::Size boardSize, float squareSize) {
	cv::VideoCapture capture;
	if (!openStereoCamera(capture, 0)) return;

	cv::namedWindow("left", cv::WINDOW_GUI_EXPANDED);
	cv::namedWindow("right", cv::WINDOW_GUI_EXPANDED);
	cv::namedWindow("left_valid", cv::WINDOW_GUI_EXPANDED);
	cv::namedWindow("right_valid", cv::WINDOW_GUI_EXPANDED);
	cv::resizeWindow("left", 1280, 720);
	cv::resizeWindow("right", 1280, 720);
	cv::resizeWindow("left_valid", 1280, 720);
	cv::resizeWindow("right_valid", 1280, 720);

	CameraCalibration leftCali(boardSize.height, boardSize.width, squareSize);
	CameraCalibration rightCali(boardSize.height, boardSize.width, squareSize);
	int captureTime = 0;
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

		cv::imshow("left", leftGray);
		cv::imshow("right", rightGray);
		auto res = cv::waitKey(1);
		if (res == 'C' || res == 'c') {
			auto resultLeft = leftCali.findCorners(leftGray);
			auto resultRight = rightCali.findCorners(rightGray);
			if (std::get<0>(resultLeft) && std::get<0>(resultRight)) {
				std::cout << "Valid frame captured! Captured [ " << ++captureTime << " ]\n";
				auto resImageLeft = leftCali.appendCorners(leftGray, std::get<1>(resultLeft));
				auto resImageRight = rightCali.appendCorners(rightGray, std::get<1>(resultRight));
				cv::imshow("left_valid", resImageLeft);
				cv::imshow("right_valid", resImageRight);
				cv::waitKey(1);
			} else {
				std::cout << "Invalid frame.\n";
				std::cout << "Capture status: left -> " << std::get<0>(resultLeft)
					<< " , right -> " << std::get<0>(resultRight) << " .\n";
				cv::drawChessboardCorners(leftGray, boardSize, std::get<1>(resultLeft), std::get<0>(resultLeft));
				cv::imshow("left_valid", leftGray);
				cv::drawChessboardCorners(rightGray, boardSize, std::get<1>(resultRight), std::get<0>(resultRight));
				cv::imshow("right_valid", rightGray);
				cv::waitKey(1);
			}
		} else if (res == 'Q' || res == 'q') {
			cv::destroyAllWindows();
			capture.release();
			return;
		} else if (res == 'E' || res == 'e') {
			auto leftResult = leftCali.calculate();
			auto rightResult = rightCali.calculate();
			bool leftOk = std::get<0>(leftResult);
			bool rightOk = std::get<0>(rightResult);
			if (!leftOk || !rightOk) {
				std::cout << "Calibration failed: left=" << leftOk << " right=" << rightOk << "\n";
				continue;
			}
			std::filesystem::create_directories("data");
			{
				cv::FileStorage fs("data/left_intrinsics.yml", cv::FileStorage::WRITE);
				fs << "camera_matrix" << std::get<1>(leftResult);
				fs << "dist_coeffs" << std::get<2>(leftResult);
				fs << "rms" << std::get<5>(leftResult);
				fs << "board_width" << boardSize.width;
				fs << "board_height" << boardSize.height;
				fs << "square_size" << squareSize;
				fs.release();
			}
			{
				cv::FileStorage fs("data/right_intrinsics.yml", cv::FileStorage::WRITE);
				fs << "camera_matrix" << std::get<1>(rightResult);
				fs << "dist_coeffs" << std::get<2>(rightResult);
				fs << "rms" << std::get<5>(rightResult);
				fs << "board_width" << boardSize.width;
				fs << "board_height" << boardSize.height;
				fs << "square_size" << squareSize;
				fs.release();
			}

			cv::Mat R, T, E, F;
			cv::Size imageSize = leftGray.size();
			double stereoRms = cv::stereoCalibrate(
				std::get<7>(leftResult),
				std::get<6>(leftResult),
				std::get<6>(rightResult),
				std::get<1>(leftResult),
				std::get<2>(leftResult),
				std::get<1>(rightResult),
				std::get<2>(rightResult),
				imageSize,
				R, T, E, F,
				cv::CALIB_USE_INTRINSIC_GUESS,
				cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 30, 1e-6));

			{
				cv::FileStorage fs("data/stereo.yml", cv::FileStorage::WRITE);
				fs << "R" << R;
				fs << "T" << T;
				fs << "E" << E;
				fs << "F" << F;
				fs << "rms" << stereoRms;
				fs << "board_width" << boardSize.width;
				fs << "board_height" << boardSize.height;
				fs << "square_size" << squareSize;
				fs.release();
			}

			std::cout << "Calibration saved to data/*.yml\n";
		}
	}
}
