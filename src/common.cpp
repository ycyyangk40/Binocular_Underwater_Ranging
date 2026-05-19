#include "common.hpp"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <iostream>

CameraCalibration::CameraCalibration(int chessBoardHeight, int chessBoardWidth, float squareSize)
	: squareSize(squareSize) {
	boardSize.height = chessBoardHeight;
	boardSize.width = chessBoardWidth;
}

std::tuple<bool, std::vector<cv::Point2f>> CameraCalibration::findCorners(cv::Mat frame) {
	std::vector<cv::Point2f> corners;
	bool result = cv::findChessboardCorners(
		frame, boardSize, corners,
		cv::CALIB_CB_ADAPTIVE_THRESH /*| cv::CALIB_CB_FAST_CHECK */
		| cv::CALIB_CB_NORMALIZE_IMAGE | cv::CALIB_CB_FILTER_QUADS);
	return std::make_tuple(result, corners);
}

cv::Mat CameraCalibration::appendCorners(cv::Mat frame, std::vector<cv::Point2f> corners) {
	imageSize = frame.size();
	cv::cornerSubPix(
		frame, corners, cv::Size(11, 11), cv::Size(-1, -1),
		cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 30, 0.001));
	cv::drawChessboardCorners(frame, boardSize, corners, true);
	imagePoints.push_back(corners);
	return frame;
}

std::tuple<bool, cv::Mat, cv::Mat, cv::Mat, cv::Mat, double, ImagePoints, ObjectPoints>
CameraCalibration::calculate() {
	cv::Mat cameraMatrix, distCoeffs, rvecs, tvecs;
	std::vector<cv::Point3f> obj;
	for (int k = 0; k < boardSize.height; k++) {
		for (int l = 0; l < boardSize.width; l++) {
			obj.emplace_back(cv::Point3f(l * squareSize, k * squareSize, 0));
		}
	}
	objectPoints.push_back(obj);
	objectPoints.resize(imagePoints.size(), objectPoints[0]);
	double rms = cv::calibrateCamera(
		objectPoints, imagePoints, imageSize, cameraMatrix, distCoeffs, rvecs, tvecs);
	bool result = cv::checkRange(cameraMatrix) && cv::checkRange(distCoeffs);
	return std::make_tuple(result, cameraMatrix, distCoeffs, rvecs, tvecs, rms, imagePoints, objectPoints);
}

bool loadIntrinsics(const std::string& path, cv::Mat& cameraMatrix, cv::Mat& distCoeffs) {
	cv::FileStorage fs(path, cv::FileStorage::READ);
	if (!fs.isOpened()) {
		std::cerr << "Failed to open: " << path << "\n";
		return false;
	}
	fs["camera_matrix"] >> cameraMatrix;
	fs["dist_coeffs"] >> distCoeffs;
	fs.release();
	return !cameraMatrix.empty() && !distCoeffs.empty();
}

bool loadStereoRT(const std::string& path, cv::Mat& R, cv::Mat& T, float* squareSize) {
	cv::FileStorage fs(path, cv::FileStorage::READ);
	if (!fs.isOpened()) {
		std::cerr << "Failed to open: " << path << "\n";
		return false;
	}
	fs["R"] >> R;
	fs["T"] >> T;
	if (squareSize != nullptr) {
		*squareSize = 0.0f;
		cv::FileNode squareSizeNode = fs["square_size"];
		if (!squareSizeNode.empty()) {
			squareSizeNode >> *squareSize;
		}
	}
	fs.release();
	return !R.empty() && !T.empty();
}

cv::Mat enhanceUnderwaterImage(const cv::Mat& bgrInput) {
	if (bgrInput.empty()) {
		return bgrInput;
	}

	std::vector<cv::Mat> bgrChannels;
	cv::split(bgrInput, bgrChannels);
	double meanB = cv::mean(bgrChannels[0])[0];
	double meanG = cv::mean(bgrChannels[1])[0];
	double meanR = cv::mean(bgrChannels[2])[0];
	double grayMean = (meanB + meanG + meanR) / 3.0;

	double gainB = grayMean / (meanB + 1e-6);
	double gainG = grayMean / (meanG + 1e-6);
	double gainR = grayMean / (meanR + 1e-6);
	gainB = std::min(2.0, std::max(0.5, gainB));
	gainG = std::min(2.0, std::max(0.5, gainG));
	gainR = std::min(2.0, std::max(0.5, gainR));

	bgrChannels[0].convertTo(bgrChannels[0], CV_8U, gainB);
	bgrChannels[1].convertTo(bgrChannels[1], CV_8U, gainG);
	bgrChannels[2].convertTo(bgrChannels[2], CV_8U, gainR);
	cv::Mat wbImage;
	cv::merge(bgrChannels, wbImage);

	cv::Mat labImage;
	cv::cvtColor(wbImage, labImage, cv::COLOR_BGR2Lab);
	std::vector<cv::Mat> labChannels;
	cv::split(labImage, labChannels);
	auto clahe = cv::createCLAHE(2.5, cv::Size(8, 8));
	clahe->apply(labChannels[0], labChannels[0]);
	cv::merge(labChannels, labImage);

	cv::Mat contrastImage;
	cv::cvtColor(labImage, contrastImage, cv::COLOR_Lab2BGR);

	cv::Mat blurImage, sharpImage;
	cv::GaussianBlur(contrastImage, blurImage, cv::Size(0, 0), 1.2);
	cv::addWeighted(contrastImage, 1.6, blurImage, -0.6, 0.0, sharpImage);
	return sharpImage;
}
