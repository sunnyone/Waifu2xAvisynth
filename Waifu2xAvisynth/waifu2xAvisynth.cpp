#include <future>
#include <Windows.h>
#include <avisynth.h>

#include <sstream>
#include <string>

#include "picojson.h"
#include "tclap/CmdLine.h"

#include "modelHandler.hpp"

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

void outputDebug(std::function<void(std::ostringstream&)> func) {
	std::ostringstream str;
	func(str);
	OutputDebugStringA(str.str().c_str());
}

class Waifu2xVideoFilter : public GenericVideoFilter {
public:
	Waifu2xVideoFilter(PClip child, int nr, int scale, int jobs, std::string& modelsDir, IScriptEnvironment* env);

	void Initialize(IScriptEnvironment* env);
	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);

private:
	std::string modelsDir;
	int jobs;

	int nrLevel;

	bool enableScaling;
	int iterTimesTwiceScaling;
	int scaleRatioAdjusted;

	std::vector<std::unique_ptr<w2xc::Model>> modelsNR;
	std::vector<std::unique_ptr<w2xc::Model>> modelsScale;
};

Waifu2xVideoFilter::Waifu2xVideoFilter(PClip child, int nr, int scale, int jobs, std::string& modelsDir, 
	IScriptEnvironment* env) : GenericVideoFilter(child) {
	
	this->modelsDir = modelsDir;
	this->jobs = jobs;

	if (nr == 1 || nr == 2) {
		this->nrLevel = nr;
	} else {
		this->nrLevel = 0;
	}

	if (scale > 1) {
		this->enableScaling = true;
		this->iterTimesTwiceScaling = static_cast<int>(std::ceil(std::log2(scale)));
		this->scaleRatioAdjusted = pow(2, this->iterTimesTwiceScaling);

		this->vi.height *= this->scaleRatioAdjusted;
		this->vi.width *= this->scaleRatioAdjusted;
	} else {
		this->enableScaling = false;
		this->scaleRatioAdjusted = 1;
	}
}

void Waifu2xVideoFilter::Initialize(IScriptEnvironment* env) {
	if (vi.pixel_type != VideoInfo::CS_Y8 &&
		vi.pixel_type != VideoInfo::CS_YV12 &&
		vi.pixel_type != VideoInfo::CS_YV16 &&
		vi.pixel_type != VideoInfo::CS_YV24) {
		env->ThrowError("Currently only Y8, YV12, YV16 or YV24 are supported.");
		return;
	}

	if (nrLevel > 0) {
		OutputDebugStringA("Waifu2x Loading NR Models.");

		std::string modelFileName(modelsDir);
		modelFileName = modelFileName + "\\noise" + std::to_string(this->nrLevel) + "_model.json";

		if (!w2xc::modelUtility::generateModelFromJSON(modelFileName, this->modelsNR)) {
			env->ThrowError("A noise model file is not found: %s", modelFileName.c_str());
			return;
		}

		for (auto&& model : this->modelsNR) {
			model->setNumberOfJobs(jobs);
		}
	}

	if (enableScaling) {
		OutputDebugStringA("Waifu2x Loading Scale Models.");

		std::string modelFileName(modelsDir);
		modelFileName = modelFileName + "/scale2.0x_model.json";

		if (!w2xc::modelUtility::generateModelFromJSON(modelFileName, this->modelsScale)) {
			env->ThrowError("A scale model file is not found: %s", modelFileName.c_str());
			return;
		}

		for (auto&& model : this->modelsScale) {
			model->setNumberOfJobs(jobs);
		}
	}

	OutputDebugStringA("Waifu2x Finished Initializing.");
}

bool filterWithModels(std::vector<std::unique_ptr<w2xc::Model>>& models, cv::Mat& srcImgY, cv::Mat& dstImgY) {
	std::unique_ptr<std::vector<cv::Mat>> inputPlanes =
		std::unique_ptr<std::vector<cv::Mat>>(
		new std::vector<cv::Mat>());
	std::unique_ptr<std::vector<cv::Mat>> outputPlanes =
		std::unique_ptr<std::vector<cv::Mat>>(
		new std::vector<cv::Mat>());

	inputPlanes->clear();
	inputPlanes->push_back(srcImgY);

	for (int index = 0; index < models.size(); index++) {
		outputDebug([&](std::ostringstream& s) { 
			s << "Waifu2x Iteration #" << (index + 1) << "..." << std::endl;
		});

		if (!models[index]->filter(*inputPlanes, *outputPlanes)) {
			return false;
		}

		if (index != models.size() - 1) {
			inputPlanes = std::move(outputPlanes);
			outputPlanes = std::unique_ptr<std::vector<cv::Mat>>(
				new std::vector<cv::Mat>());
		}
	}

	outputPlanes->at(0).copyTo(dstImgY);
	return true;
}

PVideoFrame Waifu2xVideoFilter::GetFrame(int n, IScriptEnvironment* env) {
	int percent = (int)((n / (double)vi.num_frames) * 100);
	outputDebug([&](std::ostringstream& s) {
		s << "Waifu2x GetFrame Starting: " << n << "/" << vi.num_frames << "(" << percent << "%)";
	});

	PVideoFrame src = child->GetFrame(n, env);

	// Assume Y8, YV12, YV16 or YV24 (with chroma, planar)
	// Process Y at first.
	cv::Mat yImg(src->GetHeight(PLANAR_Y), src->GetRowSize(PLANAR_Y), CV_8U, (void *)src->GetReadPtr(PLANAR_Y), src->GetPitch(PLANAR_Y));
	yImg.convertTo(yImg, CV_32F, 1.0 / 255.0);

	if (this->nrLevel > 0) {
		OutputDebugStringA("Waifu2x NR Start.");

		if (!filterWithModels(this->modelsNR, yImg, yImg)) {
			env->ThrowError("Waifu2x NR Failed.");
			return src;
		}

		OutputDebugStringA("Waifu2x NR Finished.");
	}

	if (this->enableScaling) {
		OutputDebugStringA("Waifu2x Scaling Start.");

		int curRowSize = src->GetRowSize(PLANAR_Y);
		int curHeight = src->GetHeight(PLANAR_Y);
		for (int i = 0; i < iterTimesTwiceScaling; i++) {
			curRowSize *= 2;
			curHeight *= 2;

			cv::resize(yImg, yImg, cv::Size(curRowSize, curHeight), 0, 0, cv::INTER_NEAREST);

			if (!filterWithModels(this->modelsScale, yImg, yImg)) {
				env->ThrowError("Waifu2x filtering failed.");
				return src;
			}
		}

		OutputDebugStringA("Waifu2x Scaling Finished.");
	}
	yImg.convertTo(yImg, CV_8U, 255.0);

	// If Y8, skip processing U, V
	if (vi.pixel_type == VideoInfo::CS_Y8) {
		auto dst = env->NewVideoFrame(vi);
		env->BitBlt(dst->GetWritePtr(PLANAR_Y), dst->GetPitch(PLANAR_Y),
			yImg.data, yImg.step, yImg.cols, yImg.rows);

		OutputDebugStringA("Waifu2x GetFrame Finished (Y only).");
		return dst;
	}

	// Finally process U, V
	cv::Mat uImg(src->GetHeight(PLANAR_U), src->GetRowSize(PLANAR_U), CV_8U, (void *)src->GetReadPtr(PLANAR_U), src->GetPitch(PLANAR_U));
	cv::Mat vImg(src->GetHeight(PLANAR_V), src->GetRowSize(PLANAR_V), CV_8U, (void *)src->GetReadPtr(PLANAR_V), src->GetPitch(PLANAR_V));
	if (this->enableScaling) {
		// process U and V at first (just INTER_CUBIC resize).
		cv::resize(uImg, uImg, cv::Size(uImg.cols * this->scaleRatioAdjusted, uImg.rows * this->scaleRatioAdjusted), 0, 0, cv::INTER_CUBIC);
		cv::resize(vImg, vImg, cv::Size(vImg.cols * this->scaleRatioAdjusted, vImg.rows * this->scaleRatioAdjusted), 0, 0, cv::INTER_CUBIC);
	}

	auto dst = env->NewVideoFrame(vi);
	env->BitBlt(dst->GetWritePtr(PLANAR_Y), dst->GetPitch(PLANAR_Y),
		yImg.data, yImg.step, yImg.cols, yImg.rows);
	env->BitBlt(dst->GetWritePtr(PLANAR_U), dst->GetPitch(PLANAR_U),
		uImg.data, uImg.step, uImg.cols, uImg.rows);
	env->BitBlt(dst->GetWritePtr(PLANAR_V), dst->GetPitch(PLANAR_V),
		vImg.data, vImg.step, vImg.cols, vImg.rows);

	OutputDebugStringA("Waifu2x GetFrame Finished.");

	return dst;
}


AVSValue __cdecl Waifu2x(AVSValue args, void *, IScriptEnvironment* env) {
	enum {
		ARG_CLIP,
		ARG_NR,
		ARG_SCALE,
		ARG_JOBS,
		ARG_MODELS,
	};

	PClip clip = args[ARG_CLIP].AsClip();

	int nr = args[ARG_NR].Defined() ? args[ARG_NR].AsInt() : 1;
	int scale = args[ARG_SCALE].Defined() ? args[ARG_SCALE].AsInt() : 2;

	int jobs = args[ARG_JOBS].Defined() ? args[ARG_JOBS].AsInt() : 0;
	if (jobs <= 0) {
		SYSTEM_INFO systemInfo;
		GetSystemInfo(&systemInfo);
		jobs = systemInfo.dwNumberOfProcessors;
	}

	std::string modelsDir;
	if (args[ARG_MODELS].Defined()) {
		modelsDir.assign(args[ARG_MODELS].AsString());
	} else {
		char dllPathChars[MAX_PATH] = { 0 };
		GetModuleFileNameA((HINSTANCE)&__ImageBase, dllPathChars, _countof(dllPathChars));

		// TODO: multibyte dll name may not work.
		std::string dllPath(dllPathChars);
		modelsDir.assign(dllPath.substr(0, dllPath.find_last_of('\\')) + "\\models");
		if (modelsDir.size() > MAX_PATH) {
			// TODO: need testing
			env->ThrowError("The models directory path is too long.");
		}
	}

	auto filter = new Waifu2xVideoFilter(clip, nr, scale, jobs, modelsDir, env);
	filter->Initialize(env);
	return filter;
}

const AVS_Linkage *AVS_linkage = nullptr;
extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* const vectors) {
	AVS_linkage = vectors;
	env->AddFunction("waifu2x", "c[nr]i[scale]i[jobs]i[models]s", Waifu2x, 0);
	return "";
}