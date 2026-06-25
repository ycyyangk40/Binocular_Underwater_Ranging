#include "mode_yolo.hpp"

#include "common.hpp"

#include <opencv2/calib3d.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#ifdef YOLO_USE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef YOLO_USE_ONNXRUNTIME

namespace {

enum class DepthMethod {
	Invalid,
	HistogramPeak,
	TrimmedMedian
};

const char* DepthMethodName(DepthMethod m) {
	switch (m) {
		case DepthMethod::HistogramPeak: return "peak";
		case DepthMethod::TrimmedMedian: return "median";
		default: return "invalid";
	}
}

struct HistogramPeakResult {
	bool valid = false;

	float peak_median_m = -1.0f;
	float peak_mean_m = -1.0f;
	float peak_std_m = -1.0f;

	int peak_points = 0;
	float peak_ratio = 0.0f;
	int peak_bin_index = -1;
};

struct YoloDetection {
	int classId = -1;
	float confidence = 0.0f;
	cv::Rect box;
	double depthMeters = std::numeric_limits<double>::quiet_NaN();
	float depthConfidence = 0.0f;
	bool distanceValid = false;
	float validRatio = 0.0f;
	float peakRatio = 0.0f;
	float stdM = 0.0f;
	std::string method = "?";
};

struct LetterboxInfo {
	float scale = 1.0f;
	int padX = 0;
	int padY = 0;
};

struct DepthContext {
	cv::Mat depthMeters;
	cv::Mat validMask;
};

struct ObjectDepthConfig {
	float inner_roi_scale = 0.75f;
	float min_depth_m = 0.15f;
	float max_depth_m = 5.0f;
	float trim_ratio = 0.2f;
	int min_valid_points = 15;
	float min_valid_ratio = 0.10f;
	float max_std_m = 0.45f;
	float max_jump_m = 0.50f;
	float filter_alpha = 0.3f;
	float hist_bin_width_m = 0.05f;
	int min_peak_points = 15;
	float min_peak_ratio = 0.25f;
	float max_peak_std_m = 0.45f;
};

struct ObjectDepthResult {
	bool valid = false;
	float distance_m = -1.0f;
	DepthMethod method = DepthMethod::Invalid;

	float raw_median_m = -1.0f;
	float raw_mean_m = -1.0f;
	float std_m = -1.0f;

	float valid_ratio = 0.0f;
	int valid_points = 0;
	int total_points = 0;

	float peak_ratio = 0.0f;
	int peak_points = 0;
	float peak_std_m = -1.0f;

	float depth_confidence = 0.0f;

	cv::Rect inner_roi;
};

static const std::array<const char*, 80> kCocoClasses = {
	"person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic light",
	"fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
	"elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
	"skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket", "bottle",
	"wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange",
	"broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch", "potted plant", "bed",
	"dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone", "microwave", "oven",
	"toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear", "hair drier", "toothbrush"
};

std::string ClassName(int classId) {
	if (classId < 0 || classId >= static_cast<int>(kCocoClasses.size())) {
		return "class_" + std::to_string(classId);
	}
	return kCocoClasses[static_cast<size_t>(classId)];
}

cv::Mat Letterbox(const cv::Mat& image, const cv::Size& targetSize, LetterboxInfo& info) {
	float scaleX = static_cast<float>(targetSize.width) / static_cast<float>(image.cols);
	float scaleY = static_cast<float>(targetSize.height) / static_cast<float>(image.rows);
	info.scale = std::min(scaleX, scaleY);
	int resizedWidth = static_cast<int>(std::round(image.cols * info.scale));
	int resizedHeight = static_cast<int>(std::round(image.rows * info.scale));
	info.padX = (targetSize.width - resizedWidth) / 2;
	info.padY = (targetSize.height - resizedHeight) / 2;

	cv::Mat resized;
	cv::resize(image, resized, cv::Size(resizedWidth, resizedHeight), 0, 0, cv::INTER_LINEAR);
	cv::Mat output(targetSize, CV_8UC3, cv::Scalar(114, 114, 114));
	resized.copyTo(output(cv::Rect(info.padX, info.padY, resized.cols, resized.rows)));
	return output;
}

cv::Rect shrinkBox(const cv::Rect& box, float scale, const cv::Size& imageSize) {
	float cx = box.x + box.width * 0.5f;
	float cy = box.y + box.height * 0.5f;
	int newW = static_cast<int>(box.width * scale);
	int newH = static_cast<int>(box.height * scale);
	int x = static_cast<int>(cx - newW * 0.5f);
	int y = static_cast<int>(cy - newH * 0.5f);
	cv::Rect inner(x, y, newW, newH);
	return inner & cv::Rect(0, 0, imageSize.width, imageSize.height);
}

HistogramPeakResult findHistogramPeak(const std::vector<float>& values, float binWidthM) {
	HistogramPeakResult r;
	if (values.empty()) return r;

	float zMin = *std::min_element(values.begin(), values.end());
	float zMax = *std::max_element(values.begin(), values.end());

	if (zMax - zMin < binWidthM) {
		size_t mid = values.size() / 2;
		std::vector<float> sorted = values;
		std::nth_element(sorted.begin(), sorted.begin() + mid, sorted.end());
		r.valid = true;
		r.peak_median_m = sorted[mid];
		r.peak_points = static_cast<int>(values.size());
		r.peak_ratio = 1.0f;
		r.peak_bin_index = 0;
		return r;
	}

	int nBins = std::max(1, static_cast<int>((zMax - zMin) / binWidthM + 0.5f));
	std::vector<int> bins(nBins, 0);
	for (float z : values) {
		int idx = static_cast<int>((z - zMin) / binWidthM);
		if (idx < 0) idx = 0;
		if (idx >= nBins) idx = nBins - 1;
		bins[idx]++;
	}

	int peakIdx = 0;
	for (int i = 1; i < nBins; ++i) {
		if (bins[i] > bins[peakIdx]) peakIdx = i;
	}
	r.peak_bin_index = peakIdx;

	float lo = zMin + (peakIdx - 1.5f) * binWidthM;
	float hi = zMin + (peakIdx + 2.5f) * binWidthM;

	std::vector<float> peakVals;
	for (float z : values) {
		if (z >= lo && z <= hi) peakVals.push_back(z);
	}

	r.peak_points = static_cast<int>(peakVals.size());
	r.peak_ratio = static_cast<float>(peakVals.size()) / static_cast<float>(values.size());

	if (peakVals.empty()) return r;

	std::sort(peakVals.begin(), peakVals.end());
	int m = static_cast<int>(peakVals.size());
	r.peak_median_m = peakVals[m / 2];

	double sum = 0.0, sumSq = 0.0;
	for (float v : peakVals) { sum += v; sumSq += static_cast<double>(v) * v; }
	r.peak_mean_m = static_cast<float>(sum / m);
	float meanSq = static_cast<float>(sumSq / m);
	float var = meanSq - r.peak_mean_m * r.peak_mean_m;
	r.peak_std_m = (var > 0) ? std::sqrt(var) : 0.0f;

	r.valid = true;
	return r;
}

ObjectDepthResult estimateObjectDepth(const cv::Mat& depthMeters, const cv::Rect& bbox,
                                      const ObjectDepthConfig& cfg) {
	ObjectDepthResult r;
	if (depthMeters.empty() || bbox.width <= 0 || bbox.height <= 0) return r;

	cv::Size imageSize(depthMeters.cols, depthMeters.rows);
	r.inner_roi = shrinkBox(bbox, cfg.inner_roi_scale, imageSize);
	if (r.inner_roi.empty()) return r;

	r.total_points = r.inner_roi.area();
	std::vector<float> values;
	values.reserve(static_cast<size_t>(r.total_points));

	for (int y = r.inner_roi.y; y < r.inner_roi.y + r.inner_roi.height; ++y) {
		const float* row = depthMeters.ptr<float>(y);
		for (int x = r.inner_roi.x; x < r.inner_roi.x + r.inner_roi.width; ++x) {
			float z = row[x];
			if (std::isfinite(z) && z > cfg.min_depth_m && z < cfg.max_depth_m) {
				values.push_back(z);
			}
		}
	}

	r.valid_points = static_cast<int>(values.size());
	r.valid_ratio = (r.total_points > 0)
		? static_cast<float>(values.size()) / static_cast<float>(r.total_points) : 0.0f;

	if (r.valid_points < cfg.min_valid_points || r.valid_ratio < cfg.min_valid_ratio) return r;

	// Primary: trimmed median (proven robust)
	std::sort(values.begin(), values.end());
	int n = static_cast<int>(values.size());
	int left = static_cast<int>(n * cfg.trim_ratio);
	int right = static_cast<int>(n * (1.0f - cfg.trim_ratio));
	if (right <= left) return r;

	double sum = 0.0, sumSq = 0.0;
	for (int i = left; i < right; ++i) {
		float v = values[i];
		sum += v;
		sumSq += static_cast<double>(v) * v;
	}
	int cnt = right - left;
	r.raw_mean_m = static_cast<float>(sum / cnt);
	float meanSq = static_cast<float>(sumSq / cnt);
	float var = meanSq - r.raw_mean_m * r.raw_mean_m;
	r.std_m = (var > 0) ? std::sqrt(var) : 0.0f;
	r.raw_median_m = values[left + cnt / 2];

	r.method = DepthMethod::TrimmedMedian;
	r.distance_m = r.raw_median_m;

	// Secondary: histogram peak (for debug / future upgrade)
	HistogramPeakResult peak = findHistogramPeak(values, cfg.hist_bin_width_m);
	bool peakOk = peak.valid
		&& peak.peak_points >= cfg.min_peak_points
		&& peak.peak_ratio >= cfg.min_peak_ratio
		&& peak.peak_std_m <= cfg.max_peak_std_m;
	r.peak_ratio = peak.valid ? peak.peak_ratio : 0.0f;
	r.peak_points = peak.valid ? peak.peak_points : 0;
	r.peak_std_m = peak.valid ? peak.peak_std_m : -1.0f;

	float conf = 1.0f;
	if (r.valid_ratio < 0.25f) conf *= 0.3f;
	else if (r.valid_ratio < 0.5f) conf *= 0.7f;
	if (r.std_m > 0.45f) conf *= 0.3f;
	else if (r.std_m > 0.25f) conf *= 0.7f;
	r.depth_confidence = std::clamp(conf, 0.0f, 1.0f);

	r.valid = (r.std_m <= cfg.max_std_m);
	return r;
}

class YoloDetector {
public:
	explicit YoloDetector(const std::string& modelPath)
		: env_(ORT_LOGGING_LEVEL_WARNING, "yolo") {
		if (!std::filesystem::exists(modelPath)) {
			throw std::runtime_error("YOLO model not found: " + modelPath);
		}

		Ort::SessionOptions sessionOptions;
		sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
		sessionOptions.SetIntraOpNumThreads(1);

#ifdef YOLO_USE_ONNXRUNTIME
		try {
			Ort::CUDAProviderOptions cudaOptions;
			cudaOptions.Update({{"device_id", "0"}});
			sessionOptions.AppendExecutionProvider_CUDA_V2(*cudaOptions);
			std::cout << "ONNX Runtime CUDA provider enabled.\n";
		} catch (const std::exception& ex) {
			std::cout << "CUDA provider unavailable, falling back to CPU: " << ex.what() << "\n";
		}
#endif

		std::wstring modelPathW = std::filesystem::path(modelPath).wstring();
			session_ = std::make_unique<Ort::Session>(env_, modelPathW.c_str(), sessionOptions);

		Ort::AllocatorWithDefaultOptions allocator;
		for (size_t i = 0; i < session_->GetInputCount(); ++i) {
			auto name = session_->GetInputNameAllocated(i, allocator);
			inputNames_.emplace_back(name.get());
		}
		for (size_t i = 0; i < session_->GetOutputCount(); ++i) {
			auto name = session_->GetOutputNameAllocated(i, allocator);
			outputNames_.emplace_back(name.get());
		}
	}

	std::vector<YoloDetection> Detect(const cv::Mat& image, float confThreshold = 0.25f, float nmsThreshold = 0.45f) {
		LetterboxInfo letterboxInfo;
		cv::Mat letterboxed = Letterbox(image, cv::Size(640, 640), letterboxInfo);
		cv::Mat blob = cv::dnn::blobFromImage(letterboxed, 1.0 / 255.0, cv::Size(640, 640), cv::Scalar(), true, false, CV_32F);

		std::array<int64_t, 4> inputShape{1, 3, 640, 640};
		Ort::Value inputTensor = Ort::Value::CreateTensor<float>(memoryInfo_, blob.ptr<float>(), blob.total(), inputShape.data(), inputShape.size());

		const char* inputName = inputNames_.front().c_str();
		const char* outputName = outputNames_.front().c_str();
		std::array<const char*, 1> inputNames = {inputName};
		std::array<const char*, 1> outputNames = {outputName};
		auto outputs = session_->Run(Ort::RunOptions{nullptr}, inputNames.data(), &inputTensor, 1, outputNames.data(), 1);

		Ort::Value& output = outputs.front();
		Ort::TensorTypeAndShapeInfo shapeInfo = output.GetTensorTypeAndShapeInfo();
		std::vector<int64_t> shape = shapeInfo.GetShape();
		if (shape.size() != 3) {
			throw std::runtime_error("Unexpected YOLO output rank.");
		}

		float* data = output.GetTensorMutableData<float>();
		int64_t dim1 = shape[1];
		int64_t dim2 = shape[2];
		bool channelFirst = dim1 < dim2;
		int64_t predictionCount = channelFirst ? dim2 : dim1;
		int64_t attributeCount = channelFirst ? dim1 : dim2;
		if (attributeCount < 5) {
			throw std::runtime_error("YOLO output does not contain enough attributes.");
		}

		std::vector<cv::Rect> boxes;
		std::vector<float> scores;
		std::vector<int> classIds;
		boxes.reserve(static_cast<size_t>(predictionCount));
		scores.reserve(static_cast<size_t>(predictionCount));
		classIds.reserve(static_cast<size_t>(predictionCount));

		for (int64_t i = 0; i < predictionCount; ++i) {
			float cx = 0.0f;
			float cy = 0.0f;
			float w = 0.0f;
			float h = 0.0f;
			if (channelFirst) {
				cx = data[0 * predictionCount + i];
				cy = data[1 * predictionCount + i];
				w = data[2 * predictionCount + i];
				h = data[3 * predictionCount + i];
			} else {
				cx = data[i * attributeCount + 0];
				cy = data[i * attributeCount + 1];
				w = data[i * attributeCount + 2];
				h = data[i * attributeCount + 3];
			}

			float bestScore = 0.0f;
			int bestClass = -1;
			for (int64_t c = 4; c < attributeCount; ++c) {
				float score = channelFirst ? data[c * predictionCount + i] : data[i * attributeCount + c];
				if (score > bestScore) {
					bestScore = score;
					bestClass = static_cast<int>(c - 4);
				}
			}

			if (bestScore < confThreshold || bestClass < 0) {
				continue;
			}

			float x1 = (cx - w * 0.5f - static_cast<float>(letterboxInfo.padX)) / letterboxInfo.scale;
			float y1 = (cy - h * 0.5f - static_cast<float>(letterboxInfo.padY)) / letterboxInfo.scale;
			float x2 = (cx + w * 0.5f - static_cast<float>(letterboxInfo.padX)) / letterboxInfo.scale;
			float y2 = (cy + h * 0.5f - static_cast<float>(letterboxInfo.padY)) / letterboxInfo.scale;

			x1 = std::clamp(x1, 0.0f, static_cast<float>(image.cols - 1));
			y1 = std::clamp(y1, 0.0f, static_cast<float>(image.rows - 1));
			x2 = std::clamp(x2, 0.0f, static_cast<float>(image.cols - 1));
			y2 = std::clamp(y2, 0.0f, static_cast<float>(image.rows - 1));

			int left = static_cast<int>(std::round(x1));
			int top = static_cast<int>(std::round(y1));
			int width = std::max(1, static_cast<int>(std::round(x2 - x1)));
			int height = std::max(1, static_cast<int>(std::round(y2 - y1)));
			boxes.emplace_back(left, top, width, height);
			scores.push_back(bestScore);
			classIds.push_back(bestClass);
		}

		std::vector<int> keep;
		cv::dnn::NMSBoxes(boxes, scores, confThreshold, nmsThreshold, keep);

		std::vector<YoloDetection> detections;
		detections.reserve(keep.size());
		for (int idx : keep) {
			YoloDetection det;
			det.classId = classIds[static_cast<size_t>(idx)];
			det.confidence = scores[static_cast<size_t>(idx)];
			det.box = boxes[static_cast<size_t>(idx)];
			detections.push_back(det);
		}
		return detections;
	}

private:
	Ort::Env env_;
	std::unique_ptr<Ort::Session> session_;
	Ort::MemoryInfo memoryInfo_ = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
	std::vector<std::string> inputNames_;
	std::vector<std::string> outputNames_;
};

void onDepthClick(int event, int x, int y, int, void* userdata) {
	if (event != cv::EVENT_LBUTTONDOWN || userdata == nullptr) {
		return;
	}
	auto* ctx = static_cast<DepthContext*>(userdata);
	if (ctx->depthMeters.empty()) {
		std::cout << "Depth data not ready.\n";
		return;
	}
	if (x < 0 || y < 0 || x >= ctx->depthMeters.cols || y >= ctx->depthMeters.rows) {
		return;
	}
	int x0 = std::max(0, x - 2);
	int x1 = std::min(ctx->depthMeters.cols - 1, x + 2);
	int y0 = std::max(0, y - 2);
	int y1 = std::min(ctx->depthMeters.rows - 1, y + 2);

	std::vector<float> samples;
	for (int yy = y0; yy <= y1; ++yy) {
		for (int xx = x0; xx <= x1; ++xx) {
			float depth = ctx->depthMeters.at<float>(yy, xx);
			if (std::isfinite(depth) && depth > 0.08f && depth < 5.0f) {
				samples.push_back(depth);
			}
		}
	}
	if (samples.empty()) {
		std::cout << "Invalid depth at (" << x << ", " << y << ")\n";
		return;
	}
	std::nth_element(samples.begin(), samples.begin() + samples.size() / 2, samples.end());
	std::cout << "Point(" << x << ", " << y << ") depth=" << samples[samples.size() / 2] << " m\n";
}
} // namespace

void yoloStereoMode(int cameraDeviceNo, bool fastMode, bool showGray, bool enhance, bool showLeft, bool showRight, bool showDepth, const std::string& modelPath) {
	(void)showGray;
	std::cout << "YOLO model: " << modelPath << "\n";

#ifdef _WIN32
	auto res = _putenv("OPENCV_VIDEOIO_MSMF_ENABLE_HW_TRANSFORMS=0");
	(void)res;
#endif

	if (!std::filesystem::exists(modelPath)) {
		std::cout << "Model not found, falling back to raw camera preview.\n";
		cv::VideoCapture capture;
		if (!openStereoCamera(capture, cameraDeviceNo)) return;
		cv::Mat frame;
		while (capture.read(frame)) {
			if (!frame.empty()) {
				cv::imshow("yolo", frame);
			}
			int key = cv::waitKey(1);
			if (key == 'Q' || key == 'q') {
				break;
			}
		}
		return;
	}

	YoloDetector detector(modelPath);

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
	if (!openStereoCamera(capture, cameraDeviceNo)) return;

	if (showLeft) {
		cv::namedWindow("left", cv::WINDOW_GUI_EXPANDED);
		cv::resizeWindow("left", 1280, 720);
	}
	if (showRight) {
		cv::namedWindow("right", cv::WINDOW_GUI_EXPANDED);
		cv::resizeWindow("right", 1280, 720);
	}
	if (showDepth) {
		cv::namedWindow("depth", cv::WINDOW_GUI_EXPANDED);
		cv::resizeWindow("depth", 1280, 720);
	}
	cv::namedWindow("yolo", cv::WINDOW_GUI_EXPANDED);
	cv::resizeWindow("yolo", 1280, 720);

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

	DepthContext depthContext;
	if (showDepth) {
		cv::setMouseCallback("depth", onDepthClick, &depthContext);
	}

	ObjectDepthConfig depthCfg;
	std::unordered_map<int, float> lastDistByClass;

	auto filterDistance = [&](int cls, float cur, bool valid) -> float {
		if (!valid) {
			auto it = lastDistByClass.find(cls);
			return (it != lastDistByClass.end()) ? it->second : -1.0f;
		}
		auto it = lastDistByClass.find(cls);
		if (it == lastDistByClass.end()) {
			lastDistByClass[cls] = cur;
			return cur;
		}
		float last = it->second;
		if (std::abs(cur - last) > depthCfg.max_jump_m) return last;
		float filtered = (1.0f - depthCfg.filter_alpha) * last + depthCfg.filter_alpha * cur;
		lastDistByClass[cls] = filtered;
		return filtered;
	};

	while (true) {
		cv::Mat frame;
		if (!capture.read(frame)) {
			continue;
		}

		cv::Size combinedImageSize = frame.size();
		cv::Mat leftImage = frame(cv::Rect(0, 0, combinedImageSize.width / 2, combinedImageSize.height));
		cv::Mat rightImage = frame(cv::Rect(combinedImageSize.width / 2, 0, combinedImageSize.width / 2, combinedImageSize.height));
		cv::Mat leftBase = enhance ? enhanceUnderwaterImage(leftImage) : leftImage;
		cv::Mat rightBase = enhance ? enhanceUnderwaterImage(rightImage) : rightImage;

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

		cv::Mat leftRectGray, rightRectGray;
		cv::remap(leftGray, leftRectGray, mapLx, mapLy, cv::INTER_LINEAR);
		cv::remap(rightGray, rightRectGray, mapRx, mapRy, cv::INTER_LINEAR);

		cv::Mat leftRectColor, rightRectColor;
		cv::remap(leftBase, leftRectColor, mapLx, mapLy, cv::INTER_LINEAR);
		cv::remap(rightBase, rightRectColor, mapRx, mapRy, cv::INTER_LINEAR);

		cv::Mat disp16, disp32f;
		stereoBM->compute(leftRectGray, rightRectGray, disp16);
		disp16.convertTo(disp32f, CV_32F, 1.0 / 16.0);
		cv::Mat validMaskRaw = disp32f > 0;

		cv::Mat points3d;
		cv::reprojectImageTo3D(disp32f, points3d, Q, true);
		std::vector<cv::Mat> channels;
		cv::split(points3d, channels);
		cv::Mat depth = channels[2];
		cv::Mat depthMeters = cv::abs(depth) * unitToMeter;
		depthContext.depthMeters = depthMeters;
		depthContext.validMask = validMaskRaw;

		{
			static bool printedOnce = false;
			if (!printedOnce) {
				printedOnce = true;
				std::cout << "[DEBUG] left_rect  size = " << leftRectColor.cols << "x" << leftRectColor.rows << std::endl;
				std::cout << "[DEBUG] right_rect size = " << rightRectColor.cols << "x" << rightRectColor.rows << std::endl;
				std::cout << "[DEBUG] depth_map  size = " << depthMeters.cols << "x" << depthMeters.rows << std::endl;
			}
		}

		if (leftRectColor.size() != depthMeters.size()) {
			std::cerr << "[ERROR] left_rect size " << leftRectColor.cols << "x" << leftRectColor.rows
			          << " != depth_map size " << depthMeters.cols << "x" << depthMeters.rows
			          << " — coordinate mismatch!" << std::endl;
			return;
		}

		cv::Mat finiteMask = (depthMeters == depthMeters);
		cv::Mat depthRangeMask = (depthMeters >= depthVisMinMeters) & (depthMeters <= depthVisMaxMeters);
		cv::Mat depthMask = validMaskRaw & finiteMask & depthRangeMask;

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
		cv::Mat depthVis;
		cv::applyColorMap(255 - depthVisGray, depthVis, cv::COLORMAP_TURBO);
		depthVis.setTo(cv::Scalar(0, 0, 0), ~depthMask);

		std::vector<YoloDetection> detections = detector.Detect(leftRectColor);
		for (auto& det : detections) {
			ObjectDepthResult depthResult = estimateObjectDepth(depthMeters, det.box, depthCfg);
			det.distanceValid = depthResult.valid;
			det.depthConfidence = depthResult.depth_confidence;
			det.validRatio = depthResult.valid_ratio;
			det.peakRatio = depthResult.peak_ratio;
			det.stdM = depthResult.std_m;
			det.method = DepthMethodName(depthResult.method);

			if (depthResult.valid) {
				float filteredDist = filterDistance(det.classId, depthResult.distance_m, true);
				det.depthMeters = static_cast<double>(filteredDist);
			} else {
				float lastDist = filterDistance(det.classId, 0.0f, false);
				det.depthMeters = (lastDist > 0) ? static_cast<double>(lastDist)
					: std::numeric_limits<double>::quiet_NaN();
			}

			cv::Scalar color(80, 220, 255);
			cv::rectangle(leftRectColor, det.box, color, 2);
			cv::rectangle(depthVis, det.box, color, 2);
			if (!depthResult.inner_roi.empty()) {
				cv::rectangle(leftRectColor, depthResult.inner_roi, cv::Scalar(0, 255, 255), 1);
				cv::rectangle(depthVis, depthResult.inner_roi, cv::Scalar(0, 255, 255), 1);
			}

			std::string label = ClassName(det.classId) + " " + cv::format("%.2f", det.confidence);
			if (std::isfinite(det.depthMeters)) {
				label += " Z=" + cv::format("%.2fm", det.depthMeters);
				label += " " + det.method;
			} else {
				label += " Z=invalid";
			}
			label += " v=" + cv::format("%.2f", det.validRatio);
			label += " pk=" + cv::format("%.2f", det.peakRatio);
			if (det.distanceValid) {
				label += " s=" + cv::format("%.2f", det.stdM);
			}

			int baseline = 0;
			double fontScale = 0.40;
			int thickness = 1;
			cv::Size labelSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, fontScale, thickness, &baseline);
			int labelTop = std::max(0, det.box.y - labelSize.height - 8);
			cv::rectangle(leftRectColor,
				cv::Rect(det.box.x, labelTop, labelSize.width + 8, labelSize.height + 8),
				cv::Scalar(20, 20, 20), cv::FILLED);
			cv::putText(leftRectColor, label,
				cv::Point(det.box.x + 4, labelTop + labelSize.height + 2),
				cv::FONT_HERSHEY_SIMPLEX, fontScale, cv::Scalar(255, 255, 255), thickness, cv::LINE_AA);

			std::cout << "class=" << ClassName(det.classId)
				<< " yolo_conf=" << det.confidence
				<< " distance=" << (std::isfinite(det.depthMeters) ? cv::format("%.2f", det.depthMeters) : "invalid")
				<< " method=" << det.method
				<< " valid_ratio=" << depthResult.valid_ratio
				<< " valid_points=" << depthResult.valid_points
				<< " peak_ratio=" << depthResult.peak_ratio
				<< " peak_points=" << depthResult.peak_points
				<< " median=" << depthResult.raw_median_m
				<< " std=" << depthResult.std_m
				<< " depth_conf=" << depthResult.depth_confidence
				<< "\n";
		}

		if (showLeft) {
			cv::imshow("left", leftRectColor);
		}
		if (showRight) {
			cv::imshow("right", rightRectColor);
		}
		if (showDepth) {
			cv::imshow("depth", depthVis);
		}
		cv::imshow("yolo", leftRectColor);

		int key = cv::waitKey(1);
		if (key == 'Q' || key == 'q') {
			break;
		}
	}
}

#else  // !YOLO_USE_ONNXRUNTIME

void yoloStereoMode(int, bool, bool, bool, bool, bool, bool, const std::string&) {
	std::cerr << "YOLO mode not available: rebuild with ONNX Runtime installed.\n";
}

#endif  // YOLO_USE_ONNXRUNTIME
