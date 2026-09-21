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
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>
#include <cstring>

#ifdef PLATFORM_WEB
#include <emscripten/emscripten.h>
#endif

#ifndef HAVE_LIBVPX
#define HAVE_LIBVPX 0
#endif

#ifndef HAVE_LIBVORBIS
#define HAVE_LIBVORBIS 0
#endif

#if HAVE_LIBVPX
#include <vpx/vpx_decoder.h>
#include <vpx/vp8dx.h>
#endif

#if HAVE_LIBVORBIS
#include <vorbis/codec.h>
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
	std::string lastError;
	std::string audioCodecName;
	std::string audioDecodePath;
	bool valid = false;
	bool playing = false;
	bool finished = false;
	bool hasAudio = false;
	bool webPlayer = false;
	bool audioDecodeScaffoldReady = false;
	bool vorbisHeaderParseAttempted = false;
	bool vorbisIdentificationSeen = false;
	bool vorbisCommentSeen = false;
	bool vorbisSetupSeen = false;
	bool vorbisHeadersFromCodecPrivate = false;
	bool vorbisHeadersFromPacketStream = false;
	bool vorbisSeedHeaderParseAttempted = false;
	bool vorbisSeedIdentificationSeen = false;
	bool vorbisSeedCommentSeen = false;
	bool vorbisSeedSetupSeen = false;
	bool vorbisSeedHeadersFromCodecPrivate = false;
	int webHandle = 0;
	int width = 0;
	int height = 0;
	int audioChannels = 0;
	int vorbisParsedChannels = 0;
	int vorbisSeedParsedChannels = 0;
	uint32_t audioDecodeSessionId = 0;
	uint32_t audioPacketsRead = 0;
	uint64_t audioBytesRead = 0;
	uint64_t decodedPcmFramesAvailable = 0;
	uint64_t decodedPcmFramesTotal = 0;
	uint64_t decodedPcmFramesConsumed = 0;
	uint64_t decodedPcmFramesClockEstimated = 0;
	uint64_t decodedPcmFramesClockConsumed = 0;
	uint32_t vorbisPacketsDecoded = 0;
	uint32_t autoRefillTriggerCount = 0;
	uint32_t autoRefillPacketsDecoded = 0;
	uint32_t autoRefillTargetHitCount = 0;
	double autoRefillLastLatencyMs = 0.0;
	double autoRefillTargetLatencyMs = 0.0;
	double autoRefillLatencyGainMsTotal = 0.0;
	uint32_t audioPacketCount = 0;
	double audioFirstPacketTime = 0.0;
	double audioLastPacketTime = 0.0;
	double audioLastReadPacketTime = 0.0;
	double decodedPcmDrainRemainder = 0.0;
	double audioSampleRate = 0.0;
	double vorbisParsedSampleRate = 0.0;
	double vorbisSeedParsedSampleRate = 0.0;
	uint32_t frameCount = 0;
	double frameRate = 0.0;
	double timeLength = 0.0;
	double timePlayed = 0.0;
	double playbackRate = 1.0;
	uint32_t currentFrame = 0;
	double playBaseClock = 0.0;
	double playBaseTime = 0.0;
	double lastObservedTime = 0.0;
	std::string syncMode = "wall-clock";
	bool audioLedSyncEnabled = true;
	bool audioSyncOffsetPrimed = false;
	bool audioSyncClampAdaptive = true;
	double audioSyncClampWindowMs = 120.0;
	double audioSyncClampManualWindowMs = 120.0;
	double audioSyncClampAutoWindowMs = 120.0;
	double audioSyncClampRawWindowMs = 120.0;
	double audioSyncClampSmoothingAlpha = 0.20;
	double audioSyncClampMaxStepMs = 12.0;
	double audioSyncOffsetSec = 0.0;
	double lastAudioClockSec = 0.0;
	double lastAudioLedTargetSec = 0.0;
	double audioStreamMediaBaseSec = 0.0;
	double videoAudioSyncSkewMs = 0.0;    // video clock ahead of audio-consumed position (ms); negative = video behind
	double lastDecodedFramePts = 0.0;    // PTS of most recently VP8-decoded frame
	double lastUpdateTargetPts = 0.0;    // targetTime computed in last UpdateVideoStream
	uint32_t totalFramesDecoded = 0;     // cumulative VP8 decode calls (never resets on rewind)
	uint32_t totalFramesSkipped = 0;     // stale frames advanced past without decoding (catch-up)
	uint32_t totalFrameDropEvents = 0;   // times decode budget was exhausted with pending frames
	int lastDecodeBudgetUsed = 0;        // frames decoded in the most recent UpdateVideoStream tick
	bool lastDecodeBudgetExhausted = false; // budget saturated AND frames still pending after last tick
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
	std::vector<EncodedFrame> audioPackets;
	size_t nextFrameIndex = 0;
	size_t nextAudioPacketIndex = 0;
#endif
	// Raw Vorbis header packets extracted from CodecPrivate (always three: id, comment, setup)
	std::vector<std::vector<unsigned char>> vorbisCodecPrivateHeaders;

#if HAVE_LIBVORBIS
	vorbis_info      vorbisInfo;
	vorbis_comment   vorbisComment;
	vorbis_dsp_state vorbisState;
	vorbis_block     vorbisBlock;
	bool vorbisDecoderInited = false;
	bool vorbisBlockInited = false;
	// Audio stream for real playback
	AudioStream audioStream = {0};
	bool audioStreamInited = false;
	bool audioStreamPlaying = false;
	// Interleaved float PCM buffer awaiting consumption by the audio callback
	std::vector<float> pcmPlaybackBuffer;
	size_t pcmPlaybackHead = 0;
	uint64_t pcmFramesFedToStream = 0;
	std::mutex pcmPlaybackMutex;
	int audioStreamBufferFrames = 2048;  // used for refill sizing; actual playback is callback-driven
#endif
};

static std::string gLastVideoLoadError;

#if HAVE_LIBVORBIS
static std::atomic<VideoPlayerState*> gActiveAudioCallbackState { nullptr };

static uint64_t GetBufferedPcmFramesLocked(const VideoPlayerState* state) {
	if (!state) return 0;
	int ch = state->audioChannels > 0 ? state->audioChannels : 1;
	if (ch <= 0) ch = 1;
	size_t availableFloats = (state->pcmPlaybackHead < state->pcmPlaybackBuffer.size())
		? state->pcmPlaybackBuffer.size() - state->pcmPlaybackHead : 0;
	return (uint64_t)(availableFloats / (size_t)ch);
}

static void CompactPcmPlaybackBufferLocked(VideoPlayerState* state) {
	if (!state) return;
	if (state->pcmPlaybackHead >= state->pcmPlaybackBuffer.size() / 2) {
		state->pcmPlaybackBuffer.erase(
			state->pcmPlaybackBuffer.begin(),
			state->pcmPlaybackBuffer.begin() + (ptrdiff_t)state->pcmPlaybackHead);
		state->pcmPlaybackHead = 0;
	}
	state->decodedPcmFramesAvailable = GetBufferedPcmFramesLocked(state);
}

static void RefreshDecodedPcmFramesAvailable(VideoPlayerState* state) {
	if (!state) return;
	std::lock_guard<std::mutex> lock(state->pcmPlaybackMutex);
	state->decodedPcmFramesAvailable = GetBufferedPcmFramesLocked(state);
}

static void SetActiveAudioCallbackState(VideoPlayerState* state) {
	gActiveAudioCallbackState.store(state, std::memory_order_release);
}

static void ClearActiveAudioCallbackState(VideoPlayerState* state) {
	VideoPlayerState* expected = state;
	gActiveAudioCallbackState.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
}

static void VideoAudioStreamCallback(void *bufferData, unsigned int frames) {
	float* out = (float*)bufferData;
	VideoPlayerState* state = gActiveAudioCallbackState.load(std::memory_order_acquire);
	unsigned int channels = 2;
	if (state && state->audioChannels > 0) channels = (unsigned int)state->audioChannels;
	if (channels == 0) channels = 1;
	size_t requestedFloats = (size_t)frames * (size_t)channels;
	memset(out, 0, requestedFloats * sizeof(float));
	if (!state || !state->valid || !state->audioStreamPlaying) return;

	std::lock_guard<std::mutex> lock(state->pcmPlaybackMutex);
	size_t availableFloats = (state->pcmPlaybackHead < state->pcmPlaybackBuffer.size())
		? state->pcmPlaybackBuffer.size() - state->pcmPlaybackHead : 0;
	size_t copyFloats = std::min(requestedFloats, availableFloats);
	if (copyFloats > 0) {
		memcpy(out, state->pcmPlaybackBuffer.data() + state->pcmPlaybackHead, copyFloats * sizeof(float));
		state->pcmPlaybackHead += copyFloats;
		uint64_t copiedFrames = (uint64_t)(copyFloats / (size_t)channels);
		state->pcmFramesFedToStream += copiedFrames;
		state->decodedPcmFramesConsumed += copiedFrames;
		CompactPcmPlaybackBufferLocked(state);
	}
}
#endif

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

static uint32_t ReadBE32(const unsigned char* p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t ReadBE64(const unsigned char* p) {
	return ((uint64_t)p[0] << 56) |
		((uint64_t)p[1] << 48) |
		((uint64_t)p[2] << 40) |
		((uint64_t)p[3] << 32) |
		((uint64_t)p[4] << 24) |
		((uint64_t)p[5] << 16) |
		((uint64_t)p[6] << 8) |
		(uint64_t)p[7];
}

static double ReadBigEndianFloat(const unsigned char* p, size_t sz) {
	if (sz == 4) {
		uint32_t bits = ReadBE32(p);
		float f = 0.0f;
		std::memcpy(&f, &bits, sizeof(float));
		return (double)f;
	}
	if (sz == 8) {
		uint64_t bits = ReadBE64(p);
		double d = 0.0;
		std::memcpy(&d, &bits, sizeof(double));
		return d;
	}
	return 0.0;
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
							 std::vector<EncodedFrame>* outFrames,
							 size_t* outPushedFrames,
							 bool* outUsedLacing) {
	if (outPushedFrames) *outPushedFrames = 0;
	if (outUsedLacing) *outUsedLacing = false;

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
	if (trackNum != wantedTrack) return true;
	if (p >= payloadEnd) return true;
	if (lacing != 0 && outUsedLacing) *outUsedLacing = true;

	uint64_t tc = clusterTimecode + (int64_t)relTc;
	double pts = ((double)tc * (double)timecodeScale) / 1000000000.0;
	size_t pushed = 0;

	if (lacing == 0) {
		EncodedFrame frame;
		frame.offset = (long)p;
		frame.size = (uint32_t)(payloadEnd - p);
		frame.pts = pts;
		outFrames->push_back(frame);
		if (outPushedFrames) *outPushedFrames = 1;
		return true;
	}

	if (p >= payloadEnd) return false;
	uint8_t laceCount = (uint8_t)(fileData[p++] + 1);
	if (laceCount == 0) return false;

	std::vector<size_t> frameSizes;
	frameSizes.reserve((size_t)laceCount);

	if (lacing == 1) {
		for (uint8_t i = 0; i + 1 < laceCount; i++) {
			size_t sz = 0;
			while (true) {
				if (p >= payloadEnd) return false;
				unsigned char b = fileData[p++];
				sz += (size_t)b;
				if (b != 255) break;
			}
			frameSizes.push_back(sz);
		}
	} else if (lacing == 2) {
		size_t remaining = payloadEnd - p;
		if (remaining < (size_t)laceCount) return false;
		if ((remaining % (size_t)laceCount) != 0) return false;
		size_t each = remaining / (size_t)laceCount;
		for (uint8_t i = 0; i < laceCount; i++) frameSizes.push_back(each);
	} else if (lacing == 3) {
		uint64_t firstSize = 0;
		if (!ReadEBMLVint(fileData, &p, 8, true, &firstSize, nullptr, nullptr)) return false;
		frameSizes.push_back((size_t)firstSize);

		for (uint8_t i = 1; i + 1 < laceCount; i++) {
			if (p >= payloadEnd) return false;
			unsigned char first = fileData[p];
			unsigned char mask = 0x80;
			int len = 1;
			while (len <= 8 && (first & mask) == 0) {
				mask >>= 1;
				len += 1;
			}
			if (len > 8) return false;

			uint64_t vint = 0;
			if (!ReadEBMLVint(fileData, &p, 8, true, &vint, nullptr, nullptr)) return false;
			int64_t bias = ((int64_t)1 << (7 * len - 1)) - 1;
			int64_t delta = (int64_t)vint - bias;
			int64_t nextSize = (int64_t)frameSizes.back() + delta;
			if (nextSize < 0) return false;
			frameSizes.push_back((size_t)nextSize);
		}
	} else {
		return false;
	}

	size_t remaining = payloadEnd - p;
	size_t sumKnown = 0;
	for (size_t i = 0; i < frameSizes.size(); i++) {
		sumKnown += frameSizes[i];
		if (sumKnown > remaining) return false;
	}
	if ((size_t)laceCount < frameSizes.size()) return false;
	if (remaining < sumKnown) return false;
	frameSizes.push_back(remaining - sumKnown);

	size_t cursor = 0;
	for (size_t i = 0; i < frameSizes.size(); i++) {
		size_t sz = frameSizes[i];
		if (cursor + sz > remaining) return false;
		if (sz > 0) {
			EncodedFrame frame;
			frame.offset = (long)(p + cursor);
			frame.size = (uint32_t)sz;
			frame.pts = pts + ((double)i * 1e-6);
			outFrames->push_back(frame);
			pushed += 1;
		}
		cursor += sz;
	}

	if (outPushedFrames) *outPushedFrames = pushed;
	return true;
}

static bool ParseVorbisCodecPrivate(const unsigned char* data, size_t size,
	bool* outIdentificationSeen, bool* outCommentSeen, bool* outSetupSeen,
	int* outChannels, double* outSampleRate,
	std::vector<std::vector<unsigned char>>* outHeaders = nullptr) {
	if (outIdentificationSeen) *outIdentificationSeen = false;
	if (outCommentSeen) *outCommentSeen = false;
	if (outSetupSeen) *outSetupSeen = false;
	if (outChannels) *outChannels = 0;
	if (outSampleRate) *outSampleRate = 0.0;
	if (outHeaders) outHeaders->clear();

	if (!data || size < 1) return false;

	int headerCount = (int)data[0] + 1;
	if (headerCount != 3) return false;

	size_t cursor = 1;
	size_t headerSizes[3] = {0, 0, 0};
	for (int i = 0; i < 2; i++) {
		size_t sz = 0;
		while (cursor < size && data[cursor] == 0xFF) {
			sz += 255;
			cursor += 1;
		}
		if (cursor >= size) return false;
		sz += (size_t)data[cursor++];
		headerSizes[i] = sz;
	}

	if (size < cursor + headerSizes[0] + headerSizes[1]) return false;
	headerSizes[2] = size - cursor - headerSizes[0] - headerSizes[1];

	const unsigned char* h1 = data + cursor;
	const unsigned char* h2 = h1 + headerSizes[0];
	const unsigned char* h3 = h2 + headerSizes[1];

	auto hasVorbisSig = [](const unsigned char* p, size_t n, unsigned char expectedType) -> bool {
		if (!p || n < 7) return false;
		if (p[0] != expectedType) return false;
		return p[1] == 'v' && p[2] == 'o' && p[3] == 'r' && p[4] == 'b' && p[5] == 'i' && p[6] == 's';
	};

	bool idSeen = hasVorbisSig(h1, headerSizes[0], 0x01);
	bool commentSeen = hasVorbisSig(h2, headerSizes[1], 0x03);
	bool setupSeen = hasVorbisSig(h3, headerSizes[2], 0x05);

	if (idSeen && headerSizes[0] >= 16) {
		if (outChannels) *outChannels = (int)h1[11];
		if (outSampleRate) *outSampleRate = (double)ReadLE32(&h1[12]);
	}

	if (outHeaders) {
		outHeaders->resize(3);
		(*outHeaders)[0].assign(h1, h1 + headerSizes[0]);
		(*outHeaders)[1].assign(h2, h2 + headerSizes[1]);
		(*outHeaders)[2].assign(h3, h3 + headerSizes[2]);
	}

	if (outIdentificationSeen) *outIdentificationSeen = idSeen;
	if (outCommentSeen) *outCommentSeen = commentSeen;
	if (outSetupSeen) *outSetupSeen = setupSeen;
	return true;
}

static bool ParseWebMFrames(const char* path, int* outW, int* outH, double* outFps, std::vector<EncodedFrame>* outFrames, double* outDuration,
	bool* outHasAudio, std::string* outAudioCodec, double* outAudioSampleRate, int* outAudioChannels,
	std::vector<EncodedFrame>* outAudioPackets,
	bool* outVorbisHeaderParseAttempted, bool* outVorbisIdentificationSeen,
	bool* outVorbisCommentSeen, bool* outVorbisSetupSeen,
	int* outVorbisParsedChannels, double* outVorbisParsedSampleRate,
	bool* outVorbisHeadersFromCodecPrivate,
	std::vector<std::vector<unsigned char>>* outVorbisCodecPrivateHeaders = nullptr) {
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
	uint64_t audioTrack = 0;
	double fps = 0.0;
	int width = 0;
	int height = 0;
	bool hasAudio = false;
	std::string audioCodec;
	double audioSampleRate = 0.0;
	int audioChannels = 0;
	bool vorbisHeaderParseAttempted = false;
	bool vorbisIdentificationSeen = false;
	bool vorbisCommentSeen = false;
	bool vorbisSetupSeen = false;
	bool vorbisHeadersFromCodecPrivate = false;
	int vorbisParsedChannels = 0;
	double vorbisParsedSampleRate = 0.0;
	double audioFirstPacketTime = 0.0;
	double audioLastPacketTime = 0.0;
	bool sawAudioPacket = false;
	std::vector<EncodedFrame> frames;
	std::vector<EncodedFrame> audioPackets;
	size_t malformedBlocks = 0;
	size_t lacedBlocks = 0;
	size_t lacedFrames = 0;

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
					double asr = 0.0;
					int ach = 0;
					std::vector<unsigned char> codecPrivate;

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
						} else if (tid == 0x63A2ULL) {
							codecPrivate.assign(fileData.begin() + (ptrdiff_t)ts, fileData.begin() + (ptrdiff_t)te);
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
						} else if (tid == 0xE1ULL) {
							size_t ap = ts;
							while (ap < te) {
								uint64_t aid = 0;
								uint64_t asz = 0;
								bool aunk = false;
								if (!ReadEBMLVint(fileData, &ap, 4, false, &aid, nullptr, nullptr)) break;
								if (!ReadEBMLVint(fileData, &ap, 8, true, &asz, nullptr, &aunk)) break;
								size_t aps = ap;
								size_t ape = aunk ? te : (aps + (size_t)asz);
								if (ape > te) break;
								if (aid == 0xB5ULL && (asz == 4 || asz == 8)) {
									asr = ReadBigEndianFloat(&fileData[aps], (size_t)asz);
								} else if (aid == 0x9FULL && asz > 0 && asz <= 8) {
									uint64_t v = 0;
									for (size_t i = 0; i < (size_t)asz; i++) v = (v << 8) | fileData[aps + i];
									ach = (int)v;
								}
								ap = ape;
							}
						}

						tp = te;
					}

					if (tt == 1 && codec == "V_VP8") {
						videoTrack = tn;
						if (tw > 0) width = tw;
						if (th > 0) height = th;
						if (defaultDuration > 0) fps = 1000000000.0 / (double)defaultDuration;
					} else if (tt == 2) {
						hasAudio = true;
						if (audioTrack == 0) audioTrack = tn;
						if (!codec.empty()) audioCodec = codec;
						if (codec == "A_VORBIS" && !codecPrivate.empty()) {
							bool idSeen = false;
							bool commentSeen = false;
							bool setupSeen = false;
							int parsedChannels = 0;
							double parsedSampleRate = 0.0;
							vorbisHeaderParseAttempted = true;
							if (ParseVorbisCodecPrivate(codecPrivate.data(), codecPrivate.size(),
								&idSeen, &commentSeen, &setupSeen, &parsedChannels, &parsedSampleRate,
								outVorbisCodecPrivateHeaders)) {
								vorbisIdentificationSeen = vorbisIdentificationSeen || idSeen;
								vorbisCommentSeen = vorbisCommentSeen || commentSeen;
								vorbisSetupSeen = vorbisSetupSeen || setupSeen;
								if (parsedChannels > 0) vorbisParsedChannels = parsedChannels;
								if (parsedSampleRate > 0.0) vorbisParsedSampleRate = parsedSampleRate;
								if (ach <= 0 && parsedChannels > 0) ach = parsedChannels;
								if (asr <= 0.0 && parsedSampleRate > 0.0) asr = parsedSampleRate;
								if (idSeen && commentSeen && setupSeen) vorbisHeadersFromCodecPrivate = true;
							}
						}
						if (asr > 0.0) audioSampleRate = asr;
						if (ach > 0) audioChannels = ach;
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
					size_t pushed = 0;
					bool usedLacing = false;
					if (!ParseSimpleBlock(fileData, s, (size_t)sz, clusterTimecode, timecodeScale, videoTrack, &frames, &pushed, &usedLacing)) {
						malformedBlocks += 1;
					} else if (usedLacing) {
						lacedBlocks += 1;
						lacedFrames += pushed;
					}
					if (audioTrack != 0) {
						size_t apushed = 0;
						bool alacing = false;
						if (ParseSimpleBlock(fileData, s, (size_t)sz, clusterTimecode, timecodeScale, audioTrack, &audioPackets, &apushed, &alacing)) {
							if (apushed > 0) {
								size_t first = audioPackets.size() - apushed;
								if (!sawAudioPacket) {
									audioFirstPacketTime = audioPackets[first].pts;
									sawAudioPacket = true;
								}
								audioLastPacketTime = audioPackets.back().pts;
							}
						}
					}
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
							size_t pushed = 0;
							bool usedLacing = false;
							if (!ParseSimpleBlock(fileData, bs, (size_t)bsz, clusterTimecode, timecodeScale, videoTrack, &frames, &pushed, &usedLacing)) {
								malformedBlocks += 1;
							} else if (usedLacing) {
								lacedBlocks += 1;
								lacedFrames += pushed;
							}
							if (audioTrack != 0) {
								size_t apushed = 0;
								bool alacing = false;
								if (ParseSimpleBlock(fileData, bs, (size_t)bsz, clusterTimecode, timecodeScale, audioTrack, &audioPackets, &apushed, &alacing)) {
									if (apushed > 0) {
										size_t first = audioPackets.size() - apushed;
										if (!sawAudioPacket) {
											audioFirstPacketTime = audioPackets[first].pts;
											sawAudioPacket = true;
										}
										audioLastPacketTime = audioPackets.back().pts;
									}
								}
							}
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

	if (videoTrack == 0) {
		TraceLog(LOG_WARNING, "LoadVideoStream: WebM parse failed: no VP8 video track found");
		return false;
	}
	if (width <= 0 || height <= 0) {
		TraceLog(LOG_WARNING, "LoadVideoStream: WebM parse failed: invalid video dimensions");
		return false;
	}
	if (frames.empty()) {
		TraceLog(LOG_WARNING, "LoadVideoStream: WebM parse failed: no VP8 frames (malformed blocks: %zu)", malformedBlocks);
		return false;
	}

	std::sort(frames.begin(), frames.end(), [](const EncodedFrame& a, const EncodedFrame& b) {
		return a.pts < b.pts;
	});

	if (fps <= 0.0 && frames.size() >= 2) {
		double span = frames.back().pts - frames.front().pts;
		if (span > 0.0) fps = (double)(frames.size() - 1) / span;
	}
	if (fps <= 0.0) fps = 30.0;
	if (!audioPackets.empty()) {
		std::sort(audioPackets.begin(), audioPackets.end(), [](const EncodedFrame& a, const EncodedFrame& b) {
			return a.pts < b.pts;
		});
	}

	*outW = width;
	*outH = height;
	*outFps = fps;
	*outFrames = frames;
	*outDuration = frames.back().pts + (1.0 / fps);
	if (outHasAudio) *outHasAudio = hasAudio;
	if (outAudioCodec) *outAudioCodec = audioCodec;
	if (outAudioSampleRate) *outAudioSampleRate = audioSampleRate;
	if (outAudioChannels) *outAudioChannels = audioChannels;
	if (outAudioPackets) *outAudioPackets = audioPackets;
	if (outVorbisHeaderParseAttempted) *outVorbisHeaderParseAttempted = vorbisHeaderParseAttempted;
	if (outVorbisIdentificationSeen) *outVorbisIdentificationSeen = vorbisIdentificationSeen;
	if (outVorbisCommentSeen) *outVorbisCommentSeen = vorbisCommentSeen;
	if (outVorbisSetupSeen) *outVorbisSetupSeen = vorbisSetupSeen;
	if (outVorbisParsedChannels) *outVorbisParsedChannels = vorbisParsedChannels;
	if (outVorbisParsedSampleRate) *outVorbisParsedSampleRate = vorbisParsedSampleRate;
	if (outVorbisHeadersFromCodecPrivate) *outVorbisHeadersFromCodecPrivate = vorbisHeadersFromCodecPrivate;
	if (outHasAudio && *outHasAudio && outAudioSampleRate && *outAudioSampleRate <= 0.0 && fps > 0.0) {
		// Keep fields consistent for scripts even when container omits explicit audio sampling metadata.
		*outAudioSampleRate = 0.0;
	}
	if (lacedBlocks > 0) {
		TraceLog(LOG_INFO, "LoadVideoStream: WebM parser accepted %zu laced blocks (%zu decoded frame packets)", lacedBlocks, lacedFrames);
	}
	if (hasAudio && !audioPackets.empty()) {
		TraceLog(LOG_INFO, "LoadVideoStream: WebM parser indexed %zu audio packets (first=%.3f, last=%.3f)", audioPackets.size(), audioFirstPacketTime, audioLastPacketTime);
	}
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

static void ResetAudioDecodeSessionState(VideoPlayerState* state, bool keepSeededHeaders, bool advanceSessionId);
static double GetEffectiveAudioSampleRate(const VideoPlayerState* state);
static bool DecodeDesktopAudioPacketStub(VideoPlayerState* state, uint32_t* outPacketIndex, double* outPacketPts, size_t* outPacketBytes,
	int* outDecodedSamples, int* outDecodedChannels, double* outDecodedSampleRate);
static int AutoRefillDecodedQueue(VideoPlayerState* state, double lowLatencyMs, double targetLatencyMs, int maxPacketsPerTick);

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
	state->syncMode = "wall-clock";
	state->audioSyncOffsetPrimed = false;
	state->audioSyncOffsetSec = 0.0;
	state->lastAudioClockSec = 0.0;
	state->lastAudioLedTargetSec = 0.0;
	state->audioStreamMediaBaseSec = 0.0;
	// Reset per-tick sync fields (lifetime counters totalFramesDecoded/Skipped/DropEvents are preserved)
	state->videoAudioSyncSkewMs = 0.0;
	state->lastDecodedFramePts = 0.0;
	state->lastUpdateTargetPts = 0.0;
	state->lastDecodeBudgetUsed = 0;
	state->lastDecodeBudgetExhausted = false;
#if HAVE_LIBVORBIS
	ClearActiveAudioCallbackState(state);
	if (state->audioStreamInited && state->audioStreamPlaying) {
		StopAudioStream(state->audioStream);
		state->audioStreamPlaying = false;
	}
#endif
	if (state->hasAudio && state->audioDecodeScaffoldReady) {
		ResetAudioDecodeSessionState(state, true, false);
	}
	return true;
}

static bool DecodeFrameAtIndex(VideoPlayerState* state, size_t idx, bool presentFrame = true) {
	if (!state || !state->fp || !state->codecInited || idx >= state->frames.size()) return false;
	const EncodedFrame& f = state->frames[idx];
	if (fseek(state->fp, f.offset, SEEK_SET) != 0) {
		state->lastError = "seek failed while decoding video frame";
		return false;
	}

	std::vector<unsigned char> packet(f.size);
	if (fread(packet.data(), 1, f.size, state->fp) != f.size) {
		state->lastError = "read failed while decoding video frame";
		return false;
	}
	vpx_codec_err_t err = vpx_codec_decode(&state->codec, packet.data(), (unsigned int)packet.size(), nullptr, 0);
	if (err != VPX_CODEC_OK) {
		const char* detail = vpx_codec_error_detail(&state->codec);
		TraceLog(LOG_WARNING, "LoadVideoStream: VP8 decode error at frame %zu: %s%s%s",
			idx,
			vpx_codec_err_to_string(err),
			detail ? " - " : "",
			detail ? detail : "");
		state->lastError = std::string("VP8 decode error at frame ") + std::to_string(idx) + ": " + vpx_codec_err_to_string(err) + (detail ? std::string(" - ") + detail : std::string());
		return false;
	}

	vpx_codec_iter_t iter = nullptr;
	const vpx_image_t* img = vpx_codec_get_frame(&state->codec, &iter);
	if (!img) {
		TraceLog(LOG_WARNING, "LoadVideoStream: VP8 decode produced no frame at index %zu", idx);
		state->lastError = std::string("VP8 decode produced no frame at index ") + std::to_string(idx);
		return false;
	}
	state->lastError.clear();

	ConvertI420ToRGBA(img, state->rgba);
	if (presentFrame && state->texture.id != 0 && !state->rgba.empty()) UpdateTexture(state->texture, state->rgba.data());

	state->currentFrame = (uint32_t)(idx + 1);
	state->timePlayed = f.pts;
	state->nextFrameIndex = idx + 1;
	state->totalFramesDecoded++;
	state->lastDecodedFramePts = f.pts;
	if (state->nextFrameIndex >= state->frames.size()) {
		state->finished = true;
		state->playing = false;
	}
	return true;
}

#if HAVE_LIBVORBIS
static void ClearVorbisDecoder(VideoPlayerState* state) {
	if (!state) return;
	if (state->vorbisBlockInited) {
		vorbis_block_clear(&state->vorbisBlock);
		state->vorbisBlockInited = false;
	}
	if (state->vorbisDecoderInited) {
		vorbis_dsp_clear(&state->vorbisState);
		vorbis_comment_clear(&state->vorbisComment);
		vorbis_info_clear(&state->vorbisInfo);
		state->vorbisDecoderInited = false;
	}
}

static bool InitVorbisDecoderFromHeaders(VideoPlayerState* state) {
	if (!state) return false;
	if (state->vorbisCodecPrivateHeaders.size() != 3) return false;

	ClearVorbisDecoder(state);

	vorbis_info_init(&state->vorbisInfo);
	vorbis_comment_init(&state->vorbisComment);

	for (int i = 0; i < 3; i++) {
		const auto& hdr = state->vorbisCodecPrivateHeaders[i];
		ogg_packet op;
		memset(&op, 0, sizeof(op));
		op.packet    = (unsigned char*)hdr.data();
		op.bytes     = (long)hdr.size();
		op.b_o_s     = (i == 0) ? 1 : 0;
		op.e_o_s     = 0;
		op.granulepos = 0;
		op.packetno  = (ogg_int64_t)i;
		int ret = vorbis_synthesis_headerin(&state->vorbisInfo, &state->vorbisComment, &op);
		if (ret != 0) {
			TraceLog(LOG_WARNING, "InitVorbisDecoderFromHeaders: header packet %d rejected (err=%d)", i, ret);
			vorbis_comment_clear(&state->vorbisComment);
			vorbis_info_clear(&state->vorbisInfo);
			return false;
		}
	}

	if (vorbis_synthesis_init(&state->vorbisState, &state->vorbisInfo) != 0) {
		TraceLog(LOG_WARNING, "InitVorbisDecoderFromHeaders: vorbis_synthesis_init failed");
		vorbis_comment_clear(&state->vorbisComment);
		vorbis_info_clear(&state->vorbisInfo);
		return false;
	}

	vorbis_block_init(&state->vorbisState, &state->vorbisBlock);
	state->vorbisDecoderInited = true;
	state->vorbisBlockInited = true;
	return true;
}

static void FeedAudioStreamFromBuffer(VideoPlayerState* state) {
#if HAVE_LIBVORBIS
	RefreshDecodedPcmFramesAvailable(state);
#else
	(void)state;
#endif
}

static void PrimeAudioPlaybackBuffer(VideoPlayerState* state, double lowLatencyMs = 220.0, double targetLatencyMs = 520.0, int maxPacketsPerTick = 32) {
#if HAVE_LIBVORBIS
	if (!state || !state->audioStreamInited) return;
	if (!state->hasAudio || !state->audioDecodeScaffoldReady) return;
	AutoRefillDecodedQueue(state, lowLatencyMs, targetLatencyMs, maxPacketsPerTick);
	FeedAudioStreamFromBuffer(state);
#else
	(void)state;
	(void)lowLatencyMs;
	(void)targetLatencyMs;
	(void)maxPacketsPerTick;
#endif
}

static void DiscardBufferedAudioFrames(VideoPlayerState* state, uint64_t framesToDiscard) {
#if HAVE_LIBVORBIS
	if (!state || framesToDiscard == 0) return;
	int ch = state->audioChannels > 0 ? state->audioChannels : 1;
	if (ch <= 0) ch = 1;
	std::lock_guard<std::mutex> lock(state->pcmPlaybackMutex);
	size_t availableFloats = (state->pcmPlaybackHead < state->pcmPlaybackBuffer.size())
		? state->pcmPlaybackBuffer.size() - state->pcmPlaybackHead : 0;
	uint64_t availableFrames = (uint64_t)(availableFloats / (size_t)ch);
	if (framesToDiscard > availableFrames) framesToDiscard = availableFrames;
	state->pcmPlaybackHead += (size_t)framesToDiscard * (size_t)ch;
	CompactPcmPlaybackBufferLocked(state);
#else
	(void)state;
	(void)framesToDiscard;
#endif
}

static void SeekAudioDecodeToTime(VideoPlayerState* state, double targetTime) {
	if (!state || !state->hasAudio || !state->audioDecodeScaffoldReady) return;
	if (targetTime < 0.0) targetTime = 0.0;

	ResetAudioDecodeSessionState(state, true, false);
	state->audioStreamMediaBaseSec = targetTime;
	if (targetTime <= 0.0) return;

	double sampleRate = GetEffectiveAudioSampleRate(state);
	if (sampleRate <= 0.0) return;
	uint64_t discardFrames = (uint64_t)llround(targetTime * sampleRate);

	while (state->nextAudioPacketIndex < state->audioPackets.size()
	       && state->audioPackets[state->nextAudioPacketIndex].pts + 0.0005 < targetTime) {
		uint32_t packetIndex = 0;
		double packetPts = 0.0;
		size_t packetBytes = 0;
		int decodedSamples = 0;
		int decodedChannels = 0;
		double decodedSampleRate = 0.0;
		if (!DecodeDesktopAudioPacketStub(state, &packetIndex, &packetPts, &packetBytes,
				&decodedSamples, &decodedChannels, &decodedSampleRate)) {
			break;
		}
		if (decodedSamples > 0 && discardFrames > 0) {
			uint64_t discardNow = (uint64_t)decodedSamples;
			if (discardNow > discardFrames) discardNow = discardFrames;
			DiscardBufferedAudioFrames(state, discardNow);
			discardFrames -= discardNow;
		}
	}
	if (discardFrames > 0) DiscardBufferedAudioFrames(state, discardFrames);
	AutoRefillDecodedQueue(state, 150.0, 400.0, 16);
}

#endif

static void InitializeDesktopAudioDecodeScaffold(VideoPlayerState* state) {
	if (!state) return;
	if (state->audioDecodeSessionId == 0) state->audioDecodeSessionId = 1;
	state->audioDecodeScaffoldReady = false;
	state->audioDecodePath.clear();
	state->vorbisHeaderParseAttempted = false;
	state->vorbisIdentificationSeen = false;
	state->vorbisCommentSeen = false;
	state->vorbisSetupSeen = false;
	state->vorbisHeadersFromCodecPrivate = false;
	state->vorbisHeadersFromPacketStream = false;
	state->vorbisParsedChannels = 0;
	state->vorbisParsedSampleRate = 0.0;
	state->audioPacketsRead = 0;
	state->audioBytesRead = 0;
	state->decodedPcmFramesAvailable = 0;
	state->decodedPcmFramesTotal = 0;
	state->decodedPcmFramesConsumed = 0;
	state->decodedPcmFramesClockEstimated = 0;
	state->decodedPcmFramesClockConsumed = 0;
	state->vorbisPacketsDecoded = 0;
	state->autoRefillTriggerCount = 0;
	state->autoRefillPacketsDecoded = 0;
	state->autoRefillTargetHitCount = 0;
	state->autoRefillLastLatencyMs = 0.0;
	state->autoRefillTargetLatencyMs = 0.0;
	state->autoRefillLatencyGainMsTotal = 0.0;
	state->audioLastReadPacketTime = 0.0;
	state->decodedPcmDrainRemainder = 0.0;
#if HAVE_LIBVPX
	state->nextAudioPacketIndex = 0;
	if (state->webPlayer) return;
	if (!state->hasAudio) return;
	if (state->audioPackets.empty()) return;
	if (state->audioCodecName == "A_VORBIS") {
		state->audioDecodeScaffoldReady = true;
		state->audioDecodePath = "vorbis-packet-reader";
	}
#endif
}

static bool ReadDesktopAudioPacketAtIndex(VideoPlayerState* state, size_t idx, std::vector<unsigned char>* outPacket) {
	if (!state || !outPacket || !state->fp) return false;
	if (idx >= state->audioPackets.size()) return false;
	const EncodedFrame& p = state->audioPackets[idx];
	if (fseek(state->fp, p.offset, SEEK_SET) != 0) {
		state->lastError = "seek failed while reading audio packet";
		return false;
	}
	outPacket->assign((size_t)p.size, 0);
	if (p.size > 0 && fread(outPacket->data(), 1, p.size, state->fp) != p.size) {
		state->lastError = "read failed while reading audio packet";
		return false;
	}
	state->lastError.clear();
	return true;
}

static void UpdateVorbisHeaderScaffold(VideoPlayerState* state, const std::vector<unsigned char>& packet) {
	if (!state) return;
	if (state->audioCodecName != "A_VORBIS") return;
	state->vorbisHeaderParseAttempted = true;
	if (packet.size() < 7) return;
	if (!(packet[1] == 'v' && packet[2] == 'o' && packet[3] == 'r' && packet[4] == 'b' && packet[5] == 'i' && packet[6] == 's')) return;
	state->vorbisHeadersFromPacketStream = true;

	uint8_t headerType = packet[0];
	if (headerType == 0x01) {
		state->vorbisIdentificationSeen = true;
		if (packet.size() >= 16) {
			state->vorbisParsedChannels = (int)packet[11];
			state->vorbisParsedSampleRate = (double)ReadLE32(&packet[12]);
		}
	} else if (headerType == 0x03) {
		state->vorbisCommentSeen = true;
	} else if (headerType == 0x05) {
		state->vorbisSetupSeen = true;
	}
}

static bool IsVorbisHeaderPacket(const std::vector<unsigned char>& packet, uint8_t* outHeaderType = nullptr) {
	if (outHeaderType) *outHeaderType = 0;
	if (packet.size() < 7) return false;
	if (!(packet[1] == 'v' && packet[2] == 'o' && packet[3] == 'r' && packet[4] == 'b' && packet[5] == 'i' && packet[6] == 's')) return false;
	if (outHeaderType) *outHeaderType = packet[0];
	return packet[0] == 0x01 || packet[0] == 0x03 || packet[0] == 0x05;
}

static double GetEffectiveAudioSampleRate(const VideoPlayerState* state) {
	if (!state) return 0.0;
	if (state->audioSampleRate > 0.0) return state->audioSampleRate;
	if (state->vorbisParsedSampleRate > 0.0) return state->vorbisParsedSampleRate;
	return 0.0;
}

static int GetEffectiveAudioChannels(const VideoPlayerState* state) {
	if (!state) return 0;
	if (state->audioChannels > 0) return state->audioChannels;
	if (state->vorbisParsedChannels > 0) return state->vorbisParsedChannels;
	return 0;
}

static double GetAudioStreamClockSec(const VideoPlayerState* state) {
	if (!state) return 0.0;
	double sampleRate = GetEffectiveAudioSampleRate(state);
	if (sampleRate <= 0.0) return state->audioStreamMediaBaseSec;
	return state->audioStreamMediaBaseSec + ((double)state->pcmFramesFedToStream / sampleRate);
}

static void PrimeAudioSyncOffset(VideoPlayerState* state) {
	if (!state) return;
	double audioClockSec = GetAudioStreamClockSec(state);
	state->audioSyncOffsetSec = state->timePlayed - audioClockSec;
	state->audioSyncOffsetPrimed = true;
	state->lastAudioClockSec = audioClockSec;
	state->lastAudioLedTargetSec = audioClockSec + state->audioSyncOffsetSec;
}

static int EstimateVorbisPacketDecodedSamples(const VideoPlayerState* state, size_t packetIndex) {
	if (!state) return 0;
	double sampleRate = GetEffectiveAudioSampleRate(state);
	if (sampleRate <= 0.0) return 0;

	double delta = 0.0;
	if (packetIndex + 1 < state->audioPackets.size()) {
		delta = state->audioPackets[packetIndex + 1].pts - state->audioPackets[packetIndex].pts;
	} else if (packetIndex > 0 && packetIndex < state->audioPackets.size()) {
		delta = state->audioPackets[packetIndex].pts - state->audioPackets[packetIndex - 1].pts;
	}

	if (delta <= 0.0) delta = 0.02;
	if (delta < 0.001) delta = 0.001;
	if (delta > 0.2) delta = 0.2;

	int samples = (int)llround(delta * sampleRate);
	if (samples < 1) samples = 1;
	if (samples > 16384) samples = 16384;
	return samples;
}

static int StepDesktopAudioDecodeScaffold(VideoPlayerState* state, int maxPackets) {
	if (!state || !state->audioDecodeScaffoldReady) return 0;
	if (maxPackets <= 0) return 0;
	int readCount = 0;
	std::vector<unsigned char> packet;
	while (readCount < maxPackets && state->nextAudioPacketIndex < state->audioPackets.size()) {
		if (!ReadDesktopAudioPacketAtIndex(state, state->nextAudioPacketIndex, &packet)) {
			break;
		}
		state->audioPacketsRead += 1;
		state->audioBytesRead += (uint64_t)packet.size();
		state->audioLastReadPacketTime = state->audioPackets[state->nextAudioPacketIndex].pts;
		if (state->audioCodecName == "A_VORBIS") {
			UpdateVorbisHeaderScaffold(state, packet);
		}
		state->nextAudioPacketIndex += 1;
		readCount += 1;
	}
	return readCount;
}

static bool IsAudioDecodeReady(const VideoPlayerState* state) {
	if (!state) return false;
	if (!state->audioDecodeScaffoldReady) return false;
	if (state->audioCodecName == "A_VORBIS") {
		return state->vorbisIdentificationSeen && state->vorbisCommentSeen && state->vorbisSetupSeen;
	}
	return false;
}

static void ResetAudioDecodeSessionState(VideoPlayerState* state, bool keepSeededHeaders, bool advanceSessionId = true) {
	if (!state) return;
	if (advanceSessionId) {
		state->audioDecodeSessionId += 1;
	}
	state->audioPacketsRead = 0;
	state->audioBytesRead = 0;
	state->decodedPcmFramesAvailable = 0;
	state->decodedPcmFramesTotal = 0;
	state->decodedPcmFramesConsumed = 0;
	state->decodedPcmFramesClockEstimated = 0;
	state->decodedPcmFramesClockConsumed = 0;
	state->vorbisPacketsDecoded = 0;
	state->autoRefillTriggerCount = 0;
	state->autoRefillPacketsDecoded = 0;
	state->autoRefillTargetHitCount = 0;
	state->autoRefillLastLatencyMs = 0.0;
	state->autoRefillTargetLatencyMs = 0.0;
	state->autoRefillLatencyGainMsTotal = 0.0;
	state->audioLastReadPacketTime = 0.0;
	state->decodedPcmDrainRemainder = 0.0;
	state->nextAudioPacketIndex = 0;
#if HAVE_LIBVORBIS
	{
		std::lock_guard<std::mutex> lock(state->pcmPlaybackMutex);
		state->pcmPlaybackBuffer.clear();
		state->pcmPlaybackHead = 0;
		state->pcmFramesFedToStream = 0;
	}
	state->audioStreamMediaBaseSec = 0.0;
#endif
	state->vorbisHeaderParseAttempted = false;
	state->vorbisIdentificationSeen = false;
	state->vorbisCommentSeen = false;
	state->vorbisSetupSeen = false;
	state->vorbisHeadersFromCodecPrivate = false;
	state->vorbisHeadersFromPacketStream = false;
	state->vorbisParsedChannels = 0;
	state->vorbisParsedSampleRate = 0.0;

	if (keepSeededHeaders && state->audioCodecName == "A_VORBIS") {
		state->vorbisHeaderParseAttempted = state->vorbisSeedHeaderParseAttempted;
		state->vorbisIdentificationSeen = state->vorbisSeedIdentificationSeen;
		state->vorbisCommentSeen = state->vorbisSeedCommentSeen;
		state->vorbisSetupSeen = state->vorbisSeedSetupSeen;
		state->vorbisHeadersFromCodecPrivate = state->vorbisSeedHeadersFromCodecPrivate;
		if (state->vorbisSeedParsedChannels > 0) state->vorbisParsedChannels = state->vorbisSeedParsedChannels;
		if (state->vorbisSeedParsedSampleRate > 0.0) state->vorbisParsedSampleRate = state->vorbisSeedParsedSampleRate;
	}
#if HAVE_LIBVORBIS
	// Reinitialize the Vorbis decoder so the DSP state is fresh for the new session.
	if (state->vorbisCodecPrivateHeaders.size() == 3) {
		if (InitVorbisDecoderFromHeaders(state)) {
			state->audioDecodePath = "vorbis-libvorbis";
		}
	}
#endif
}

static bool DecodeDesktopAudioPacketStub(VideoPlayerState* state, uint32_t* outPacketIndex, double* outPacketPts, size_t* outPacketBytes,
	int* outDecodedSamples, int* outDecodedChannels, double* outDecodedSampleRate) {
	if (!state) return false;
	if (state->nextAudioPacketIndex >= state->audioPackets.size()) return false;
	std::vector<unsigned char> packet;
	if (!ReadDesktopAudioPacketAtIndex(state, state->nextAudioPacketIndex, &packet)) return false;

	int decodedSamples = 0;
	int decodedChannels = state->audioChannels;
	double decodedSampleRate = state->audioSampleRate;

	if (state->audioCodecName == "A_VORBIS") {
		uint8_t headerType = 0;
		bool isHeaderPacket = IsVorbisHeaderPacket(packet, &headerType);
		UpdateVorbisHeaderScaffold(state, packet);
		if (!isHeaderPacket && IsAudioDecodeReady(state)) {
#if HAVE_LIBVORBIS
			if (state->vorbisDecoderInited) {
				ogg_packet op;
				memset(&op, 0, sizeof(op));
				op.packet    = packet.data();
				op.bytes     = (long)packet.size();
				op.b_o_s     = 0;
				op.e_o_s     = 0;
				op.granulepos = -1;
				op.packetno  = (ogg_int64_t)state->nextAudioPacketIndex;
				if (vorbis_synthesis(&state->vorbisBlock, &op) == 0) {
					vorbis_synthesis_blockin(&state->vorbisState, &state->vorbisBlock);
					float** pcm = nullptr;
					int samples;
					int ch = state->vorbisInfo.channels > 0 ? state->vorbisInfo.channels : 1;
					while ((samples = vorbis_synthesis_pcmout(&state->vorbisState, &pcm)) > 0) {
						// Interleave float PCM channels into playback buffer
						{
							std::lock_guard<std::mutex> lock(state->pcmPlaybackMutex);
							for (int s = 0; s < samples; s++) {
								for (int c = 0; c < ch; c++) {
									state->pcmPlaybackBuffer.push_back(pcm[c][s]);
								}
							}
							state->decodedPcmFramesAvailable = GetBufferedPcmFramesLocked(state);
						}
						decodedSamples += samples;
						vorbis_synthesis_read(&state->vorbisState, samples);
					}
				}
			} else {
				decodedSamples = EstimateVorbisPacketDecodedSamples(state, state->nextAudioPacketIndex);
			}
#else
			decodedSamples = EstimateVorbisPacketDecodedSamples(state, state->nextAudioPacketIndex);
#endif
			if (decodedSamples > 0) {
#if HAVE_LIBVORBIS
				// When real PCM is buffered in pcmPlaybackBuffer, decodedPcmFramesAvailable
				// is kept accurate by FeedAudioStreamFromBuffer; only bump the estimate-path total here.
				if (!state->vorbisDecoderInited) {
					state->decodedPcmFramesAvailable += (uint64_t)decodedSamples;
				}
#else
				state->decodedPcmFramesAvailable += (uint64_t)decodedSamples;
#endif
				state->decodedPcmFramesTotal += (uint64_t)decodedSamples;
				state->vorbisPacketsDecoded += 1;
			}
		}
		if (decodedChannels <= 0 && state->vorbisParsedChannels > 0) decodedChannels = state->vorbisParsedChannels;
		if (decodedSampleRate <= 0.0 && state->vorbisParsedSampleRate > 0.0) decodedSampleRate = state->vorbisParsedSampleRate;
	}

	if (outPacketIndex) *outPacketIndex = (uint32_t)state->nextAudioPacketIndex;
	if (outPacketPts) *outPacketPts = state->audioPackets[state->nextAudioPacketIndex].pts;
	if (outPacketBytes) *outPacketBytes = packet.size();
	if (outDecodedSamples) *outDecodedSamples = decodedSamples;
	if (outDecodedChannels) *outDecodedChannels = decodedChannels;
	if (outDecodedSampleRate) *outDecodedSampleRate = decodedSampleRate;
	state->audioPacketsRead += 1;
	state->audioBytesRead += (uint64_t)packet.size();
	state->audioLastReadPacketTime = state->audioPackets[state->nextAudioPacketIndex].pts;
	state->nextAudioPacketIndex += 1;
	return true;
}

static uint64_t ConsumeDecodedPcmFrames(VideoPlayerState* state, uint64_t wantedFrames) {
	if (!state) return 0;
	if (wantedFrames == 0 || wantedFrames > state->decodedPcmFramesAvailable) wantedFrames = state->decodedPcmFramesAvailable;
	state->decodedPcmFramesAvailable -= wantedFrames;
	state->decodedPcmFramesConsumed += wantedFrames;
	return wantedFrames;
}

static uint64_t ConsumeDecodedPcmFramesByMediaDelta(VideoPlayerState* state, double mediaDeltaSeconds) {
	if (!state) return 0;
	if (mediaDeltaSeconds <= 0.0) return 0;

	double sampleRate = GetEffectiveAudioSampleRate(state);
	if (sampleRate <= 0.0) return 0;

	double exactFrames = mediaDeltaSeconds * sampleRate + state->decodedPcmDrainRemainder;
	if (exactFrames < 1.0) {
		state->decodedPcmDrainRemainder = exactFrames;
		return 0;
	}

	uint64_t wanted = (uint64_t)floor(exactFrames);
	state->decodedPcmDrainRemainder = exactFrames - (double)wanted;
	state->decodedPcmFramesClockEstimated += wanted;
	uint64_t consumed = ConsumeDecodedPcmFrames(state, wanted);
	state->decodedPcmFramesClockConsumed += consumed;
	return consumed;
}

static uint64_t GetDecodedPcmClockDriftFrames(const VideoPlayerState* state) {
	if (!state) return 0;
	if (state->decodedPcmFramesClockEstimated <= state->decodedPcmFramesClockConsumed) return 0;
	return state->decodedPcmFramesClockEstimated - state->decodedPcmFramesClockConsumed;
}

static double GetDecodedQueueLatencyMs(const VideoPlayerState* state) {
	if (!state) return 0.0;
	double sampleRate = GetEffectiveAudioSampleRate(state);
	if (sampleRate <= 0.0) return 0.0;
	return ((double)state->decodedPcmFramesAvailable / sampleRate) * 1000.0;
}

static double ComputeAdaptiveAudioSyncClampWindowMs(const VideoPlayerState* state) {
	if (!state) return 120.0;
	double latencyMs = GetDecodedQueueLatencyMs(state);
	double targetMs = state->autoRefillTargetLatencyMs;
	if (targetMs <= 0.0) targetMs = 140.0;
	double marginMs = latencyMs - targetMs;
	double hitRate = 0.7;
	if (state->autoRefillTriggerCount > 0) {
		hitRate = (double)state->autoRefillTargetHitCount / (double)state->autoRefillTriggerCount;
		if (hitRate < 0.0) hitRate = 0.0;
		if (hitRate > 1.0) hitRate = 1.0;
	}

	double clampMs = targetMs * 0.50;
	if (clampMs < 30.0) clampMs = 30.0;
	if (clampMs > 180.0) clampMs = 180.0;

	if (marginMs < 0.0) {
		clampMs += (-marginMs) * 0.50;
	} else {
		clampMs -= marginMs * 0.10;
	}

	if (hitRate < 0.5) {
		clampMs += 25.0;
	} else if (hitRate > 0.9) {
		clampMs -= 10.0;
	}

	double skewMs = fabs(state->videoAudioSyncSkewMs);
	if (skewMs > 100.0) clampMs += (skewMs - 100.0) * 0.25;

	if (clampMs < 20.0) clampMs = 20.0;
	if (clampMs > 250.0) clampMs = 250.0;
	return clampMs;
}

static double ResolveAudioSyncClampWindowMs(VideoPlayerState* state) {
	if (!state) return 120.0;
	if (!state->audioSyncClampAdaptive) {
		state->audioSyncClampRawWindowMs = state->audioSyncClampManualWindowMs;
		state->audioSyncClampAutoWindowMs = state->audioSyncClampManualWindowMs;
		state->audioSyncClampWindowMs = state->audioSyncClampManualWindowMs;
		return state->audioSyncClampManualWindowMs;
	}

	double rawMs = ComputeAdaptiveAudioSyncClampWindowMs(state);
	state->audioSyncClampRawWindowMs = rawMs;

	double prevMs = state->audioSyncClampAutoWindowMs;
	if (prevMs <= 0.0) prevMs = rawMs;

	double alpha = state->audioSyncClampSmoothingAlpha;
	if (alpha < 0.01) alpha = 0.01;
	if (alpha > 1.00) alpha = 1.00;

	double smoothedMs = prevMs + alpha * (rawMs - prevMs);
	double deltaMs = smoothedMs - prevMs;
	double maxStepMs = state->audioSyncClampMaxStepMs;
	if (maxStepMs < 1.0) maxStepMs = 1.0;
	if (maxStepMs > 100.0) maxStepMs = 100.0;
	if (deltaMs > maxStepMs) deltaMs = maxStepMs;
	if (deltaMs < -maxStepMs) deltaMs = -maxStepMs;

	double effectiveMs = prevMs + deltaMs;
	if (effectiveMs < 20.0) effectiveMs = 20.0;
	if (effectiveMs > 250.0) effectiveMs = 250.0;

	state->audioSyncClampAutoWindowMs = effectiveMs;
	state->audioSyncClampWindowMs = effectiveMs;
	return effectiveMs;
}

static int AutoRefillDecodedQueue(VideoPlayerState* state, double lowLatencyMs, double targetLatencyMs, int maxPacketsPerTick) {
	if (!state) return 0;
	if (maxPacketsPerTick <= 0) return 0;
	if (!state->audioDecodeScaffoldReady) return 0;
	if (state->nextAudioPacketIndex >= state->audioPackets.size()) return 0;

	double latencyMs = GetDecodedQueueLatencyMs(state);
	state->autoRefillLastLatencyMs = latencyMs;
	state->autoRefillTargetLatencyMs = targetLatencyMs;
	if (latencyMs >= lowLatencyMs) return 0;
	state->autoRefillTriggerCount += 1;

	int consumedPackets = 0;
	while (consumedPackets < maxPacketsPerTick && state->nextAudioPacketIndex < state->audioPackets.size()) {
		uint32_t packetIndex = 0;
		double packetPts = 0.0;
		size_t packetBytes = 0;
		int decodedSamples = 0;
		int decodedChannels = 0;
		double decodedSampleRate = 0.0;
		if (!DecodeDesktopAudioPacketStub(state, &packetIndex, &packetPts, &packetBytes, &decodedSamples, &decodedChannels, &decodedSampleRate)) {
			break;
		}
		consumedPackets += 1;
		if (targetLatencyMs > 0.0 && GetDecodedQueueLatencyMs(state) >= targetLatencyMs) break;
	}
	state->autoRefillPacketsDecoded += (uint32_t)consumedPackets;
	double postRefillLatencyMs = GetDecodedQueueLatencyMs(state);
	state->autoRefillLastLatencyMs = postRefillLatencyMs;
	if (postRefillLatencyMs > latencyMs) {
		state->autoRefillLatencyGainMsTotal += (postRefillLatencyMs - latencyMs);
	}
	bool hitTarget = false;
	if (targetLatencyMs > 0.0) {
		hitTarget = (postRefillLatencyMs >= targetLatencyMs);
	} else {
		hitTarget = (consumedPackets > 0);
	}
	if (hitTarget) state->autoRefillTargetHitCount += 1;

	return consumedPackets;
}

static VideoPlayerState* LoadDesktopVideo(const char* path) {
	FILE* fp = fopen(path, "rb");
	if (!fp) {
		gLastVideoLoadError = std::string("could not open file: ") + path;
		return nullptr;
	}
	gLastVideoLoadError.clear();

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
	bool webmHasAudio = false;
	std::string webmAudioCodec;
	double webmAudioSampleRate = 0.0;
	int webmAudioChannels = 0;
	bool webmVorbisHeaderParseAttempted = false;
	bool webmVorbisIdentificationSeen = false;
	bool webmVorbisCommentSeen = false;
	bool webmVorbisSetupSeen = false;
	int webmVorbisParsedChannels = 0;
	double webmVorbisParsedSampleRate = 0.0;
	bool webmVorbisHeadersFromCodecPrivate = false;
	std::vector<std::vector<unsigned char>> webmVorbisCodecPrivateHeaders;
	std::vector<EncodedFrame> webmFrames;
	std::vector<EncodedFrame> webmAudioPackets;

	bool loaded = BuildIVFFrameIndex(fp, &ivfFrames, &ivfW, &ivfH, &ivfFps, &ivfDuration);
	if (loaded) {
		state->container = "ivf";
		state->codecName = "vp8";
		state->width = (int)ivfW;
		state->height = (int)ivfH;
		state->frameRate = ivfFps;
		state->timeLength = ivfDuration;
		state->frames = ivfFrames;
	} else if (ParseWebMFrames(path, &webmW, &webmH, &webmFps, &webmFrames, &webmDuration,
		&webmHasAudio, &webmAudioCodec, &webmAudioSampleRate, &webmAudioChannels, &webmAudioPackets,
		&webmVorbisHeaderParseAttempted, &webmVorbisIdentificationSeen, &webmVorbisCommentSeen,
		&webmVorbisSetupSeen, &webmVorbisParsedChannels, &webmVorbisParsedSampleRate,
		&webmVorbisHeadersFromCodecPrivate, &webmVorbisCodecPrivateHeaders)) {
		state->container = "webm";
		state->codecName = "vp8";
		state->width = webmW;
		state->height = webmH;
		state->frameRate = webmFps;
		state->timeLength = webmDuration;
		state->frames = webmFrames;
		state->hasAudio = webmHasAudio;
		state->audioCodecName = webmAudioCodec;
		state->audioSampleRate = webmAudioSampleRate;
		state->audioChannels = webmAudioChannels;
		state->audioPackets = webmAudioPackets;
		state->audioPacketCount = (uint32_t)state->audioPackets.size();
		if (!state->audioPackets.empty()) {
			state->audioFirstPacketTime = state->audioPackets.front().pts;
			state->audioLastPacketTime = state->audioPackets.back().pts;
		}
	} else {
		TraceLog(LOG_WARNING, "LoadVideoStream: desktop expects VP8 in IVF or WebM (V_VP8) container");
		gLastVideoLoadError = "desktop expects VP8 in IVF or WebM (V_VP8) container";
		fclose(fp);
		delete state;
		return nullptr;
	}

	Image blank = GenImageColor(state->width, state->height, BLACK);
	state->texture = LoadTextureFromImage(blank);
	UnloadImage(blank);
	if (state->texture.id == 0) {
		gLastVideoLoadError = "failed to create video texture";
		fclose(fp);
		delete state;
		return nullptr;
	}

	if (!InitDecoder(state)) {
		gLastVideoLoadError = "failed to initialize VP8 decoder";
		UnloadTexture(state->texture);
		fclose(fp);
		delete state;
		return nullptr;
	}

	state->frameCount = (uint32_t)state->frames.size();
	state->rgba.resize((size_t)state->width * (size_t)state->height * 4u);
	state->playBaseClock = GetTime();
	state->playBaseTime = 0.0;
	state->lastError.clear();
	InitializeDesktopAudioDecodeScaffold(state);
	if (state->audioCodecName == "A_VORBIS") {
		state->vorbisSeedHeaderParseAttempted = webmVorbisHeaderParseAttempted;
		state->vorbisSeedIdentificationSeen = webmVorbisIdentificationSeen;
		state->vorbisSeedCommentSeen = webmVorbisCommentSeen;
		state->vorbisSeedSetupSeen = webmVorbisSetupSeen;
		state->vorbisSeedHeadersFromCodecPrivate = webmVorbisHeadersFromCodecPrivate;
		state->vorbisSeedParsedChannels = webmVorbisParsedChannels;
		state->vorbisSeedParsedSampleRate = webmVorbisParsedSampleRate;
		state->vorbisCodecPrivateHeaders = webmVorbisCodecPrivateHeaders;
		ResetAudioDecodeSessionState(state, true, false);
		if (state->audioChannels <= 0 && state->vorbisParsedChannels > 0) state->audioChannels = state->vorbisParsedChannels;
		if (state->audioSampleRate <= 0.0 && state->vorbisParsedSampleRate > 0.0) state->audioSampleRate = state->vorbisParsedSampleRate;
	}
#if HAVE_LIBVORBIS
	if (state->vorbisDecoderInited && state->audioChannels > 0 && state->audioSampleRate > 0.0) {
		state->audioStream = LoadAudioStream(
			(unsigned int)state->audioSampleRate, 32u, (unsigned int)state->audioChannels);
		if (IsAudioStreamValid(state->audioStream)) {
			SetAudioStreamCallback(state->audioStream, VideoAudioStreamCallback);
			state->audioStreamInited = true;
		}
	}
#endif
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
#if HAVE_LIBVORBIS
	ClearActiveAudioCallbackState(state);
	ClearVorbisDecoder(state);
	if (state->audioStreamInited) {
		if (state->audioStreamPlaying) StopAudioStream(state->audioStream);
		UnloadAudioStream(state->audioStream);
		state->audioStreamInited = false;
		state->audioStreamPlaying = false;
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
	if (handle <= 0 || width <= 0 || height <= 0) {
		gLastVideoLoadError = "web backend failed to load video metadata";
		return nullptr;
	}
	gLastVideoLoadError.clear();

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
		gLastVideoLoadError = "failed to create video texture";
		DestroyVideoPlayer(state);
		return nullptr;
	}

	state->rgba.resize((size_t)state->width * (size_t)state->height * 4u);
	state->lastError.clear();
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
		state->syncMode = "wall-clock";
		state->audioSyncOffsetPrimed = false;
#if HAVE_LIBVORBIS
		if (state->audioStreamInited && !state->audioStreamPlaying) {
			PrimeAudioPlaybackBuffer(state);
			SetActiveAudioCallbackState(state);
			PlayAudioStream(state->audioStream);
			state->audioStreamPlaying = true;
			if (state->audioLedSyncEnabled) PrimeAudioSyncOffset(state);
		}
#endif
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
		state->syncMode = "wall-clock";
#if HAVE_LIBVORBIS
		state->audioSyncOffsetPrimed = false;
#endif
#if HAVE_LIBVORBIS
		if (state->audioStreamPlaying) {
			PauseAudioStream(state->audioStream);
			state->audioStreamPlaying = false;
			ClearActiveAudioCallbackState(state);
		}
#endif
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
			state->syncMode = "wall-clock";
			state->audioSyncOffsetPrimed = false;
#if HAVE_LIBVORBIS
			if (state->audioStreamInited && !state->audioStreamPlaying) {
				PrimeAudioPlaybackBuffer(state);
				SetActiveAudioCallbackState(state);
				ResumeAudioStream(state->audioStream);
				state->audioStreamPlaying = true;
				if (state->audioLedSyncEnabled) PrimeAudioSyncOffset(state);
			}
#endif
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
		state->syncMode = "wall-clock";
		state->audioSyncOffsetPrimed = false;
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
#if HAVE_LIBVORBIS
		ClearActiveAudioCallbackState(state);
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
		SeekAudioDecodeToTime(state, pos);
		state->timePlayed = pos;
		state->finished = false;
#endif
		state->audioSyncOffsetPrimed = false;
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
			double prevMediaTime = state->timePlayed;
			double wallClockTarget = state->playBaseTime + (GetTime() - state->playBaseClock) * state->playbackRate;
			if (wallClockTarget < 0.0) wallClockTarget = 0.0;
			double targetTime = wallClockTarget;
			state->syncMode = "wall-clock";
			state->lastAudioClockSec = 0.0;
			state->lastAudioLedTargetSec = 0.0;

#if HAVE_LIBVORBIS
			if (state->audioLedSyncEnabled && state->audioStreamInited && state->audioStreamPlaying) {
				double syncSampleRate = GetEffectiveAudioSampleRate(state);
				if (syncSampleRate > 0.0) {
					double audioClockSec = GetAudioStreamClockSec(state);
					if (!state->audioSyncOffsetPrimed) PrimeAudioSyncOffset(state);
					double audioLedTarget = audioClockSec + state->audioSyncOffsetSec;
					double clampWindowMs = ResolveAudioSyncClampWindowMs(state);
					double clampWindowSec = clampWindowMs / 1000.0;
					if (clampWindowSec > 0.0) {
						if (audioLedTarget > wallClockTarget + clampWindowSec) audioLedTarget = wallClockTarget + clampWindowSec;
						if (audioLedTarget < wallClockTarget - clampWindowSec) audioLedTarget = wallClockTarget - clampWindowSec;
					}
					if (audioLedTarget < 0.0) audioLedTarget = 0.0;
					targetTime = audioLedTarget;
					state->syncMode = "audio-stream";
					state->lastAudioClockSec = audioClockSec;
					state->lastAudioLedTargetSec = audioLedTarget;
				}
			}
#endif

			if (state->looping && state->timeLength > 0.0) {
				double wrapped = fmod(targetTime, state->timeLength);
				if (wrapped < 0.0) wrapped += state->timeLength;
				if (wrapped + 0.0005 < state->timePlayed) {
					state->loopEventPending = true;
					if (!RewindDesktopVideo(state)) return IntrinsicResult::Null;
#if HAVE_LIBVORBIS
					if (state->audioStreamInited) {
						PrimeAudioPlaybackBuffer(state);
						SetActiveAudioCallbackState(state);
						PlayAudioStream(state->audioStream);
						state->audioStreamPlaying = true;
						if (state->audioLedSyncEnabled) PrimeAudioSyncOffset(state);
					}
#endif
				}
				targetTime = wrapped;
			}

			double oneFrameInterval = (state->frameRate > 1.0) ? (1.0 / state->frameRate) : (1.0 / 30.0);

			int decoded = 0;
			const int decodeBudget = 8;
			while (state->nextFrameIndex < state->frames.size() && state->frames[state->nextFrameIndex].pts <= targetTime + 0.0005 && decoded < decodeBudget) {
				bool staleFrame = state->frames[state->nextFrameIndex].pts < targetTime - oneFrameInterval;
				if (!DecodeFrameAtIndex(state, state->nextFrameIndex, !staleFrame)) {
					state->finished = true;
					state->playing = false;
					break;
				}
				if (staleFrame) state->totalFramesSkipped++;
				decoded += 1;
			}
			state->lastDecodeBudgetUsed = decoded;
			state->lastDecodeBudgetExhausted = (decoded >= decodeBudget)
				&& (state->nextFrameIndex < state->frames.size())
				&& (state->frames[state->nextFrameIndex].pts <= targetTime + 0.0005);
			if (state->lastDecodeBudgetExhausted) state->totalFrameDropEvents++;
			state->lastUpdateTargetPts = targetTime;
			state->timePlayed = targetTime;
			if (state->timeLength > 0.0 && state->timePlayed > state->timeLength) state->timePlayed = state->timeLength;
			double mediaDelta = state->timePlayed - prevMediaTime;
#if HAVE_LIBVORBIS
			if (state->audioStreamInited) {
				if (state->hasAudio && state->audioDecodeScaffoldReady) {
					AutoRefillDecodedQueue(state, 220.0, 520.0, 24);
				}
				FeedAudioStreamFromBuffer(state);
				state->videoAudioSyncSkewMs = (state->timePlayed - GetAudioStreamClockSec(state)) * 1000.0;
			} else {
#endif
			if (mediaDelta > 0.0 && state->decodedPcmFramesAvailable > 0) {
				ConsumeDecodedPcmFramesByMediaDelta(state, mediaDelta);
			}
			// Compute audio-sync skew: how far ahead the video clock is vs audio-consumed position.
			// Positive = video leads audio, 0 = in sync, negative = video behind audio.
			{
				double syncSampleRate = GetEffectiveAudioSampleRate(state);
				if (syncSampleRate > 0.0 && state->decodedPcmFramesConsumed > 0) {
					double audioPositionSec = (double)state->decodedPcmFramesConsumed / syncSampleRate;
					state->videoAudioSyncSkewMs = (state->timePlayed - audioPositionSec) * 1000.0;
				}
			}
			if (state->hasAudio && state->audioDecodeScaffoldReady) {
				// Keep a small decoded queue buffered to emulate real decode+drain stabilization.
				AutoRefillDecodedQueue(state, 60.0, 140.0, 12);
			}
#if HAVE_LIBVORBIS
			}
#endif
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
		info.SetValue(String("hasAudio"), Value(state->hasAudio ? 1 : 0));
		info.SetValue(String("audioCodec"), Value(String(state->audioCodecName.c_str())));
		info.SetValue(String("audioSampleRate"), Value(state->audioSampleRate));
		info.SetValue(String("audioChannels"), Value(state->audioChannels));
		info.SetValue(String("audioPacketCount"), Value((int)state->audioPacketCount));
		info.SetValue(String("audioFirstPacketTime"), Value(state->audioFirstPacketTime));
		info.SetValue(String("audioLastPacketTime"), Value(state->audioLastPacketTime));
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
		info.SetValue(String("hasAudio"), Value(state->hasAudio ? 1 : 0));
		info.SetValue(String("audioCodec"), Value(String(state->audioCodecName.c_str())));
		info.SetValue(String("audioSampleRate"), Value(state->audioSampleRate));
		info.SetValue(String("audioChannels"), Value(state->audioChannels));
		info.SetValue(String("audioPacketCount"), Value((int)state->audioPacketCount));
		info.SetValue(String("audioFirstPacketTime"), Value(state->audioFirstPacketTime));
		info.SetValue(String("audioLastPacketTime"), Value(state->audioLastPacketTime));
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
	i->AddParam("video", Value::null);
	i->code = INTRINSIC_LAMBDA {
		Value videoVal = context->GetVar(String("video"));
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(videoVal);
		if (state && state->valid) {
			return IntrinsicResult(Value(String(state->lastError.c_str())));
		}
		return IntrinsicResult(Value(String(gLastVideoLoadError.c_str())));
	};
	raylibModule.SetValue("GetVideoLastError", i->GetFunc());

	i = Intrinsic::Create("");
	i->code = INTRINSIC_LAMBDA {
		ValueDict info;
		info.SetValue(String("ok"), Value(String("ok")));
		info.SetValue(String("ready"), Value(String("ready")));
		info.SetValue(String("decodeNotWired"), Value(String("decode-not-wired")));
		info.SetValue(String("notReady"), Value(String("not-ready")));
		info.SetValue(String("unsupportedCodec"), Value(String("unsupported-codec")));
		info.SetValue(String("endOfStream"), Value(String("end-of-stream")));
		info.SetValue(String("readFailed"), Value(String("read-failed")));
		info.SetValue(String("sessionMismatch"), Value(String("session-mismatch")));
		info.SetValue(String("webBackend"), Value(String("web-backend")));
		return IntrinsicResult(Value(info));
	};
	raylibModule.SetValue("GetVideoAudioDecodeStatuses", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->code = INTRINSIC_LAMBDA {
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
		if (!state || !state->valid) return IntrinsicResult::Null;

		ValueDict info;
		info.SetValue(String("backend"), Value(String(state->webPlayer ? "web" : "desktop")));
		info.SetValue(String("hasAudio"), Value(state->hasAudio ? 1 : 0));
		info.SetValue(String("codec"), Value(String(state->audioCodecName.c_str())));
		info.SetValue(String("decodePath"), Value(String(state->audioDecodePath.c_str())));
		info.SetValue(String("decodeScaffoldSupported"), Value(state->audioDecodeScaffoldReady ? 1 : 0));
		info.SetValue(String("readyForDecode"), Value(IsAudioDecodeReady(state) ? 1 : 0));
		info.SetValue(String("sessionGuardSupported"), Value(1));
		info.SetValue(String("statusConstantsSupported"), Value(1));
		info.SetValue(String("readinessHelperSupported"), Value(1));
		info.SetValue(String("batchDecodeSupported"), Value(1));
		int decoderWired = (!state->webPlayer && state->audioCodecName == "A_VORBIS") ? 1 : 0;
		info.SetValue(String("decoderWired"), Value(decoderWired));
#if HAVE_LIBVORBIS
		int playbackWiredVal = state->audioStreamInited ? 1 : 0;
		info.SetValue(String("playbackWired"), Value(playbackWiredVal));
		info.SetValue(String("avSyncWired"), Value(playbackWiredVal));
#else
		info.SetValue(String("playbackWired"), Value::zero);
		info.SetValue(String("avSyncWired"), Value::zero);
#endif
		info.SetValue(String("decodedPcmFramesAvailable"), Value((double)state->decodedPcmFramesAvailable));
		info.SetValue(String("decodedPcmFramesTotal"), Value((double)state->decodedPcmFramesTotal));
		info.SetValue(String("decodedPcmFramesConsumed"), Value((double)state->decodedPcmFramesConsumed));
		info.SetValue(String("decodedVorbisPackets"), Value((int)state->vorbisPacketsDecoded));
		info.SetValue(String("status"), Value(String("ok")));
		info.SetValue(String("message"), Value(String("audio decode/playback capability snapshot")));
		return IntrinsicResult(Value(info));
	};
	raylibModule.SetValue("GetVideoAudioDecodeCapabilities", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->code = INTRINSIC_LAMBDA {
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
		if (!state || !state->valid) return IntrinsicResult::Null;

		ValueDict info;
		info.SetValue(String("hasAudio"), Value(state->hasAudio ? 1 : 0));
		info.SetValue(String("packetCount"), Value((int)state->audioPacketCount));
		info.SetValue(String("firstPacketTime"), Value(state->audioFirstPacketTime));
		info.SetValue(String("lastPacketTime"), Value(state->audioLastPacketTime));

		double span = 0.0;
		double minDelta = 0.0;
		double maxDelta = 0.0;
		double avgDelta = 0.0;
		int nonMonotonicCount = 0;
		if (state->audioPackets.size() >= 2) {
			bool firstDelta = true;
			double sumDelta = 0.0;
			for (size_t i = 1; i < state->audioPackets.size(); i++) {
				double d = state->audioPackets[i].pts - state->audioPackets[i - 1].pts;
				if (d < 0.0) nonMonotonicCount += 1;
				if (firstDelta) {
					minDelta = d;
					maxDelta = d;
					firstDelta = false;
				} else {
					if (d < minDelta) minDelta = d;
					if (d > maxDelta) maxDelta = d;
				}
				sumDelta += d;
			}
			avgDelta = sumDelta / (double)(state->audioPackets.size() - 1);
			span = state->audioPackets.back().pts - state->audioPackets.front().pts;
		}

		info.SetValue(String("packetSpan"), Value(span));
		info.SetValue(String("minDelta"), Value(minDelta));
		info.SetValue(String("maxDelta"), Value(maxDelta));
		info.SetValue(String("avgDelta"), Value(avgDelta));
		info.SetValue(String("nonMonotonicCount"), Value(nonMonotonicCount));
		info.SetValue(String("isMonotonic"), Value(nonMonotonicCount == 0 ? 1 : 0));
		return IntrinsicResult(Value(info));
	};
	raylibModule.SetValue("GetVideoAudioIndexDiagnostics", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->AddParam("maxPackets", Value(1));
	i->AddParam("expectedSessionId", Value::zero);
	i->code = INTRINSIC_LAMBDA {
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
		if (!state || !state->valid) return IntrinsicResult::Null;
		int maxPackets = context->GetVar(String("maxPackets")).IntValue();
		int expectedSessionId = context->GetVar(String("expectedSessionId")).IntValue();
		if (maxPackets < 1) maxPackets = 1;
		if (maxPackets > 1024) maxPackets = 1024;

#ifdef PLATFORM_WEB
		(void)maxPackets;
		(void)expectedSessionId;
		return IntrinsicResult::Null;
#else
		if (state->webPlayer) return IntrinsicResult::Null;
		ValueDict info;
		info.SetValue(String("decodeSessionId"), Value((int)state->audioDecodeSessionId));
		info.SetValue(String("supported"), Value(state->audioDecodeScaffoldReady ? 1 : 0));
		info.SetValue(String("codec"), Value(String(state->audioCodecName.c_str())));
		info.SetValue(String("decodePath"), Value(String(state->audioDecodePath.c_str())));
		if (expectedSessionId > 0 && expectedSessionId != (int)state->audioDecodeSessionId) {
			info.SetValue(String("readCount"), Value::zero);
			info.SetValue(String("status"), Value(String("session-mismatch")));
			info.SetValue(String("message"), Value(String("stale decode session id; call GetVideoAudioDecodeState or ResetVideoAudioDecodeSession")));
			info.SetValue(String("totalReadPackets"), Value((int)state->audioPacketsRead));
			info.SetValue(String("totalReadBytes"), Value((double)state->audioBytesRead));
			info.SetValue(String("nextPacketIndex"), Value((int)state->nextAudioPacketIndex));
			info.SetValue(String("lastReadPacketTime"), Value(state->audioLastReadPacketTime));
			info.SetValue(String("remainingPackets"), Value((int)(state->audioPackets.size() - state->nextAudioPacketIndex)));
			info.SetValue(String("vorbisHeaderParseAttempted"), Value(state->vorbisHeaderParseAttempted ? 1 : 0));
			info.SetValue(String("vorbisIdentificationSeen"), Value(state->vorbisIdentificationSeen ? 1 : 0));
			info.SetValue(String("vorbisCommentSeen"), Value(state->vorbisCommentSeen ? 1 : 0));
			info.SetValue(String("vorbisSetupSeen"), Value(state->vorbisSetupSeen ? 1 : 0));
			bool guardVorbisHeadersReady = (state->vorbisIdentificationSeen && state->vorbisCommentSeen && state->vorbisSetupSeen);
			info.SetValue(String("vorbisHeadersReady"), Value(guardVorbisHeadersReady ? 1 : 0));
			info.SetValue(String("vorbisParsedChannels"), Value(state->vorbisParsedChannels));
			info.SetValue(String("vorbisParsedSampleRate"), Value(state->vorbisParsedSampleRate));
			const char* guardHeaderSource = "none";
			if (state->vorbisHeadersFromCodecPrivate && state->vorbisHeadersFromPacketStream) {
				guardHeaderSource = "codecPrivate+packet";
			} else if (state->vorbisHeadersFromCodecPrivate) {
				guardHeaderSource = "codecPrivate";
			} else if (state->vorbisHeadersFromPacketStream) {
				guardHeaderSource = "packet";
			}
			info.SetValue(String("vorbisHeaderSource"), Value(String(guardHeaderSource)));
			std::string guardMissingHeaders;
			if (!state->vorbisIdentificationSeen) guardMissingHeaders += "identification ";
			if (!state->vorbisCommentSeen) guardMissingHeaders += "comment ";
			if (!state->vorbisSetupSeen) guardMissingHeaders += "setup ";
			if (!guardMissingHeaders.empty() && guardMissingHeaders.back() == ' ') guardMissingHeaders.pop_back();
			info.SetValue(String("vorbisMissingHeaders"), Value(String(guardMissingHeaders.c_str())));
			int guardReadyForDecode = 0;
			if (state->audioDecodeScaffoldReady && state->audioCodecName == "A_VORBIS" && guardVorbisHeadersReady) guardReadyForDecode = 1;
			info.SetValue(String("readyForDecode"), Value(guardReadyForDecode));
			return IntrinsicResult(Value(info));
		}

		int readCount = StepDesktopAudioDecodeScaffold(state, maxPackets);
		info.SetValue(String("readCount"), Value(readCount));
		info.SetValue(String("totalReadPackets"), Value((int)state->audioPacketsRead));
		info.SetValue(String("totalReadBytes"), Value((double)state->audioBytesRead));
		info.SetValue(String("nextPacketIndex"), Value((int)state->nextAudioPacketIndex));
		info.SetValue(String("lastReadPacketTime"), Value(state->audioLastReadPacketTime));
		info.SetValue(String("remainingPackets"), Value((int)(state->audioPackets.size() - state->nextAudioPacketIndex)));
		info.SetValue(String("vorbisHeaderParseAttempted"), Value(state->vorbisHeaderParseAttempted ? 1 : 0));
		info.SetValue(String("vorbisIdentificationSeen"), Value(state->vorbisIdentificationSeen ? 1 : 0));
		info.SetValue(String("vorbisCommentSeen"), Value(state->vorbisCommentSeen ? 1 : 0));
		info.SetValue(String("vorbisSetupSeen"), Value(state->vorbisSetupSeen ? 1 : 0));
		bool vorbisHeadersReady = (state->vorbisIdentificationSeen && state->vorbisCommentSeen && state->vorbisSetupSeen);
		info.SetValue(String("vorbisHeadersReady"), Value(vorbisHeadersReady ? 1 : 0));
		info.SetValue(String("vorbisParsedChannels"), Value(state->vorbisParsedChannels));
		info.SetValue(String("vorbisParsedSampleRate"), Value(state->vorbisParsedSampleRate));
		const char* headerSource = "none";
		if (state->vorbisHeadersFromCodecPrivate && state->vorbisHeadersFromPacketStream) {
			headerSource = "codecPrivate+packet";
		} else if (state->vorbisHeadersFromCodecPrivate) {
			headerSource = "codecPrivate";
		} else if (state->vorbisHeadersFromPacketStream) {
			headerSource = "packet";
		}
		info.SetValue(String("vorbisHeaderSource"), Value(String(headerSource)));
		std::string missingHeaders;
		if (!state->vorbisIdentificationSeen) missingHeaders += "identification ";
		if (!state->vorbisCommentSeen) missingHeaders += "comment ";
		if (!state->vorbisSetupSeen) missingHeaders += "setup ";
		if (!missingHeaders.empty() && missingHeaders.back() == ' ') missingHeaders.pop_back();
		info.SetValue(String("vorbisMissingHeaders"), Value(String(missingHeaders.c_str())));
		int readyForDecode = 0;
		if (state->audioDecodeScaffoldReady && state->audioCodecName == "A_VORBIS" && vorbisHeadersReady) readyForDecode = 1;
		info.SetValue(String("readyForDecode"), Value(readyForDecode));
		const char* status = "ok";
		const char* message = "audio scaffold step completed";
		if (!state->audioDecodeScaffoldReady) {
			status = "unsupported-codec";
			message = "audio decode path is not scaffolded for this codec";
		} else if (state->nextAudioPacketIndex >= state->audioPackets.size()) {
			status = "end-of-stream";
			message = "no more audio packets to read";
		} else if (readCount == 0 && !state->lastError.empty()) {
			status = "read-failed";
			message = "failed to read audio packet from stream";
		}
		info.SetValue(String("status"), Value(String(status)));
		info.SetValue(String("message"), Value(String(message)));
		return IntrinsicResult(Value(info));
#endif
	};
	raylibModule.SetValue("StepVideoAudioDecodeScaffold", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->AddParam("expectedSessionId", Value::zero);
	i->code = INTRINSIC_LAMBDA {
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
		if (!state || !state->valid) return IntrinsicResult::Null;
		int expectedSessionId = context->GetVar(String("expectedSessionId")).IntValue();

		ValueDict info;
		info.SetValue(String("codec"), Value(String(state->audioCodecName.c_str())));
		info.SetValue(String("decodePath"), Value(String(state->audioDecodePath.c_str())));
		info.SetValue(String("supported"), Value(state->audioDecodeScaffoldReady ? 1 : 0));
		int ready = IsAudioDecodeReady(state) ? 1 : 0;
		info.SetValue(String("readyForDecode"), Value(ready));
		info.SetValue(String("consumedPacket"), Value::zero);
		info.SetValue(String("packetIndex"), Value(-1));
		info.SetValue(String("packetPts"), Value(0.0));
		info.SetValue(String("packetBytes"), Value::zero);
		info.SetValue(String("decodedSamples"), Value::zero);
		info.SetValue(String("decodedChannels"), Value(state->audioChannels));
		info.SetValue(String("decodedSampleRate"), Value(state->audioSampleRate));
		info.SetValue(String("decodeSessionId"), Value((int)state->audioDecodeSessionId));
		info.SetValue(String("totalReadPackets"), Value((int)state->audioPacketsRead));
		info.SetValue(String("totalReadBytes"), Value((double)state->audioBytesRead));
		info.SetValue(String("decodedPcmFramesAvailable"), Value((double)state->decodedPcmFramesAvailable));
		info.SetValue(String("remainingPackets"), Value((int)(state->audioPackets.size() - state->nextAudioPacketIndex)));

#ifdef PLATFORM_WEB
		info.SetValue(String("status"), Value(String("web-backend")));
		info.SetValue(String("message"), Value(String("audio decode stub is desktop-only")));
		return IntrinsicResult(Value(info));
#else
		if (state->webPlayer) {
			info.SetValue(String("status"), Value(String("web-backend")));
			info.SetValue(String("message"), Value(String("audio decode stub is desktop-only")));
			return IntrinsicResult(Value(info));
		}
		if (expectedSessionId > 0 && expectedSessionId != (int)state->audioDecodeSessionId) {
			info.SetValue(String("status"), Value(String("session-mismatch")));
			info.SetValue(String("message"), Value(String("stale decode session id; call GetVideoAudioDecodeState or ResetVideoAudioDecodeSession")));
			return IntrinsicResult(Value(info));
		}
		if (!state->audioDecodeScaffoldReady) {
			info.SetValue(String("status"), Value(String("unsupported-codec")));
			info.SetValue(String("message"), Value(String("audio decode path is not scaffolded for this codec")));
			return IntrinsicResult(Value(info));
		}
		if (!ready) {
			info.SetValue(String("status"), Value(String("not-ready")));
			info.SetValue(String("message"), Value(String("audio headers are incomplete; decoder not ready")));
			return IntrinsicResult(Value(info));
		}
		if (state->nextAudioPacketIndex >= state->audioPackets.size()) {
			info.SetValue(String("status"), Value(String("end-of-stream")));
			info.SetValue(String("message"), Value(String("no more audio packets to consume")));
			return IntrinsicResult(Value(info));
		}

		uint32_t packetIndex = 0;
		double packetPts = 0.0;
		size_t packetBytes = 0;
		int decodedSamples = 0;
		int decodedChannels = state->audioChannels;
		double decodedSampleRate = state->audioSampleRate;
		if (!DecodeDesktopAudioPacketStub(state, &packetIndex, &packetPts, &packetBytes, &decodedSamples, &decodedChannels, &decodedSampleRate)) {
			info.SetValue(String("status"), Value(String("read-failed")));
			info.SetValue(String("message"), Value(String("failed to read audio packet from stream")));
			return IntrinsicResult(Value(info));
		}

		info.SetValue(String("consumedPacket"), Value(1));
		info.SetValue(String("packetIndex"), Value((int)packetIndex));
		info.SetValue(String("packetPts"), Value(packetPts));
		info.SetValue(String("packetBytes"), Value((int)packetBytes));
		info.SetValue(String("decodedSamples"), Value(decodedSamples));
		info.SetValue(String("decodedChannels"), Value(decodedChannels));
		info.SetValue(String("decodedSampleRate"), Value(decodedSampleRate));
#if HAVE_LIBVORBIS
		if (state->vorbisDecoderInited) {
			info.SetValue(String("status"), Value(String("ok")));
			info.SetValue(String("message"), Value(String("packet decoded by libvorbis")));
		} else {
			info.SetValue(String("status"), Value(String("decode-not-wired")));
			info.SetValue(String("message"), Value(String("packet consumed; Vorbis packet decode internals staged, PCM output not wired")));
		}
#else
		info.SetValue(String("status"), Value(String("decode-not-wired")));
		info.SetValue(String("message"), Value(String("packet consumed; Vorbis packet decode internals staged, PCM output not wired")));
#endif
		info.SetValue(String("totalReadPackets"), Value((int)state->audioPacketsRead));
		info.SetValue(String("totalReadBytes"), Value((double)state->audioBytesRead));
		info.SetValue(String("decodedPcmFramesAvailable"), Value((double)state->decodedPcmFramesAvailable));
		info.SetValue(String("remainingPackets"), Value((int)(state->audioPackets.size() - state->nextAudioPacketIndex)));
		return IntrinsicResult(Value(info));
#endif
	};
	raylibModule.SetValue("DecodeVideoAudioPacket", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->AddParam("maxPackets", Value(4));
	i->AddParam("expectedSessionId", Value::zero);
	i->code = INTRINSIC_LAMBDA {
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
		if (!state || !state->valid) return IntrinsicResult::Null;

		int maxPackets = context->GetVar(String("maxPackets")).IntValue();
		int expectedSessionId = context->GetVar(String("expectedSessionId")).IntValue();
		if (maxPackets < 1) maxPackets = 1;
		if (maxPackets > 1024) maxPackets = 1024;

		ValueList results;
		auto makeBaseResult = [&](VideoPlayerState* s) {
			ValueDict info;
			info.SetValue(String("codec"), Value(String(s->audioCodecName.c_str())));
			info.SetValue(String("decodePath"), Value(String(s->audioDecodePath.c_str())));
			info.SetValue(String("supported"), Value(s->audioDecodeScaffoldReady ? 1 : 0));
			int ready = IsAudioDecodeReady(s) ? 1 : 0;
			info.SetValue(String("readyForDecode"), Value(ready));
			info.SetValue(String("consumedPacket"), Value::zero);
			info.SetValue(String("packetIndex"), Value(-1));
			info.SetValue(String("packetPts"), Value(0.0));
			info.SetValue(String("packetBytes"), Value::zero);
			info.SetValue(String("decodedSamples"), Value::zero);
			info.SetValue(String("decodedChannels"), Value(s->audioChannels));
			info.SetValue(String("decodedSampleRate"), Value(s->audioSampleRate));
			info.SetValue(String("decodeSessionId"), Value((int)s->audioDecodeSessionId));
			info.SetValue(String("totalReadPackets"), Value((int)s->audioPacketsRead));
			info.SetValue(String("totalReadBytes"), Value((double)s->audioBytesRead));
			info.SetValue(String("decodedPcmFramesAvailable"), Value((double)s->decodedPcmFramesAvailable));
			info.SetValue(String("remainingPackets"), Value((int)(s->audioPackets.size() - s->nextAudioPacketIndex)));
			return info;
		};

#ifdef PLATFORM_WEB
		ValueDict info = makeBaseResult(state);
		info.SetValue(String("status"), Value(String("web-backend")));
		info.SetValue(String("message"), Value(String("audio decode stub is desktop-only")));
		results.Add(Value(info));
		return IntrinsicResult(Value(results));
#else
		if (state->webPlayer) {
			ValueDict info = makeBaseResult(state);
			info.SetValue(String("status"), Value(String("web-backend")));
			info.SetValue(String("message"), Value(String("audio decode stub is desktop-only")));
			results.Add(Value(info));
			return IntrinsicResult(Value(results));
		}
		if (expectedSessionId > 0 && expectedSessionId != (int)state->audioDecodeSessionId) {
			ValueDict info = makeBaseResult(state);
			info.SetValue(String("status"), Value(String("session-mismatch")));
			info.SetValue(String("message"), Value(String("stale decode session id; call GetVideoAudioDecodeState or ResetVideoAudioDecodeSession")));
			results.Add(Value(info));
			return IntrinsicResult(Value(results));
		}

		for (int iter = 0; iter < maxPackets; iter++) {
			ValueDict info = makeBaseResult(state);
			int ready = IsAudioDecodeReady(state) ? 1 : 0;

			if (!state->audioDecodeScaffoldReady) {
				info.SetValue(String("status"), Value(String("unsupported-codec")));
				info.SetValue(String("message"), Value(String("audio decode path is not scaffolded for this codec")));
				results.Add(Value(info));
				break;
			}
			if (!ready) {
				info.SetValue(String("status"), Value(String("not-ready")));
				info.SetValue(String("message"), Value(String("audio headers are incomplete; decoder not ready")));
				results.Add(Value(info));
				break;
			}
			if (state->nextAudioPacketIndex >= state->audioPackets.size()) {
				info.SetValue(String("status"), Value(String("end-of-stream")));
				info.SetValue(String("message"), Value(String("no more audio packets to consume")));
				results.Add(Value(info));
				break;
			}

			uint32_t packetIndex = 0;
			double packetPts = 0.0;
			size_t packetBytes = 0;
			int decodedSamples = 0;
			int decodedChannels = state->audioChannels;
			double decodedSampleRate = state->audioSampleRate;
			if (!DecodeDesktopAudioPacketStub(state, &packetIndex, &packetPts, &packetBytes, &decodedSamples, &decodedChannels, &decodedSampleRate)) {
				info.SetValue(String("status"), Value(String("read-failed")));
				info.SetValue(String("message"), Value(String("failed to read audio packet from stream")));
				results.Add(Value(info));
				break;
			}

			info.SetValue(String("consumedPacket"), Value(1));
			info.SetValue(String("packetIndex"), Value((int)packetIndex));
			info.SetValue(String("packetPts"), Value(packetPts));
			info.SetValue(String("packetBytes"), Value((int)packetBytes));
			info.SetValue(String("decodedSamples"), Value(decodedSamples));
			info.SetValue(String("decodedChannels"), Value(decodedChannels));
			info.SetValue(String("decodedSampleRate"), Value(decodedSampleRate));
#if HAVE_LIBVORBIS
			if (state->vorbisDecoderInited) {
				info.SetValue(String("status"), Value(String("ok")));
				info.SetValue(String("message"), Value(String("packet decoded by libvorbis")));
			} else {
				info.SetValue(String("status"), Value(String("decode-not-wired")));
				info.SetValue(String("message"), Value(String("packet consumed; Vorbis packet decode internals staged, PCM output not wired")));
			}
#else
			info.SetValue(String("status"), Value(String("decode-not-wired")));
			info.SetValue(String("message"), Value(String("packet consumed; Vorbis packet decode internals staged, PCM output not wired")));
#endif
			info.SetValue(String("totalReadPackets"), Value((int)state->audioPacketsRead));
			info.SetValue(String("totalReadBytes"), Value((double)state->audioBytesRead));
			info.SetValue(String("decodedPcmFramesAvailable"), Value((double)state->decodedPcmFramesAvailable));
			info.SetValue(String("remainingPackets"), Value((int)(state->audioPackets.size() - state->nextAudioPacketIndex)));
			results.Add(Value(info));
		}

		return IntrinsicResult(Value(results));
#endif
	};
	raylibModule.SetValue("DecodeVideoAudioPacketBatch", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->AddParam("expectedSessionId", Value::zero);
	i->code = INTRINSIC_LAMBDA {
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
		if (!state || !state->valid) return IntrinsicResult::Null;
		int expectedSessionId = context->GetVar(String("expectedSessionId")).IntValue();

		ValueDict info;
		info.SetValue(String("codec"), Value(String(state->audioCodecName.c_str())));
		info.SetValue(String("decodePath"), Value(String(state->audioDecodePath.c_str())));
		int supported = state->audioDecodeScaffoldReady ? 1 : 0;
		int ready = IsAudioDecodeReady(state) ? 1 : 0;
		int remaining = (int)(state->audioPackets.size() - state->nextAudioPacketIndex);
		if (remaining < 0) remaining = 0;
		int sessionId = (int)state->audioDecodeSessionId;
		int isCurrentSession = 1;
		if (expectedSessionId > 0 && expectedSessionId != sessionId) isCurrentSession = 0;
		info.SetValue(String("supported"), Value(supported));
		info.SetValue(String("readyForDecode"), Value(ready));
		info.SetValue(String("decodeSessionId"), Value(sessionId));
		info.SetValue(String("expectedSessionId"), Value(expectedSessionId));
		info.SetValue(String("isCurrentSession"), Value(isCurrentSession));
		info.SetValue(String("nextPacketIndex"), Value((int)state->nextAudioPacketIndex));
		info.SetValue(String("totalReadPackets"), Value((int)state->audioPacketsRead));
		info.SetValue(String("totalReadBytes"), Value((double)state->audioBytesRead));
		info.SetValue(String("decodedPcmFramesAvailable"), Value((double)state->decodedPcmFramesAvailable));
		info.SetValue(String("decodedPcmFramesTotal"), Value((double)state->decodedPcmFramesTotal));
		info.SetValue(String("decodedPcmFramesConsumed"), Value((double)state->decodedPcmFramesConsumed));
		info.SetValue(String("decodedVorbisPackets"), Value((int)state->vorbisPacketsDecoded));
		info.SetValue(String("remainingPackets"), Value(remaining));
		info.SetValue(String("lastReadPacketTime"), Value(state->audioLastReadPacketTime));
		info.SetValue(String("vorbisHeaderParseAttempted"), Value(state->vorbisHeaderParseAttempted ? 1 : 0));
		info.SetValue(String("vorbisIdentificationSeen"), Value(state->vorbisIdentificationSeen ? 1 : 0));
		info.SetValue(String("vorbisCommentSeen"), Value(state->vorbisCommentSeen ? 1 : 0));
		info.SetValue(String("vorbisSetupSeen"), Value(state->vorbisSetupSeen ? 1 : 0));
		bool vorbisHeadersReady = (state->vorbisIdentificationSeen && state->vorbisCommentSeen && state->vorbisSetupSeen);
		info.SetValue(String("vorbisHeadersReady"), Value(vorbisHeadersReady ? 1 : 0));
		const char* headerSource = "none";
		if (state->vorbisHeadersFromCodecPrivate && state->vorbisHeadersFromPacketStream) {
			headerSource = "codecPrivate+packet";
		} else if (state->vorbisHeadersFromCodecPrivate) {
			headerSource = "codecPrivate";
		} else if (state->vorbisHeadersFromPacketStream) {
			headerSource = "packet";
		}
		info.SetValue(String("vorbisHeaderSource"), Value(String(headerSource)));
		std::string missingHeaders;
		if (!state->vorbisIdentificationSeen) missingHeaders += "identification ";
		if (!state->vorbisCommentSeen) missingHeaders += "comment ";
		if (!state->vorbisSetupSeen) missingHeaders += "setup ";
		if (!missingHeaders.empty() && missingHeaders.back() == ' ') missingHeaders.pop_back();
		info.SetValue(String("vorbisMissingHeaders"), Value(String(missingHeaders.c_str())));

		const char* status = "ready";
		const char* message = "decode state snapshot";
#ifdef PLATFORM_WEB
		status = "web-backend";
		message = "audio decode state is desktop-only";
#else
		if (state->webPlayer) {
			status = "web-backend";
			message = "audio decode state is desktop-only";
		} else if (!isCurrentSession) {
			status = "session-mismatch";
			message = "stale decode session id; call GetVideoAudioDecodeState or ResetVideoAudioDecodeSession";
		} else if (!supported) {
			status = "unsupported-codec";
			message = "audio decode path is not scaffolded for this codec";
		} else if (!ready) {
			status = "not-ready";
			message = "audio headers are incomplete; decoder not ready";
		} else if (remaining <= 0) {
			status = "end-of-stream";
			message = "no more audio packets to consume";
		}
#endif
		info.SetValue(String("status"), Value(String(status)));
		info.SetValue(String("message"), Value(String(message)));
		return IntrinsicResult(Value(info));
	};
	raylibModule.SetValue("GetVideoAudioDecodeState", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->code = INTRINSIC_LAMBDA {
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
		if (!state || !state->valid) return IntrinsicResult::Null;

		ValueDict info;
		info.SetValue(String("codec"), Value(String(state->audioCodecName.c_str())));
		info.SetValue(String("decodePath"), Value(String(state->audioDecodePath.c_str())));
		int supported = state->audioDecodeScaffoldReady ? 1 : 0;
		int ready = IsAudioDecodeReady(state) ? 1 : 0;
		int remaining = (int)(state->audioPackets.size() - state->nextAudioPacketIndex);
		if (remaining < 0) remaining = 0;
		info.SetValue(String("supported"), Value(supported));
		info.SetValue(String("readyForDecode"), Value(ready));
		info.SetValue(String("decodeSessionId"), Value((int)state->audioDecodeSessionId));
		info.SetValue(String("nextPacketIndex"), Value((int)state->nextAudioPacketIndex));
		info.SetValue(String("remainingPackets"), Value(remaining));

		const char* status = "ready";
		const char* message = "decode session snapshot created";
#ifdef PLATFORM_WEB
		status = "web-backend";
		message = "audio decode helper is desktop-only";
#else
		if (state->webPlayer) {
			status = "web-backend";
			message = "audio decode helper is desktop-only";
		} else if (!supported) {
			status = "unsupported-codec";
			message = "audio decode path is not scaffolded for this codec";
		} else if (!ready) {
			status = "not-ready";
			message = "audio headers are incomplete; decoder not ready";
		} else if (remaining <= 0) {
			status = "end-of-stream";
			message = "no more audio packets to consume";
		}
#endif
		info.SetValue(String("status"), Value(String(status)));
		info.SetValue(String("message"), Value(String(message)));
		return IntrinsicResult(Value(info));
	};
	raylibModule.SetValue("CreateVideoAudioDecodeSession", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->AddParam("expectedSessionId", Value::zero);
	i->code = INTRINSIC_LAMBDA {
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
		if (!state || !state->valid) return IntrinsicResult::Null;
		int expectedSessionId = context->GetVar(String("expectedSessionId")).IntValue();

		ValueDict info;
		int supported = state->audioDecodeScaffoldReady ? 1 : 0;
		int ready = IsAudioDecodeReady(state) ? 1 : 0;
		int sessionId = (int)state->audioDecodeSessionId;
		int isCurrentSession = 1;
		if (expectedSessionId > 0 && expectedSessionId != sessionId) isCurrentSession = 0;
		info.SetValue(String("decodeSessionId"), Value(sessionId));
		info.SetValue(String("expectedSessionId"), Value(expectedSessionId));
		info.SetValue(String("isCurrentSession"), Value(isCurrentSession));
		info.SetValue(String("supported"), Value(supported));
		info.SetValue(String("readyForDecode"), Value(ready));

		const char* status = "ready";
		const char* message = "decode ready check";
#ifdef PLATFORM_WEB
		status = "web-backend";
		message = "audio decode readiness helper is desktop-only";
#else
		if (state->webPlayer) {
			status = "web-backend";
			message = "audio decode readiness helper is desktop-only";
		} else if (!isCurrentSession) {
			status = "session-mismatch";
			message = "stale decode session id; call GetVideoAudioDecodeState or ResetVideoAudioDecodeSession";
		} else if (!supported) {
			status = "unsupported-codec";
			message = "audio decode path is not scaffolded for this codec";
		} else if (!ready) {
			status = "not-ready";
			message = "audio headers are incomplete; decoder not ready";
		}
#endif
		info.SetValue(String("status"), Value(String(status)));
		info.SetValue(String("message"), Value(String(message)));
		return IntrinsicResult(Value(info));
	};
	raylibModule.SetValue("IsVideoAudioDecodeReady", i->GetFunc());

		i = Intrinsic::Create("");
		i->AddParam("video");
		i->AddParam("maxFrames", Value::zero);
		i->code = INTRINSIC_LAMBDA {
			VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
			if (!state || !state->valid) return IntrinsicResult::Null;
			int maxFrames = context->GetVar(String("maxFrames")).IntValue();

			ValueDict info;
			info.SetValue(String("decodeSessionId"), Value((int)state->audioDecodeSessionId));
			info.SetValue(String("requestedFrames"), Value(maxFrames));
			info.SetValue(String("consumedFrames"), Value::zero);

	#ifdef PLATFORM_WEB
			info.SetValue(String("status"), Value(String("web-backend")));
			info.SetValue(String("message"), Value(String("decoded PCM frame consumption helper is desktop-only")));
			info.SetValue(String("decodedPcmFramesAvailable"), Value((double)state->decodedPcmFramesAvailable));
			info.SetValue(String("decodedPcmFramesConsumed"), Value((double)state->decodedPcmFramesConsumed));
			return IntrinsicResult(Value(info));
	#else
			if (state->webPlayer) {
				info.SetValue(String("status"), Value(String("web-backend")));
				info.SetValue(String("message"), Value(String("decoded PCM frame consumption helper is desktop-only")));
				info.SetValue(String("decodedPcmFramesAvailable"), Value((double)state->decodedPcmFramesAvailable));
				info.SetValue(String("decodedPcmFramesConsumed"), Value((double)state->decodedPcmFramesConsumed));
				return IntrinsicResult(Value(info));
			}

			uint64_t wanted = 0;
			if (maxFrames > 0) wanted = (uint64_t)maxFrames;
			uint64_t consumed = ConsumeDecodedPcmFrames(state, wanted);
			info.SetValue(String("consumedFrames"), Value((double)consumed));
			info.SetValue(String("decodedPcmFramesAvailable"), Value((double)state->decodedPcmFramesAvailable));
			info.SetValue(String("decodedPcmFramesConsumed"), Value((double)state->decodedPcmFramesConsumed));
			info.SetValue(String("status"), Value(String("ok")));
			info.SetValue(String("message"), Value(String("decoded PCM frames consumed from placeholder queue")));
			return IntrinsicResult(Value(info));
	#endif
		};
		raylibModule.SetValue("ConsumeVideoDecodedPcmFrames", i->GetFunc());

		i = Intrinsic::Create("");
		i->AddParam("video");
		i->code = INTRINSIC_LAMBDA {
			VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
			if (!state || !state->valid) return IntrinsicResult::Null;
			double effectiveSampleRate = GetEffectiveAudioSampleRate(state);
			int effectiveChannels = GetEffectiveAudioChannels(state);
			uint64_t driftFrames = GetDecodedPcmClockDriftFrames(state);
			double refillPacketsPerTrigger = 0.0;
			double refillLatencyGainMs = 0.0;
			double refillTargetHitRate = 0.0;
			if (state->autoRefillTriggerCount > 0) {
				refillPacketsPerTrigger = (double)state->autoRefillPacketsDecoded / (double)state->autoRefillTriggerCount;
				refillLatencyGainMs = state->autoRefillLatencyGainMsTotal / (double)state->autoRefillTriggerCount;
				refillTargetHitRate = (double)state->autoRefillTargetHitCount / (double)state->autoRefillTriggerCount;
				if (refillTargetHitRate < 0.0) refillTargetHitRate = 0.0;
				if (refillTargetHitRate > 1.0) refillTargetHitRate = 1.0;
			}

			ValueDict info;
			info.SetValue(String("decodeSessionId"), Value((int)state->audioDecodeSessionId));
			info.SetValue(String("availableFrames"), Value((double)state->decodedPcmFramesAvailable));
			info.SetValue(String("consumedFrames"), Value((double)state->decodedPcmFramesConsumed));
			info.SetValue(String("totalDecodedFrames"), Value((double)state->decodedPcmFramesTotal));
			info.SetValue(String("clockEstimatedFrames"), Value((double)state->decodedPcmFramesClockEstimated));
			info.SetValue(String("clockConsumedFrames"), Value((double)state->decodedPcmFramesClockConsumed));
			info.SetValue(String("clockDriftFrames"), Value((double)driftFrames));
			info.SetValue(String("decodedVorbisPackets"), Value((int)state->vorbisPacketsDecoded));
			info.SetValue(String("autoRefillTriggerCount"), Value((int)state->autoRefillTriggerCount));
			info.SetValue(String("autoRefillPacketsDecoded"), Value((int)state->autoRefillPacketsDecoded));
			info.SetValue(String("autoRefillTargetHitCount"), Value((int)state->autoRefillTargetHitCount));
			info.SetValue(String("refillPacketsPerTrigger"), Value(refillPacketsPerTrigger));
			info.SetValue(String("refillLatencyGainMs"), Value(refillLatencyGainMs));
			info.SetValue(String("refillTargetHitRate"), Value(refillTargetHitRate));
			info.SetValue(String("sampleRate"), Value(effectiveSampleRate));
			info.SetValue(String("channels"), Value(effectiveChannels));
			info.SetValue(String("syncMode"), Value(String(state->syncMode.c_str())));
			info.SetValue(String("audioLedSyncEnabled"), Value(state->audioLedSyncEnabled ? 1 : 0));
			info.SetValue(String("audioSyncClampWindowMs"), Value(state->audioSyncClampWindowMs));
			info.SetValue(String("audioSyncClampAdaptive"), Value(state->audioSyncClampAdaptive ? 1 : 0));
			info.SetValue(String("audioSyncClampManualWindowMs"), Value(state->audioSyncClampManualWindowMs));
			info.SetValue(String("audioSyncClampAutoWindowMs"), Value(state->audioSyncClampAutoWindowMs));
			info.SetValue(String("audioSyncClampRawWindowMs"), Value(state->audioSyncClampRawWindowMs));
			info.SetValue(String("audioSyncClampSmoothingAlpha"), Value(state->audioSyncClampSmoothingAlpha));
			info.SetValue(String("audioSyncClampMaxStepMs"), Value(state->audioSyncClampMaxStepMs));
			info.SetValue(String("audioSyncOffsetSec"), Value(state->audioSyncOffsetSec));
			info.SetValue(String("audioClockSec"), Value(state->lastAudioClockSec));
			info.SetValue(String("audioLedTargetSec"), Value(state->lastAudioLedTargetSec));

			double latencyMs = 0.0;
			if (effectiveSampleRate > 0.0) latencyMs = ((double)state->decodedPcmFramesAvailable / effectiveSampleRate) * 1000.0;
			info.SetValue(String("latencyMs"), Value(latencyMs));
			double driftMs = 0.0;
			if (effectiveSampleRate > 0.0) driftMs = ((double)driftFrames / effectiveSampleRate) * 1000.0;
			info.SetValue(String("clockDriftMs"), Value(driftMs));
			info.SetValue(String("autoRefillLastLatencyMs"), Value(state->autoRefillLastLatencyMs));
			info.SetValue(String("autoRefillTargetLatencyMs"), Value(state->autoRefillTargetLatencyMs));
			double estimatedBufferingMarginMs = latencyMs - state->autoRefillTargetLatencyMs;
			info.SetValue(String("estimatedBufferingMarginMs"), Value(estimatedBufferingMarginMs));

			// Tuning hints based on refillTargetHitRate distribution
			const char* tuningHint = "insufficient-data";
			double suggestedAdjustmentMs = 0.0;
			int minTriggersForReliableHint = 5;
			
			if (state->autoRefillTriggerCount >= minTriggersForReliableHint) {
				if (refillTargetHitRate >= 0.9) {
					tuningHint = "increase-target";
					// Target is too conservative; suggest increase based on average gain
					suggestedAdjustmentMs = refillLatencyGainMs * 0.5;  // Use 50% of average gain
				} else if (refillTargetHitRate >= 0.5 && refillTargetHitRate < 0.9) {
					tuningHint = "optimal";
					suggestedAdjustmentMs = 0.0;
				} else if (refillTargetHitRate < 0.5) {
					tuningHint = "decrease-target";
					// Target is too aggressive; suggest decrease
					// Use margin as guide: if margin is very negative, reduce more aggressively
					if (estimatedBufferingMarginMs < -50.0) {
						suggestedAdjustmentMs = -40.0;
					} else if (estimatedBufferingMarginMs < -20.0) {
						suggestedAdjustmentMs = -20.0;
					} else {
						suggestedAdjustmentMs = -10.0;
					}
				}
			}
			
			info.SetValue(String("tuningHint"), Value(String(tuningHint)));
			info.SetValue(String("suggestedAdjustmentMs"), Value(suggestedAdjustmentMs));

			const char* status = "ok";
			const char* message = "audio decode queue telemetry";
	#ifdef PLATFORM_WEB
			status = "web-backend";
			message = "audio queue telemetry helper is desktop-only";
	#else
			if (state->webPlayer) {
				status = "web-backend";
				message = "audio queue telemetry helper is desktop-only";
			} else if (!state->hasAudio) {
				status = "not-ready";
				message = "video has no audio stream";
			} else if (!state->audioDecodeScaffoldReady) {
				status = "unsupported-codec";
				message = "audio decode path is not scaffolded for this codec";
			}
	#endif
			info.SetValue(String("status"), Value(String(status)));
			info.SetValue(String("message"), Value(String(message)));
			return IntrinsicResult(Value(info));
		};
		raylibModule.SetValue("GetVideoAudioQueueState", i->GetFunc());

		i = Intrinsic::Create("");
		i->AddParam("video");
		i->AddParam("enabled", Value(1));
		i->AddParam("clampWindowMs", Value(120.0));
		i->AddParam("smoothingAlpha", Value::null);
		i->AddParam("maxStepMs", Value::null);
		i->AddParam("tuning", Value::null);
		i->code = INTRINSIC_LAMBDA {
			VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
			if (!state || !state->valid) return IntrinsicResult::Null;
			int enabled = context->GetVar(String("enabled")).IntValue();
			double clampWindowMs = context->GetVar(String("clampWindowMs")).DoubleValue();
			bool hasClampWindowMs = true;
			bool hasSmoothingAlpha = false;
			bool hasMaxStepMs = false;
			double smoothingAlpha = state->audioSyncClampSmoothingAlpha;
			double maxStepMs = state->audioSyncClampMaxStepMs;

			auto applyTuningMap = [&](ValueDict map) {
				Value vEnabled = map.Lookup(String("enabled"), Value::null);
				if (vEnabled.type != ValueType::Null) enabled = vEnabled.IntValue();
				Value vClamp = map.Lookup(String("clampWindowMs"), Value::null);
				if (vClamp.type != ValueType::Null) {
					clampWindowMs = vClamp.DoubleValue();
					hasClampWindowMs = true;
				}
				Value vAlpha = map.Lookup(String("smoothingAlpha"), Value::null);
				if (vAlpha.type != ValueType::Null) {
					smoothingAlpha = vAlpha.DoubleValue();
					hasSmoothingAlpha = true;
				}
				Value vStep = map.Lookup(String("maxStepMs"), Value::null);
				if (vStep.type != ValueType::Null) {
					maxStepMs = vStep.DoubleValue();
					hasMaxStepMs = true;
				}
			};

			Value clampVal = context->GetVar(String("clampWindowMs"));
			if (clampVal.type == ValueType::Map) {
				applyTuningMap(clampVal.GetDict());
			}

			Value alphaVal = context->GetVar(String("smoothingAlpha"));
			if (alphaVal.type == ValueType::Map) {
				applyTuningMap(alphaVal.GetDict());
			} else if (alphaVal.type != ValueType::Null) {
				smoothingAlpha = alphaVal.DoubleValue();
				hasSmoothingAlpha = true;
			}

			Value stepVal = context->GetVar(String("maxStepMs"));
			if (stepVal.type != ValueType::Null) {
				maxStepMs = stepVal.DoubleValue();
				hasMaxStepMs = true;
			}

			Value tuningVal = context->GetVar(String("tuning"));
			if (tuningVal.type == ValueType::Map) {
				applyTuningMap(tuningVal.GetDict());
			}

			if (clampWindowMs > 1000.0) clampWindowMs = 1000.0;
			state->audioLedSyncEnabled = (enabled != 0);
			if (hasClampWindowMs && clampWindowMs <= 0.0) {
				state->audioSyncClampAdaptive = true;
				state->audioSyncClampWindowMs = state->audioSyncClampAutoWindowMs;
			} else if (hasClampWindowMs) {
				state->audioSyncClampAdaptive = false;
				state->audioSyncClampManualWindowMs = clampWindowMs;
				state->audioSyncClampRawWindowMs = clampWindowMs;
				state->audioSyncClampAutoWindowMs = clampWindowMs;
				state->audioSyncClampWindowMs = clampWindowMs;
			}

			if (hasSmoothingAlpha) {
				if (smoothingAlpha < 0.01) smoothingAlpha = 0.01;
				if (smoothingAlpha > 1.00) smoothingAlpha = 1.00;
				state->audioSyncClampSmoothingAlpha = smoothingAlpha;
			}
			if (hasMaxStepMs) {
				if (maxStepMs < 1.0) maxStepMs = 1.0;
				if (maxStepMs > 100.0) maxStepMs = 100.0;
				state->audioSyncClampMaxStepMs = maxStepMs;
			}
			state->audioSyncOffsetPrimed = false;

			ValueDict info;
			info.SetValue(String("audioLedSyncEnabled"), Value(state->audioLedSyncEnabled ? 1 : 0));
			info.SetValue(String("audioSyncClampWindowMs"), Value(state->audioSyncClampWindowMs));
			info.SetValue(String("audioSyncClampAdaptive"), Value(state->audioSyncClampAdaptive ? 1 : 0));
			info.SetValue(String("audioSyncClampManualWindowMs"), Value(state->audioSyncClampManualWindowMs));
			info.SetValue(String("audioSyncClampAutoWindowMs"), Value(state->audioSyncClampAutoWindowMs));
			info.SetValue(String("audioSyncClampRawWindowMs"), Value(state->audioSyncClampRawWindowMs));
			info.SetValue(String("audioSyncClampSmoothingAlpha"), Value(state->audioSyncClampSmoothingAlpha));
			info.SetValue(String("audioSyncClampMaxStepMs"), Value(state->audioSyncClampMaxStepMs));
			info.SetValue(String("status"), Value(String("ok")));
			info.SetValue(String("message"), Value(String("audio sync tuning updated (clampWindowMs<=0 re-enables adaptive clamp; optional smoothingAlpha/maxStepMs accepted)")));
			return IntrinsicResult(Value(info));
		};
		raylibModule.SetValue("SetVideoAudioSyncTuning", i->GetFunc());

		i = Intrinsic::Create("");
		i->AddParam("video");
		i->code = INTRINSIC_LAMBDA {
			VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
			if (!state || !state->valid) return IntrinsicResult::Null;
			ValueDict info;
			info.SetValue(String("syncMode"), Value(String(state->syncMode.c_str())));
			info.SetValue(String("audioLedSyncEnabled"), Value(state->audioLedSyncEnabled ? 1 : 0));
			info.SetValue(String("audioSyncClampWindowMs"), Value(state->audioSyncClampWindowMs));
			info.SetValue(String("audioSyncClampAdaptive"), Value(state->audioSyncClampAdaptive ? 1 : 0));
			info.SetValue(String("audioSyncClampManualWindowMs"), Value(state->audioSyncClampManualWindowMs));
			info.SetValue(String("audioSyncClampAutoWindowMs"), Value(state->audioSyncClampAutoWindowMs));
			info.SetValue(String("audioSyncClampRawWindowMs"), Value(state->audioSyncClampRawWindowMs));
			info.SetValue(String("audioSyncClampSmoothingAlpha"), Value(state->audioSyncClampSmoothingAlpha));
			info.SetValue(String("audioSyncClampMaxStepMs"), Value(state->audioSyncClampMaxStepMs));
			info.SetValue(String("audioSyncOffsetSec"), Value(state->audioSyncOffsetSec));
			info.SetValue(String("audioClockSec"), Value(state->lastAudioClockSec));
			info.SetValue(String("audioLedTargetSec"), Value(state->lastAudioLedTargetSec));
			info.SetValue(String("status"), Value(String("ok")));
			info.SetValue(String("message"), Value(String("audio sync tuning snapshot")));
			return IntrinsicResult(Value(info));
		};
		raylibModule.SetValue("GetVideoAudioSyncTuning", i->GetFunc());

		i = Intrinsic::Create("");
		i->AddParam("video");
		i->code = INTRINSIC_LAMBDA {
			VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
			if (!state || !state->valid) return IntrinsicResult::Null;
			ValueDict info;
			info.SetValue(String("syncMode"), Value(String(state->syncMode.c_str())));
			info.SetValue(String("videoAudioSyncSkewMs"), Value(state->videoAudioSyncSkewMs));
			info.SetValue(String("totalFramesDecoded"), Value((int)state->totalFramesDecoded));
			info.SetValue(String("totalFramesSkipped"), Value((int)state->totalFramesSkipped));
			info.SetValue(String("totalFrameDropEvents"), Value((int)state->totalFrameDropEvents));
			info.SetValue(String("lastDecodeBudgetUsed"), Value(state->lastDecodeBudgetUsed));
			info.SetValue(String("lastDecodeBudgetExhausted"), Value(state->lastDecodeBudgetExhausted ? 1 : 0));
			info.SetValue(String("lastDecodedFramePts"), Value(state->lastDecodedFramePts));
			info.SetValue(String("lastUpdateTargetPts"), Value(state->lastUpdateTargetPts));
			info.SetValue(String("audioLedSyncEnabled"), Value(state->audioLedSyncEnabled ? 1 : 0));
			info.SetValue(String("audioSyncClampWindowMs"), Value(state->audioSyncClampWindowMs));
			info.SetValue(String("audioSyncClampAdaptive"), Value(state->audioSyncClampAdaptive ? 1 : 0));
			info.SetValue(String("audioSyncClampManualWindowMs"), Value(state->audioSyncClampManualWindowMs));
			info.SetValue(String("audioSyncClampAutoWindowMs"), Value(state->audioSyncClampAutoWindowMs));
			info.SetValue(String("audioSyncClampRawWindowMs"), Value(state->audioSyncClampRawWindowMs));
			info.SetValue(String("audioSyncClampSmoothingAlpha"), Value(state->audioSyncClampSmoothingAlpha));
			info.SetValue(String("audioSyncClampMaxStepMs"), Value(state->audioSyncClampMaxStepMs));
			info.SetValue(String("audioSyncOffsetSec"), Value(state->audioSyncOffsetSec));
			info.SetValue(String("audioClockSec"), Value(state->lastAudioClockSec));
			info.SetValue(String("audioLedTargetSec"), Value(state->lastAudioLedTargetSec));
			int framesBuffered = 0;
#if HAVE_LIBVPX
			if (!state->webPlayer && state->nextFrameIndex < state->frames.size()) {
				framesBuffered = (int)(state->frames.size() - state->nextFrameIndex);
			}
#endif
			info.SetValue(String("framesBuffered"), Value(framesBuffered));
			return IntrinsicResult(Value(info));
		};
		raylibModule.SetValue("GetVideoFrameTimingDiagnostics", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("video");
	i->AddParam("keepSeededHeaders", Value(1));
	i->code = INTRINSIC_LAMBDA {
		VideoPlayerState* state = (VideoPlayerState*)ValueToVideoPlayerHandle(context->GetVar(String("video")));
		if (!state || !state->valid) return IntrinsicResult::Null;
		int keepSeededHeaders = context->GetVar(String("keepSeededHeaders")).IntValue();

#ifndef PLATFORM_WEB
		if (!state->webPlayer) {
			ResetAudioDecodeSessionState(state, keepSeededHeaders != 0, true);
		}
#endif

		ValueDict info;
		info.SetValue(String("codec"), Value(String(state->audioCodecName.c_str())));
		info.SetValue(String("decodePath"), Value(String(state->audioDecodePath.c_str())));
		int supported = state->audioDecodeScaffoldReady ? 1 : 0;
		int ready = IsAudioDecodeReady(state) ? 1 : 0;
		int remaining = (int)(state->audioPackets.size() - state->nextAudioPacketIndex);
		if (remaining < 0) remaining = 0;
		info.SetValue(String("supported"), Value(supported));
		info.SetValue(String("readyForDecode"), Value(ready));
		info.SetValue(String("decodeSessionId"), Value((int)state->audioDecodeSessionId));
		info.SetValue(String("nextPacketIndex"), Value((int)state->nextAudioPacketIndex));
		info.SetValue(String("totalReadPackets"), Value((int)state->audioPacketsRead));
		info.SetValue(String("totalReadBytes"), Value((double)state->audioBytesRead));
		info.SetValue(String("decodedPcmFramesAvailable"), Value((double)state->decodedPcmFramesAvailable));
		info.SetValue(String("decodedPcmFramesTotal"), Value((double)state->decodedPcmFramesTotal));
		info.SetValue(String("decodedPcmFramesConsumed"), Value((double)state->decodedPcmFramesConsumed));
		info.SetValue(String("decodedVorbisPackets"), Value((int)state->vorbisPacketsDecoded));
		info.SetValue(String("remainingPackets"), Value(remaining));
		info.SetValue(String("lastReadPacketTime"), Value(state->audioLastReadPacketTime));
		info.SetValue(String("vorbisHeaderParseAttempted"), Value(state->vorbisHeaderParseAttempted ? 1 : 0));
		info.SetValue(String("vorbisIdentificationSeen"), Value(state->vorbisIdentificationSeen ? 1 : 0));
		info.SetValue(String("vorbisCommentSeen"), Value(state->vorbisCommentSeen ? 1 : 0));
		info.SetValue(String("vorbisSetupSeen"), Value(state->vorbisSetupSeen ? 1 : 0));
		bool vorbisHeadersReady = (state->vorbisIdentificationSeen && state->vorbisCommentSeen && state->vorbisSetupSeen);
		info.SetValue(String("vorbisHeadersReady"), Value(vorbisHeadersReady ? 1 : 0));
		const char* headerSource = "none";
		if (state->vorbisHeadersFromCodecPrivate && state->vorbisHeadersFromPacketStream) {
			headerSource = "codecPrivate+packet";
		} else if (state->vorbisHeadersFromCodecPrivate) {
			headerSource = "codecPrivate";
		} else if (state->vorbisHeadersFromPacketStream) {
			headerSource = "packet";
		}
		info.SetValue(String("vorbisHeaderSource"), Value(String(headerSource)));
		std::string missingHeaders;
		if (!state->vorbisIdentificationSeen) missingHeaders += "identification ";
		if (!state->vorbisCommentSeen) missingHeaders += "comment ";
		if (!state->vorbisSetupSeen) missingHeaders += "setup ";
		if (!missingHeaders.empty() && missingHeaders.back() == ' ') missingHeaders.pop_back();
		info.SetValue(String("vorbisMissingHeaders"), Value(String(missingHeaders.c_str())));

		const char* status = "ready";
		const char* message = "decode session reset applied";
#ifdef PLATFORM_WEB
		status = "web-backend";
		message = "audio decode reset is desktop-only";
#else
		if (state->webPlayer) {
			status = "web-backend";
			message = "audio decode reset is desktop-only";
		} else if (!supported) {
			status = "unsupported-codec";
			message = "audio decode path is not scaffolded for this codec";
		} else if (!ready) {
			status = "not-ready";
			message = "audio headers are incomplete; decoder not ready";
		} else if (remaining <= 0) {
			status = "end-of-stream";
			message = "no more audio packets to consume";
		}
#endif
		info.SetValue(String("status"), Value(String(status)));
		info.SetValue(String("message"), Value(String(message)));
		info.SetValue(String("resetApplied"), Value(1));
		info.SetValue(String("keptSeededHeaders"), Value(keepSeededHeaders != 0 ? 1 : 0));
		return IntrinsicResult(Value(info));
	};
	raylibModule.SetValue("ResetVideoAudioDecodeSession", i->GetFunc());

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
