#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <tuple>
#include <vector>

using ObjectPoints = std::vector<std::vector<cv::Point3f>>;
using ImagePoints = std::vector<std::vector<cv::Point2f>>;

class CameraCalibration {
public:
	CameraCalibration(int chessBoardHeight, int chessBoardWidth, float squareSize);

	std::tuple<bool, std::vector<cv::Point2f>> findCorners(cv::Mat frame);
	cv::Mat appendCorners(cv::Mat frame, std::vector<cv::Point2f> corners);
	std::tuple<bool, cv::Mat, cv::Mat, cv::Mat, cv::Mat, double, ImagePoints, ObjectPoints> calculate();

private:
	cv::Size boardSize;
	float squareSize;
	cv::Size imageSize;
	ImagePoints imagePoints;
	ObjectPoints objectPoints;
};

bool loadIntrinsics(const std::string& path, cv::Mat& cameraMatrix, cv::Mat& distCoeffs);
bool loadStereoRT(const std::string& path, cv::Mat& R, cv::Mat& T, float* squareSize = nullptr);
cv::Mat enhanceUnderwaterImage(const cv::Mat& bgrInput);
