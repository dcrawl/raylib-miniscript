(function () {
    var nextVideoId = 1;
    var videos = new Map();

    function waitFor(video, eventName) {
        return new Promise(function (resolve, reject) {
            function onOk() {
                cleanup();
                resolve();
            }
            function onErr() {
                cleanup();
                reject(new Error(eventName + " failed for video source"));
            }
            function cleanup() {
                video.removeEventListener(eventName, onOk);
                video.removeEventListener("error", onErr);
            }
            video.addEventListener(eventName, onOk, { once: true });
            video.addEventListener("error", onErr, { once: true });
        });
    }

    function getEntry(id) {
        return videos.get(id) || null;
    }

    Module.vpxVideoLoad = async function (url) {
        var video = document.createElement("video");
        video.preload = "auto";
        video.crossOrigin = "anonymous";
        video.playsInline = true;
        video.muted = false;
        video.loop = false;
        video.src = url;

        await waitFor(video, "loadedmetadata");
        try {
            await waitFor(video, "canplay");
        } catch (_e) {
            // Some browsers may already be ready after metadata.
        }

        var width = video.videoWidth || 1;
        var height = video.videoHeight || 1;
        var canvas = document.createElement("canvas");
        canvas.width = width;
        canvas.height = height;
        var ctx = canvas.getContext("2d", { willReadFrequently: true });
        if (!ctx) {
            throw new Error("2D canvas unavailable for video frame copy");
        }

        var id = nextVideoId++;
        videos.set(id, {
            video: video,
            canvas: canvas,
            ctx: ctx,
            width: width,
            height: height
        });

        return {
            id: id,
            width: width,
            height: height,
            duration: Number.isFinite(video.duration) ? video.duration : 0
        };
    };

    Module.vpxVideoDestroy = function (id) {
        var entry = getEntry(id);
        if (!entry) return;
        entry.video.pause();
        entry.video.removeAttribute("src");
        entry.video.load();
        videos.delete(id);
    };

    Module.vpxVideoPlay = function (id) {
        var entry = getEntry(id);
        if (!entry) return;
        var p = entry.video.play();
        if (p && typeof p.catch === "function") {
            p.catch(function () {
                // User gesture policies may block autoplay with audio.
            });
        }
    };

    Module.vpxVideoPause = function (id) {
        var entry = getEntry(id);
        if (!entry) return;
        entry.video.pause();
    };

    Module.vpxVideoStop = function (id) {
        var entry = getEntry(id);
        if (!entry) return;
        entry.video.pause();
        entry.video.currentTime = 0;
    };

    Module.vpxVideoSeek = function (id, timeSec) {
        var entry = getEntry(id);
        if (!entry) return;
        try {
            entry.video.currentTime = Math.max(0, Number(timeSec) || 0);
        } catch (_e) {
            // Ignore invalid seeks.
        }
    };

    Module.vpxVideoIsPlaying = function (id) {
        var entry = getEntry(id);
        if (!entry) return 0;
        var v = entry.video;
        return (!v.paused && !v.ended) ? 1 : 0;
    };

    Module.vpxVideoIsFinished = function (id) {
        var entry = getEntry(id);
        if (!entry) return 1;
        return entry.video.ended ? 1 : 0;
    };

    Module.vpxVideoGetTimePlayed = function (id) {
        var entry = getEntry(id);
        if (!entry) return 0;
        return Number.isFinite(entry.video.currentTime) ? entry.video.currentTime : 0;
    };

    Module.vpxVideoGetTimeLength = function (id) {
        var entry = getEntry(id);
        if (!entry) return 0;
        return Number.isFinite(entry.video.duration) ? entry.video.duration : 0;
    };

    Module.vpxVideoSetLooping = function (id, enabled) {
        var entry = getEntry(id);
        if (!entry) return;
        entry.video.loop = !!enabled;
    };

    Module.vpxVideoGetLooping = function (id) {
        var entry = getEntry(id);
        if (!entry) return 0;
        return entry.video.loop ? 1 : 0;
    };

    Module.vpxVideoSetPlaybackRate = function (id, rate) {
        var entry = getEntry(id);
        if (!entry) return;
        var r = Number(rate);
        if (!Number.isFinite(r)) r = 1.0;
        if (r < 0.05) r = 0.05;
        if (r > 4.0) r = 4.0;
        entry.video.playbackRate = r;
    };

    Module.vpxVideoGetPlaybackRate = function (id) {
        var entry = getEntry(id);
        if (!entry) return 1.0;
        var r = Number(entry.video.playbackRate);
        return Number.isFinite(r) ? r : 1.0;
    };

    Module.vpxVideoCopyFrameRGBA = function (id, dstPtr, maxBytes) {
        var entry = getEntry(id);
        if (!entry) return 0;

        var video = entry.video;
        var width = video.videoWidth || entry.width;
        var height = video.videoHeight || entry.height;
        if (width <= 0 || height <= 0) return 0;

        if (entry.canvas.width !== width || entry.canvas.height !== height) {
            entry.canvas.width = width;
            entry.canvas.height = height;
            entry.width = width;
            entry.height = height;
        }

        try {
            entry.ctx.drawImage(video, 0, 0, width, height);
            var imgData = entry.ctx.getImageData(0, 0, width, height);
            var bytes = imgData.data;
            if (bytes.length > maxBytes) return 0;
            HEAPU8.set(bytes, dstPtr);
            return bytes.length;
        } catch (_e) {
            return 0;
        }
    };
})();
