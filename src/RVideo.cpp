//
//  RVideo.cpp
//  raylib-miniscript
//
//  VP8 video playback intrinsics
//

#include "RaylibIntrinsics.h"
#include "RaylibTypes.h"
#include "raylib.h"
#include "MiniscriptInterpreter.h"
#include "MiniscriptTypes.h"
#include "macros.h"

#include <stdio.h>
#include <stdint.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#ifdef PLATFORM_WEB
#include <emscripten/emscripten.h>
#endif

#ifndef HAVE_LIBVPX
#define HAVE_LIBVPX 0
#endif

#if HAVE_LIBVPX
#include <vpx/vpx_decoder.h>
#include <vpx/vp8dx.h>
#endif

using namespace MiniScript;

namespace {

struct EncodedFrame {
	long offset = 0;
	uint32_t size = 0;
	double pts = 0.0;
};

struct VideoPlayerState {
	std::string path;
	std::string container;
	std::string codecName;
	bool valid = false;
	bool playing = false;
	bool finished = false;
	bool webPlayer = false;
	int webHandle = 0;
	int width = 0;
	int height = 0;
	uint32_t frameCount = 0;
	double frameRate = 0.0;
	double timeLength = 0.0;
	double timePlayed = 0.0;
	double playbackRate = 1.0;
	uint32_t currentFrame = 0;
	double playBaseClock = 0.0;
	double playBaseTime = 0.0;
	double lastObservedTime = 0.0;
	bool looping = false;
	bool loopEventPending = false;
	bool finishEventPending = false;
	bool finishedPrev = false;
	FILE* fp = nullptr;
	Texture2D texture = {0};
	std::vector<unsigned char> rgba;
#if HAVE_LIBVPX
	vpx_codec_ctx_t codec;
	bool codecInited = false;
	std::vector<EncodedFrame> frames;
	size_t nextFrameIndex = 0;
#endif
};

#ifdef PLATFORM_WEB
EM_ASYNC_JS(int, WebVideoLoad, (const char* urlPtr, int* outW, int* outH, double* outDuration), {
	const url = UTF8ToString(urlPtr);
	if (!Module.vpxVideoLoad) return 0;
	try {
		const meta = await Module.vpxVideoLoad(url);
		HEAP32[outW >> 2] = meta.width | 0;
		HEAP32[outH >> 2] = meta.height | 0;
		HEAPF64[outDuration >> 3] = +meta.duration || 0;
		return meta.id | 0;
	} catch (e) {
		console.error("WebVideoLoad failed:", e);
		return 0;
	}
});

EM_JS(void, WebVideoDestroy, (int id), {
	if (Module.vpxVideoDestroy) Module.vpxVideoDestroy(id);
});

EM_JS(void, WebVideoPlay, (int id), {
	if (Module.vpxVideoPlay) Module.vpxVideoPlay(id);
});

EM_JS(void, WebVideoPause, (int id), {
	if (Module.vpxVideoPause) Module.vpxVideoPause(id);
});

EM_JS(void, WebVideoStop, (int id), {
	if (Module.vpxVideoStop) Module.vpxVideoStop(id);
});

EM_JS(void, WebVideoSeek, (int id, double timeSec), {
	if (Module.vpxVideoSeek) Module.vpxVideoSeek(id, timeSec);
});

EM_JS(int, WebVideoIsPlaying, (int id), {
	if (!Module.vpxVideoIsPlaying) return 0;
	return Module.vpxVideoIsPlaying(id) | 0;
});

EM_JS(int, WebVideoIsFinished, (int id), {
	if (!Module.vpxVideoIsFinished) return 1;
	return Module.vpxVideoIsFinished(id) | 0;
});

EM_JS(double, WebVideoGetTimePlayed, (int id), {
	if (!Module.vpxVideoGetTimePlayed) return 0;
	return +Module.vpxVideoGetTimePlayed(id);
});

EM_JS(double, WebVideoGetTimeLength, (int id), {
	if (!Module.vpxVideoGetTimeLength) return 0;
	return +Module.vpxVideoGetTimeLength(id);
});

EM_JS(void, WebVideoSetLooping, (int id, int enabled), {
	if (Module.vpxVideoSetLooping) Module.vpxVideoSetLooping(id, enabled);
});

EM_JS(int, WebVideoGetLooping, (int id), {
	if (!Module.vpxVideoGetLooping) return 0;
	return Module.vpxVideoGetLooping(id) | 0;
});

EM_JS(void, WebVideoSetPlaybackRate, (int id, double rate), {
	if (Module.vpxVideoSetPlaybackRate) Module.vpxVideoSetPlaybackRate(id, rate);
});

EM_JS(double, WebVideoGetPlaybackRate, (int id), {
	if (!Module.vpxVideoGetPlaybackRate) return 1.0;
	return +Module.vpxVideoGetPlaybackRate(id);
});

EM_JS(int, WebVideoCopyFrameRGBA, (int id, uint8_t* dstPtr, int maxBytes), {
	if (!Module.vpxVideoCopyFrameRGBA) return 0;
	return Module.vpxVideoCopyFrameRGBA(id, dstPtr, maxBytes) | 0;
});
#endif

static uint16_t ReadLE16(const unsigned char* p) {
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t ReadLE32(const unsigned char* p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t ReadLE64(const unsigned char* p) {
	return (uint64_t)p[0] |
		((uint64_t)p[1] << 8) |
		((uint64_t)p[2] << 16) |
		((uint64_t)p[3] << 24) |
		((uint64_t)p[4] << 32) |
		((uint64_t)p[5] << 40) |
		((uint64_t)p[6] << 48) |
		((uint64_t)p[7] << 56);
}

static unsigned char ClampByte(int v) {
	if (v < 0) return 0;
	if (v > 255) return 255;
	return (unsigned char)v;
}

#if HAVE_LIBVPX
static bool ReadFileBytes(const char* path, std::vector<unsigned char>* out) {
	FILE* f = fopen(path, "rb");
	if (!f) return false;
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return false;
	}
	long size = ftell(f);
	if (size <= 0) {
		fclose(f);
		return false;
	}
	if (fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return false;
	}
	out->resize((size_t)size);
	if (fread(out->data(), 1, (size_t)size, f) != (size_t)size) {
		fclose(f);
		return false;
	}
	fclose(f);
	return true;
}

static bool ReadEBMLVint(const std::vector<unsigned char>& data, size_t* pos, int maxLen, bool stripMarker, uint64_t* outValue, int* outLen, bool* outUnknown) {
	if (*pos >= data.size()) return false;
	unsigned char first = data[*pos];
	unsigned char mask = 0x80;
	int len = 1;
	while (len <= maxLen && (first & mask) == 0) {
		mask >>= 1;
		len += 1;
	}
	if (len > maxLen) return false;
	if (*pos + (size_t)len > data.size()) return false;

	uint64_t val = stripMarker ? (uint64_t)(first & (mask - 1)) : (uint64_t)first;
	for (int i = 1; i < len; i++) {
		val = (val << 8) | (uint64_t)data[*pos + (size_t)i];
	}

	bool unknown = false;
	if (stripMarker) {
		uint64_t maxVal = (len == 8) ? UINT64_MAX : ((1ULL << (7 * len)) - 1ULL);
		unknown = (val == maxVal);
	}

	*pos += (size_t)len;
	*outValue = val;
	if (outLen) *outLen = len;
	if (outUnknown) *outUnknown = unknown;
	return true;
}

static bool ParseSimpleBlock(const std::vector<unsigned char>& fileData,
							 size_t payloadStart,
							 size_t payloadSize,
							 uint64_t clusterTimecode,
							 uint64_t timecodeScale,
							 uint64_t wantedTrack,
							 std::vector<EncodedFrame>* outFrames) {
	size_t p = payloadStart;
	size_t payloadEnd = payloadStart + payloadSize;
	if (payloadEnd > fileData.size()) return false;

	uint64_t trackNum = 0;
	if (!ReadEBMLVint(fileData, &p, 8, true, &trackNum, nullptr, nullptr)) return false;
	if (p + 3 > payloadEnd) return false;

	int16_t relTc = (int16_t)(((int)fileData[p] << 8) | (int)fileData[p + 1]);
	p += 2;
	unsigned char flags = fileData[p++];

	unsigned char lacing = (unsigned char)((flags >> 1) & 0x03);
	if (lacing != 0) return true;
	if (trackNum != wantedTrack) return true;
	if (p >= payloadEnd) return true;

	uint64_t tc = clusterTimecode + (int64_t)relTc;
	double pts = ((double)tc * (double)timecodeScale) / 1000000000.0;

	EncodedFrame frame;
	frame.offset = (long)p;
	frame.size = (uint32_t)(payloadEnd - p);
	frame.pts = pts;
	outFrames->push_back(frame);
	return true;
}

static bool ParseWebMFrames(const char* path, int* outW, int* outH, double* outFps, std::vector<EncodedFrame>* outFrames, double* outDuration) {
	std::vector<unsigned char> fileData;
	if (!ReadFileBytes(path, &fileData)) return false;

	size_t pos = 0;
	size_t segmentStart = 0;
	size_t segmentEnd = 0;
	bool foundSegment = false;

	while (pos < fileData.size()) {
		size_t elemPos = pos;
		uint64_t elemId = 0;
		uint64_t elemSize = 0;
		bool sizeUnknown = false;
		if (!ReadEBMLVint(fileData, &pos, 4, false, &elemId, nullptr, nullptr)) return false;
		if (!ReadEBMLVint(fileData, &pos, 8, true, &elemSize, nullptr, &sizeUnknown)) return false;

		size_t payloadStart = pos;
		size_t payloadEnd = sizeUnknown ? fileData.size() : (payloadStart + (size_t)elemSize);
		if (payloadEnd > fileData.size()) return false;

		if (elemId == 0x18538067ULL) {
			segmentStart = payloadStart;
			segmentEnd = payloadEnd;
			foundSegment = true;
			break;
		}
		if (payloadEnd <= elemPos) break;
		pos = payloadEnd;
	}

	if (!foundSegment) return false;

	uint64_t timecodeScale = 1000000ULL;
	uint64_t videoTrack = 0;
	double fps = 0.0;
	int width = 0;
	int height = 0;
	std::vector<EncodedFrame> frames;

	pos = segmentStart;
	while (pos < segmentEnd) {
		size_t elemStart = pos;
		uint64_t elemId = 0;
		uint64_t elemSize = 0;
		bool sizeUnknown = false;
		if (!ReadEBMLVint(fileData, &pos, 4, false, &elemId, nullptr, nullptr)) break;
		if (!ReadEBMLVint(fileData, &pos, 8, true, &elemSize, nullptr, &sizeUnknown)) break;
		size_t payloadStart = pos;
		size_t payloadEnd = sizeUnknown ? segmentEnd : (payloadStart + (size_t)elemSize);
		if (payloadEnd > segmentEnd) break;

		if (elemId == 0x1549A966ULL) {
			size_t p = payloadStart;
			while (p < payloadEnd) {
				uint64_t id = 0;
				uint64_t sz = 0;
				bool unk = false;
				if (!ReadEBMLVint(fileData, &p, 4, false, &id, nullptr, nullptr)) break;
				if (!ReadEBMLVint(fileData, &p, 8, true, &sz, nullptr, &unk)) break;
				size_t s = p;
				size_t e = unk ? payloadEnd : (s + (size_t)sz);
				if (e > payloadEnd) break;
				if (id == 0x2AD7B1ULL && sz > 0 && sz <= 8) {
					uint64_t v = 0;
					for (size_t i = 0; i < (size_t)sz; i++) v = (v << 8) | fileData[s + i];
					timecodeScale = v;
				}
				p = e;
			}
		} else if (elemId == 0x1654AE6BULL) {
			size_t p = payloadStart;
			while (p < payloadEnd) {
				uint64_t id = 0;
				uint64_t sz = 0;
				bool unk = false;
				if (!ReadEBMLVint(fileData, &p, 4, false, &id, nullptr, nullptr)) break;
				if (!ReadEBMLVint(fileData, &p, 8, true, &sz, nullptr, &unk)) break;
				size_t s = p;
				size_t e = unk ? payloadEnd : (s + (size_t)sz);
				if (e > payloadEnd) break;

				if (id == 0xAEULL) {
					uint64_t tn = 0;
					uint64_t tt = 0;
					std::string codec;
					uint64_t defaultDuration = 0;
					int tw = 0;
					int th = 0;

					size_t tp = s;
					while (tp < e) {
						uint64_t tid = 0;
						uint64_t tsz = 0;
						bool tunk = false;
						if (!ReadEBMLVint(fileData, &tp, 4, false, &tid, nullptr, nullptr)) break;
						if (!ReadEBMLVint(fileData, &tp, 8, true, &tsz, nullptr, &tunk)) break;
						size_t ts = tp;
						size_t te = tunk ? e : (ts + (size_t)tsz);
						if (te > e) break;

						if (tid == 0xD7ULL && tsz > 0 && tsz <= 8) {
							for (size_t i = 0; i < (size_t)tsz; i++) tn = (tn << 8) | fileData[ts + i];
						} else if (tid == 0x83ULL && tsz > 0 && tsz <= 8) {
							for (size_t i = 0; i < (size_t)tsz; i++) tt = (tt << 8) | fileData[ts + i];
						} else if (tid == 0x86ULL) {
							codec.assign((const char*)&fileData[ts], (size_t)tsz);
						} else if (tid == 0x23E383ULL && tsz > 0 && tsz <= 8) {
							for (size_t i = 0; i < (size_t)tsz; i++) defaultDuration = (defaultDuration << 8) | fileData[ts + i];
						} else if (tid == 0xE0ULL) {
							size_t vp = ts;
							while (vp < te) {
								uint64_t vid = 0;
								uint64_t vsz = 0;
								bool vunk = false;
								if (!ReadEBMLVint(fileData, &vp, 4, false, &vid, nullptr, nullptr)) break;
								if (!ReadEBMLVint(fileData, &vp, 8, true, &vsz, nullptr, &vunk)) break;
								size_t vps = vp;
								size_t vpe = vunk ? te : (vps + (size_t)vsz);
								if (vpe > te) break;
								if (vid == 0xB0ULL && vsz > 0 && vsz <= 8) {
									uint64_t v = 0;
									for (size_t i = 0; i < (size_t)vsz; i++) v = (v << 8) | fileData[vps + i];
									tw = (int)v;
								} else if (vid == 0xBAULL && vsz > 0 && vsz <= 8) {
									uint64_t v = 0;
									for (size_t i = 0; i < (size_t)vsz; i++) v = (v << 8) | fileData[vps + i];
									th = (int)v;
								}
								vp = vpe;
							}
						}

						tp = te;
					}

					if (tt == 1 && codec == "V_VP8") {
						videoTrack = tn;
						if (tw > 0) width = tw;
						if (th > 0) height = th;
						if (defaultDuration > 0) fps = 1000000000.0 / (double)defaultDuration;
					}
				}
				p = e;
			}
		} else if (elemId == 0x1F43B675ULL) {
			if (videoTrack == 0) {
				pos = payloadEnd;
				continue;
			}
			uint64_t clusterTimecode = 0;
			size_t p = payloadStart;
			while (p < payloadEnd) {
				uint64_t id = 0;
				uint64_t sz = 0;
				bool unk = false;
				if (!ReadEBMLVint(fileData, &p, 4, false, &id, nullptr, nullptr)) break;
				if (!ReadEBMLVint(fileData, &p, 8, true, &sz, nullptr, &unk)) break;
				size_t s = p;
				size_t e = unk ? payloadEnd : (s + (size_t)sz);
				if (e > payloadEnd) break;

				if (id == 0xE7ULL && sz > 0 && sz <= 8) {
					clusterTimecode = 0;
					for (size_t i = 0; i < (size_t)sz; i++) clusterTimecode = (clusterTimecode << 8) | fileData[s + i];
				} else if (id == 0xA3ULL) {
					ParseSimpleBlock(fileData, s, (size_t)sz, clusterTimecode, timecodeScale, videoTrack, &frames);
				} else if (id == 0xA0ULL) {
					size_t bg = s;
					while (bg < e) {
						uint64_t bid = 0;
						uint64_t bsz = 0;
						bool bunk = false;
						if (!ReadEBMLVint(fileData, &bg, 4, false, &bid, nullptr, nullptr)) break;
						if (!ReadEBMLVint(fileData, &bg, 8, true, &bsz, nullptr, &bunk)) break;
						size_t bs = bg;
						size_t be = bunk ? e : (bs + (size_t)bsz);
						if (be > e) break;
						if (bid == 0xA1ULL) {
							ParseSimpleBlock(fileData, bs, (size_t)bsz, clusterTimecode, timecodeScale, videoTrack, &frames);
						}
						bg = be;
					}
				}

				p = e;
			}
		}

		if (payloadEnd <= elemStart) break;
		pos = payloadEnd;
	}

	if (videoTrack == 0 || width <= 0 || height <= 0 || frames.empty()) return false;

	std::sort(frames.begin(), frames.end(), [](const EncodedFrame& a, const EncodedFrame& b) {
		return a.pts < b.pts;
	});

	if (fps <= 0.0 && frames.size() >= 2) {
		double span = frames.back().pts - frames.front().pts;
		if (span > 0.0) fps = (double)(frames.size() - 1) / span;
	}
	if (fps <= 0.0) fps = 30.0;

	*outW = width;
	*outH = height;
	*outFps = fps;
	*outFrames = frames;
	*outDuration = frames.back().pts + (1.0 / fps);
	return true;
}

static bool BuildIVFFrameIndex(FILE* fp, std::vector<EncodedFrame>* outFrames, uint32_t* outWidth, uint32_t* outHeight, double* outFps, double* outDuration) {
	unsigned char header[32];
	if (fseek(fp, 0, SEEK_SET) != 0) return false;
	if (fread(header, 1, sizeof(header), fp) != sizeof(header)) return false;

	if (!(header[0] == 'D' && header[1] == 'K' && header[2] == 'I' && header[3] == 'F')) return false;

	uint32_t fourcc = ReadLE32(&header[8]);
	if (fourcc != 0x30385056u) return false;

	uint32_t width = ReadLE16(&header[12]);
	uint32_t height = ReadLE16(&header[14]);
	uint32_t rate = ReadLE32(&header[16]);
	uint32_t scale = ReadLE32(&header[20]);
	if (rate == 0) rate = 30;
	if (scale == 0) scale = 1;
	double fps = (double)rate / (double)scale;
	if (fps <= 0.0) fps = 30.0;

	std::vector<EncodedFrame> frames;
	while (true) {
		unsigned char frameHeader[12];
		if (fread(frameHeader, 1, sizeof(frameHeader), fp) != sizeof(frameHeader)) break;
		uint32_t frameSize = ReadLE32(frameHeader);
		uint64_t ts = ReadLE64(&frameHeader[4]);
		long payloadOffset = ftell(fp);
		if (frameSize == 0) break;

		if (fseek(fp, (long)frameSize, SEEK_CUR) != 0) break;

		EncodedFrame frame;
		frame.offset = payloadOffset;
		frame.size = frameSize;
		frame.pts = ((double)ts * (double)scale) / (double)rate;
		frames.push_back(frame);
	}

	if (frames.empty()) return false;
	if (frames.back().pts <= 0.0) {
		for (size_t i = 0; i < frames.size(); i++) frames[i].pts = (double)i / fps;
	}

	*outFrames = frames;
	*outWidth = width;
	*outHeight = height;
	*outFps = fps;
	*outDuration = frames.back().pts + (1.0 / fps);
	return true;
}

static void ConvertI420ToRGBA(const vpx_image_t* img, std::vector<unsigned char>& outRGBA) {
	const int w = img->d_w;
	const int h = img->d_h;
	outRGBA.resize((size_t)w * (size_t)h * 4u);

	const unsigned char* yPlane = img->planes[VPX_PLANE_Y];
	const unsigned char* uPlane = img->planes[VPX_PLANE_U];
	const unsigned char* vPlane = img->planes[VPX_PLANE_V];
	const int yStride = img->stride[VPX_PLANE_Y];
	const int uStride = img->stride[VPX_PLANE_U];
	const int vStride = img->stride[VPX_PLANE_V];

	for (int y = 0; y < h; y++) {
		unsigned char* dstRow = &outRGBA[(size_t)y * (size_t)w * 4u];
		const unsigned char* yRow = yPlane + y * yStride;
		const unsigned char* uRow = uPlane + (y >> 1) * uStride;
		const unsigned char* vRow = vPlane + (y >> 1) * vStride;
		for (int x = 0; x < w; x++) {
			const int Y = (int)yRow[x];
			const int U = (int)uRow[x >> 1] - 128;
			const int V = (int)vRow[x >> 1] - 128;
			const int C = Y - 16;
			const int D = U;
			const int E = V;
			dstRow[x * 4 + 0] = ClampByte((298 * C + 409 * E + 128) >> 8);
			dstRow[x * 4 + 1] = ClampByte((298 * C - 100 * D - 208 * E + 128) >> 8);
			dstRow[x * 4 + 2] = ClampByte((298 * C + 516 * D + 128) >> 8);
			dstRow[x * 4 + 3] = 255;
		}
	}
}

static bool InitDecoder(VideoPlayerState* state) {
	if (state->codecInited) {
		vpx_codec_destroy(&state->codec);
		state->codecInited = false;
	}
	if (vpx_codec_dec_init(&state->codec, vpx_codec_vp8_dx(), nullptr, 0) != VPX_CODEC_OK) return false;
	state->codecInited = true;
	return true;
}

static bool RewindDesktopVideo(VideoPlayerState* state) {
	if (!state || !state->fp) return false;
	if (!InitDecoder(state)) return false;
	state->nextFrameIndex = 0;
	state->currentFrame = 0;
	state->timePlayed = 0.0;
	state->finished = false;
	state->playBaseClock = GetTime();
	state->playBaseTime = 0.0;
	state->lastObservedTime = 0.0;
	state->finishedPrev = false;
	return true;
}

static bool DecodeFrameAtIndex(VideoPlayerState* state, size_t idx) {
	if (!state || !state->fp || !state->codecInited || idx >= state->frames.size()) return false;
	const EncodedFrame& f = state->frames[idx];
	if (fseek(state->fp, f.offset, SEEK_SET) != 0) return false;

	std::vector<unsigned char> packet(f.size);
	if (fread(packet.data(), 1, f.size, state->fp) != f.size) return false;
	if (vpx_codec_decode(&state->codec, packet.data(), (unsigned int)packet.size(), nullptr, 0) != VPX_CODEC_OK) return false;

	vpx_codec_iter_t iter = nullptr;
	const vpx_image_t* img = vpx_codec_get_frame(&state->codec, &iter);
	if (!img) return false;

	ConvertI420ToRGBA(img, state->rgba);
	if (state->texture.id != 0 && !state->rgba.empty()) UpdateTexture(state->texture, state->rgba.data());

	state->currentFrame = (uint32_t)(idx + 1);
	state->timePlayed = f.pts;
	state->nextFrameIndex = idx + 1;
	if (state->nextFrameIndex >= state->frames.size()) {
		state->finished = true;
		state->playing = false;
	}
	return true;
}

static VideoPlayerState* LoadDesktopVideo(const char* path) {
	FILE* fp = fopen(path, "rb");
	if (!fp) return nullptr;

	VideoPlayerState* state = new VideoPlayerState();
	state->path = path;
	state->fp = fp;

	uint32_t ivfW = 0;
	uint32_t ivfH = 0;
	double ivfFps = 0.0;
	double ivfDuration = 0.0;
	std::vector<EncodedFrame> ivfFrames;

	int webmW = 0;
	int webmH = 0;
	double webmFps = 0.0;
	double webmDuration = 0.0;
	std::vector<EncodedFrame> webmFrames;

	bool loaded = BuildIVFFrameIndex(fp, &ivfFrames, &ivfW, &ivfH, &ivfFps, &ivfDuration);
	if (loaded) {
		state->container = "ivf";
		state->codecName = "vp8";
		state->width = (int)ivfW;
		state->height = (int)ivfH;
		state->frameRate = ivfFps;
		state->timeLength = ivfDuration;
		state->frames = ivfFrames;
	} else if (ParseWebMFrames(path, &webmW, &webmH, &webmFps, &webmFrames, &webmDuration)) {
		state->container = "webm";
		state->codecName = "vp8";
		state->width = webmW;
		state->height = webmH;
		state->frameRate = webmFps;
		state->timeLength = webmDuration;
		state->frames = webmFrames;
	} else {
		TraceLog(LOG_WARNING, "LoadVideoStream: desktop expects VP8 in IVF or WebM (V_VP8) container");
		fclose(fp);
		delete state;
		return nullptr;
	}

	Image blank = GenImageColor(state->width, state->height, BLACK);
	state->texture = LoadTextureFromImage(blank);
	UnloadImage(blank);
	if (state->texture.id == 0) {
		fclose(fp);
		delete state;
		return nullptr;
	}

	if (!InitDecoder(state)) {
		UnloadTexture(state->texture);
		fclose(fp);
		delete state;
		return nullptr;
	}

	state->frameCount = (uint32_t)state->frames.size();
	state->rgba.resize((size_t)state->width * (size_t)state->height * 4u);
	state->playBaseClock = GetTime();
	state->playBaseTime = 0.0;
	state->valid = true;
	return state;
}
#endif

static void SyncVideoStateValue(Value videoVal, VideoPlayerState* state) {
	UpdateVideoPlayerStateValue(videoVal, (int)state->currentFrame, state->timePlayed, state->playing ? 1 : 0, state->finished ? 1 : 0);
}

static void DestroyVideoPlayer(VideoPlayerState* state) {
	if (!state) return;
#ifdef PLATFORM_WEB
	if (state->webPlayer && state->webHandle > 0) {
		WebVideoDestroy(state->webHandle);
		state->webHandle = 0;
	}
#endif
#if HAVE_LIBVPX
	if (state->codecInited) {
		vpx_codec_destroy(&state->codec);
		state->codecInited = false;
	}
#endif
	if (state->fp) {
		fclose(state->fp);
		state->fp = nullptr;
	}
	if (state->texture.id != 0) {
		UnloadTexture(state->texture);
		state->texture = Texture2D{0};
	}
	delete state;
}

static void FreeVideoMapTextureHandle(Value videoVal) {
	if (videoVal.type != ValueType::Map) return;
	ValueDict videoMap = videoVal.GetDict();
	Value texVal = videoMap.Lookup(String("texture"), Value::null);
	if (texVal.type != ValueType::Map) return;
	ValueDict texMap = texVal.GetDict();
	Value texHandleVal = texMap.Lookup(String("_handle"), Value::zero);
	Texture* texPtr = (Texture*)ValueToPointer(texHandleVal);
	if (texPtr) delete texPtr;
}

#ifdef PLATFORM_WEB
static VideoPlayerState* LoadWebVideo(const char* path) {
	int width = 0;
	int height = 0;
	double duration = 0.0;
	int handle = WebVideoLoad(path, &width, &height, &duration);
	if (handle <= 0 || width <= 0 || height <= 0) return nullptr;

	VideoPlayerState* state = new VideoPlayerState();
	state->path = path;
	state->container = "webm";
	state->codecName = "browser";
	state->webPlayer = true;
	state->webHandle = handle;
	state->width = width;
	state->height = height;
	state->frameRate = 30.0;
	state->timeLength = duration > 0.0 ? duration : 0.0;
	state->frameCount = (state->timeLength > 0.0) ? (uint32_t)(state->timeLength * state->frameRate) : 0;

	Image blank = GenImageColor(state->width, state->height, BLACK);
	state->texture = LoadTextureFromImage(blank);
	UnloadImage(blank);
	if (state->texture.id == 0) {
		DestroyVideoPlayer(state);
		return nullptr;
	}

	state->rgba.resize((size_t)state->width * (size_t)state->height * 4u);
	state->valid = true;
	return state;
}
#endif

}  // namespace

void AddRVideoMethods(ValueDict raylibModule) {
	Intrinsic* i;

	i = Intrinsic::Create("");
	i->AddParam("fileName");
	i->code = INTRINSIC_LAMBDA {
		String path = context->GetVar(String("fileName")).ToString();
#if !defined(PLATFORM_WEB) && !HAVE_LIBVPX
		TraceLog(LOG_WARNING, "LoadVideoStream: libvpx support not enabled in this build");
		(void)path;
		return IntrinsicResult::Null;
#else
		VideoPlayerState* state = nullptr;
#ifdef PLATFORM_WEB
		state = LoadWebVideo(path.c_str());
#else
		state = LoadDesktopVideo(path.c_str());
#endif
		if (!state || !state->valid) return IntrinsicResult::Null;
		return IntrinsicResult(VideoPlayerToValue(state, state->texture, state->width, state->height, (int)state->frameCount, state->frameRate, state->timeLength));
#endif
	};
	raylibModule.SetValue("LoadVideoStream", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->code = INTRINSIC_LAMBDA {
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
		return IntrinsicResult(state != nullptr && state->valid);
	};
	raylibModule.SetValue("IsVideoStreamValid", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->code = INTRINSIC_LAMBDA {
		Value videoVal = context->GetVar(String("video"));
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(videoVal);
		if (!state || !state->valid) return IntrinsicResult::Null;
#ifdef PLATFORM_WEB
		if (state->webPlayer && state->finished) {
			WebVideoSeek(state->webHandle, 0.0);
			state->finished = false;
		}
		if (state->webPlayer) WebVideoPlay(state->webHandle);
#else
		if (state->finished && !RewindDesktopVideo(state)) return IntrinsicResult::Null;
#endif
		state->playing = true;
		state->playBaseClock = GetTime();
		state->playBaseTime = state->timePlayed;
		state->lastObservedTime = state->timePlayed;
		state->finishedPrev = state->finished;
		SyncVideoStateValue(videoVal, state);
		return IntrinsicResult::Null;
	};
	raylibModule.SetValue("PlayVideoStream", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->code = INTRINSIC_LAMBDA {
		Value videoVal = context->GetVar(String("video"));
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(videoVal);
		if (!state || !state->valid) return IntrinsicResult::Null;
#ifdef PLATFORM_WEB
		if (state->webPlayer) WebVideoPause(state->webHandle);
#endif
		state->playing = false;
		SyncVideoStateValue(videoVal, state);
		return IntrinsicResult::Null;
	};
	raylibModule.SetValue("PauseVideoStream", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->code = INTRINSIC_LAMBDA {
		Value videoVal = context->GetVar(String("video"));
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(videoVal);
		if (!state || !state->valid) return IntrinsicResult::Null;
		if (!state->finished) {
#ifdef PLATFORM_WEB
			if (state->webPlayer) WebVideoPlay(state->webHandle);
#endif
			state->playing = true;
			state->playBaseClock = GetTime();
			state->playBaseTime = state->timePlayed;
			state->lastObservedTime = state->timePlayed;
		}
		state->finishedPrev = state->finished;
		SyncVideoStateValue(videoVal, state);
		return IntrinsicResult::Null;
	};
	raylibModule.SetValue("ResumeVideoStream", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->code = INTRINSIC_LAMBDA {
		Value videoVal = context->GetVar(String("video"));
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(videoVal);
		if (!state || !state->valid) return IntrinsicResult::Null;
		state->playing = false;
#ifdef PLATFORM_WEB
		if (state->webPlayer) {
			WebVideoStop(state->webHandle);
			state->timePlayed = 0.0;
			state->currentFrame = 0;
			state->finished = false;
		}
#else
		if (!RewindDesktopVideo(state)) return IntrinsicResult::Null;
#endif
		state->loopEventPending = false;
		state->finishEventPending = false;
		SyncVideoStateValue(videoVal, state);
		return IntrinsicResult::Null;
	};
	raylibModule.SetValue("StopVideoStream", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->AddParam("position", Value::zero);
	i->code = INTRINSIC_LAMBDA {
		Value videoVal = context->GetVar(String("video"));
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(videoVal);
		if (!state || !state->valid) return IntrinsicResult::Null;

		double pos = context->GetVar(String("position")).DoubleValue();
		if (pos < 0.0) pos = 0.0;
		if (state->timeLength > 0.0 && pos > state->timeLength) pos = state->timeLength;

#ifdef PLATFORM_WEB
		if (state->webPlayer) {
			WebVideoSeek(state->webHandle, pos);
			state->timePlayed = pos;
			state->currentFrame = (state->frameRate > 0.0) ? (uint32_t)(pos * state->frameRate) : 0;
			state->finished = false;
		}
#else
		if (!RewindDesktopVideo(state)) return IntrinsicResult::Null;
		for (size_t idx = 0; idx < state->frames.size(); idx++) {
			if (state->frames[idx].pts > pos) break;
			if (!DecodeFrameAtIndex(state, idx)) break;
		}
		state->timePlayed = pos;
		state->finished = false;
#endif
		state->playBaseClock = GetTime();
		state->playBaseTime = pos;
		state->lastObservedTime = pos;
		state->finishedPrev = state->finished;
		SyncVideoStateValue(videoVal, state);
		return IntrinsicResult::Null;
	};
	raylibModule.SetValue("SeekVideoStream", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->code = INTRINSIC_LAMBDA {
		Value videoVal = context->GetVar(String("video"));
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(videoVal);
		if (!state || !state->valid) return IntrinsicResult::Null;

#ifdef PLATFORM_WEB
		if (state->webPlayer && state->texture.id != 0) {
			if (state->rgba.empty()) state->rgba.resize((size_t)state->width * (size_t)state->height * 4u);
			int copied = WebVideoCopyFrameRGBA(state->webHandle, state->rgba.data(), (int)state->rgba.size());
			if (copied > 0) UpdateTexture(state->texture, state->rgba.data());
			double prevTime = state->timePlayed;
			state->timePlayed = WebVideoGetTimePlayed(state->webHandle);
			double duration = WebVideoGetTimeLength(state->webHandle);
			if (duration > 0.0) state->timeLength = duration;
			state->playing = WebVideoIsPlaying(state->webHandle) != 0;
			state->finished = WebVideoIsFinished(state->webHandle) != 0;
			if (state->looping && state->timePlayed + 0.05 < prevTime) {
				state->loopEventPending = true;
			}
			if (state->finished && !state->finishedPrev) {
				state->finishEventPending = true;
			}
			state->finishedPrev = state->finished;
			if (state->frameRate > 0.0) state->currentFrame = (uint32_t)(state->timePlayed * state->frameRate);
		}
#else
		if (state->playing && !state->finished) {
			double targetTime = state->playBaseTime + (GetTime() - state->playBaseClock) * state->playbackRate;
			if (targetTime < 0.0) targetTime = 0.0;

			if (state->looping && state->timeLength > 0.0) {
				double wrapped = fmod(targetTime, state->timeLength);
				if (wrapped < 0.0) wrapped += state->timeLength;
				if (wrapped + 0.0005 < state->timePlayed) {
					state->loopEventPending = true;
					if (!RewindDesktopVideo(state)) return IntrinsicResult::Null;
				}
				targetTime = wrapped;
			}

			int decoded = 0;
			const int decodeBudget = 8;
			while (state->nextFrameIndex < state->frames.size() && state->frames[state->nextFrameIndex].pts <= targetTime + 0.0005 && decoded < decodeBudget) {
				if (!DecodeFrameAtIndex(state, state->nextFrameIndex)) {
					state->finished = true;
					state->playing = false;
					break;
				}
				decoded += 1;
			}
			state->timePlayed = targetTime;
			if (state->timeLength > 0.0 && state->timePlayed > state->timeLength) state->timePlayed = state->timeLength;
			if (!state->looping && state->nextFrameIndex >= state->frames.size() && state->timePlayed >= state->timeLength - 0.0005) {
				state->finished = true;
				state->playing = false;
			}
			if (state->finished && !state->finishedPrev) {
				state->finishEventPending = true;
			}
			state->finishedPrev = state->finished;
		}
#endif

		SyncVideoStateValue(videoVal, state);
		return IntrinsicResult::Null;
	};
	raylibModule.SetValue("UpdateVideoStream", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->code = INTRINSIC_LAMBDA {
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
		return IntrinsicResult(state && state->valid && state->playing && !state->finished);
	};
	raylibModule.SetValue("IsVideoStreamPlaying", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->code = INTRINSIC_LAMBDA {
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
		if (!state || !state->valid) return IntrinsicResult::Null;
#ifdef PLATFORM_WEB
		if (state->webPlayer) state->timeLength = WebVideoGetTimeLength(state->webHandle);
#endif
		return IntrinsicResult(state->timeLength);
	};
	raylibModule.SetValue("GetVideoTimeLength", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->code = INTRINSIC_LAMBDA {
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
		if (!state || !state->valid) return IntrinsicResult::Null;
#ifdef PLATFORM_WEB
		if (state->webPlayer) state->timePlayed = WebVideoGetTimePlayed(state->webHandle);
#endif
		return IntrinsicResult(state->timePlayed);
	};
	raylibModule.SetValue("GetVideoTimePlayed", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->code = INTRINSIC_LAMBDA {
		Value videoVal = context->GetVar(String("video"));
		if (videoVal.type != ValueType::Map) return IntrinsicResult::Null;
		ValueDict map = videoVal.GetDict();
		return IntrinsicResult(map.Lookup(String("texture"), Value::null));
	};
	raylibModule.SetValue("GetVideoTexture", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->code = INTRINSIC_LAMBDA {
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
		if (!state || !state->valid) return IntrinsicResult::Null;

		ValueDict info;
		info.SetValue(String("path"), Value(String(state->path.c_str())));
		info.SetValue(String("container"), Value(String(state->container.c_str())));
		info.SetValue(String("codec"), Value(String(state->codecName.c_str())));
		info.SetValue(String("width"), Value(state->width));
		info.SetValue(String("height"), Value(state->height));
		info.SetValue(String("frameRate"), Value(state->frameRate));
		info.SetValue(String("frameCount"), Value((int)state->frameCount));
		info.SetValue(String("timeLength"), Value(state->timeLength));
		info.SetValue(String("playbackRate"), Value(state->playbackRate));
		info.SetValue(String("looping"), Value(state->looping ? 1 : 0));
		info.SetValue(String("isWebBackend"), Value(state->webPlayer ? 1 : 0));
		return IntrinsicResult(Value(info));
	};
	raylibModule.SetValue("GetVideoInfo", i->GetFunc());

	// Alias for readability.
	i = Intrinsic::Create("");
	i->AddParam("video");
	i->code = INTRINSIC_LAMBDA {
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
		if (!state || !state->valid) return IntrinsicResult::Null;

		ValueDict info;
		info.SetValue(String("path"), Value(String(state->path.c_str())));
		info.SetValue(String("container"), Value(String(state->container.c_str())));
		info.SetValue(String("codec"), Value(String(state->codecName.c_str())));
		info.SetValue(String("width"), Value(state->width));
		info.SetValue(String("height"), Value(state->height));
		info.SetValue(String("frameRate"), Value(state->frameRate));
		info.SetValue(String("frameCount"), Value((int)state->frameCount));
		info.SetValue(String("timeLength"), Value(state->timeLength));
		info.SetValue(String("playbackRate"), Value(state->playbackRate));
		info.SetValue(String("looping"), Value(state->looping ? 1 : 0));
		info.SetValue(String("isWebBackend"), Value(state->webPlayer ? 1 : 0));
		return IntrinsicResult(Value(info));
	};
	raylibModule.SetValue("GetVideoMetadata", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->code = INTRINSIC_LAMBDA {
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
		if (!state || !state->valid) return IntrinsicResult::Null;
		return IntrinsicResult(Value(String(state->webPlayer ? "web" : "desktop")));
	};
	raylibModule.SetValue("GetVideoBackend", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->AddParam("enabled", Value(1));
	i->code = INTRINSIC_LAMBDA {
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
		if (!state || !state->valid) return IntrinsicResult::Null;
		int enabled = context->GetVar(String("enabled")).IntValue();
		state->looping = (enabled != 0);
#ifdef PLATFORM_WEB
		if (state->webPlayer) WebVideoSetLooping(state->webHandle, state->looping ? 1 : 0);
#endif
		return IntrinsicResult::Null;
	};
	raylibModule.SetValue("SetVideoLooping", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->code = INTRINSIC_LAMBDA {
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
		if (!state || !state->valid) return IntrinsicResult::Null;
#ifdef PLATFORM_WEB
		if (state->webPlayer) state->looping = (WebVideoGetLooping(state->webHandle) != 0);
#endif
		return IntrinsicResult(state->looping ? 1 : 0);
	};
	raylibModule.SetValue("GetVideoLooping", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->AddParam("rate", Value(1.0));
	i->code = INTRINSIC_LAMBDA {
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
		if (!state || !state->valid) return IntrinsicResult::Null;
		double rate = context->GetVar(String("rate")).DoubleValue();
		if (rate < 0.05) rate = 0.05;
		if (rate > 4.0) rate = 4.0;
		state->playBaseTime = state->timePlayed;
		state->playBaseClock = GetTime();
		state->playbackRate = rate;
#ifdef PLATFORM_WEB
		if (state->webPlayer) WebVideoSetPlaybackRate(state->webHandle, rate);
#endif
		return IntrinsicResult::Null;
	};
	raylibModule.SetValue("SetVideoPlaybackRate", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->code = INTRINSIC_LAMBDA {
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
		if (!state || !state->valid) return IntrinsicResult::Null;
#ifdef PLATFORM_WEB
		if (state->webPlayer) state->playbackRate = WebVideoGetPlaybackRate(state->webHandle);
#endif
		return IntrinsicResult(state->playbackRate);
	};
	raylibModule.SetValue("GetVideoPlaybackRate", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->code = INTRINSIC_LAMBDA {
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
		if (!state || !state->valid) return IntrinsicResult::Null;
		int raised = state->loopEventPending ? 1 : 0;
		state->loopEventPending = false;
		return IntrinsicResult(raised);
	};
	raylibModule.SetValue("DidVideoLoop", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->code = INTRINSIC_LAMBDA {
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
		if (!state || !state->valid) return IntrinsicResult::Null;
		int raised = state->finishEventPending ? 1 : 0;
		state->finishEventPending = false;
		return IntrinsicResult(raised);
	};
	raylibModule.SetValue("DidVideoFinish", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->code = INTRINSIC_LAMBDA {
		Value videoVal = context->GetVar(String("video"));
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(videoVal);
		if (!state) return IntrinsicResult::Null;
		FreeVideoMapTextureHandle(videoVal);
		DestroyVideoPlayer(state);
		if (videoVal.type == ValueType::Map) {
			ValueDict map = videoVal.GetDict();
			map.SetValue(String("_handle"), Value::zero);
			map.SetValue(String("isPlaying"), Value::zero);
			map.SetValue(String("isFinished"), Value(1));
		}
		return IntrinsicResult::Null;
	};
	raylibModule.SetValue("UnloadVideoStream", i->GetFunc());
}
