#include "StreamRelayer.hpp"
#include "WebSocketSession.hpp"
#include <iostream>
#include <vector>
#include <chrono>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace {
    void writeInt64LE(uint8_t* buf, int64_t val) {
        buf[0] = val & 0xFF;
        buf[1] = (val >> 8) & 0xFF;
        buf[2] = (val >> 16) & 0xFF;
        buf[3] = (val >> 24) & 0xFF;
        buf[4] = (val >> 32) & 0xFF;
        buf[5] = (val >> 40) & 0xFF;
        buf[6] = (val >> 48) & 0xFF;
        buf[7] = (val >> 56) & 0xFF;
    }

    void writeUint32LE(uint8_t* buf, uint32_t val) {
        buf[0] = val & 0xFF;
        buf[1] = (val >> 8) & 0xFF;
        buf[2] = (val >> 16) & 0xFF;
        buf[3] = (val >> 24) & 0xFF;
    }
}

StreamRelayer::~StreamRelayer() {
    stop();
}

void StreamRelayer::start(const std::string& rtspUrl) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        // Stop the current one first if running
        lock.unlock(); // avoid deadlock
        stop();
        lock.lock();
    }

    rtspUrl_ = rtspUrl;
    running_ = true;
    thread_ = std::thread(&StreamRelayer::runLoop, this);
    std::cout << "[StreamRelayer] Started relayer thread for URL: " << rtspUrl << std::endl;
}

void StreamRelayer::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
        std::cout << "[StreamRelayer] Stopped relayer thread." << std::endl;
    }
}

bool StreamRelayer::isActive() const {
    return running_;
}

void StreamRelayer::runLoop() {
    avformat_network_init();

    while (running_) {
        AVFormatContext* inFmtCtx = nullptr;
        AVDictionary* options = nullptr;
        
        // Force TCP to ensure frame delivery, set 5s connection timeout
        av_dict_set(&options, "rtsp_transport", "tcp", 0);
        av_dict_set(&options, "stimeout", "5000000", 0);

        std::cout << "[StreamRelayer] Connecting to RTSP: " << rtspUrl_ << std::endl;
        if (avformat_open_input(&inFmtCtx, rtspUrl_.c_str(), nullptr, &options) != 0) {
            std::cerr << "[StreamRelayer] Error: Could not open RTSP input. Retrying in 3 seconds..." << std::endl;
            av_dict_free(&options);
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }
        av_dict_free(&options);

        if (avformat_find_stream_info(inFmtCtx, nullptr) < 0) {
            std::cerr << "[StreamRelayer] Error: Could not find stream info. Retrying..." << std::endl;
            avformat_close_input(&inFmtCtx);
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        // Find Video Stream
        int videoStreamIdx = -1;
        for (unsigned int i = 0; i < inFmtCtx->nb_streams; ++i) {
            if (inFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                videoStreamIdx = i;
                break;
            }
        }

        if (videoStreamIdx == -1) {
            std::cerr << "[StreamRelayer] Error: No video stream found. Retrying..." << std::endl;
            avformat_close_input(&inFmtCtx);
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        // Setup Decoder
        const AVCodec* decoder = avcodec_find_decoder(inFmtCtx->streams[videoStreamIdx]->codecpar->codec_id);
        if (!decoder) {
            std::cerr << "[StreamRelayer] Error: Decoder not found. Retrying..." << std::endl;
            avformat_close_input(&inFmtCtx);
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        AVCodecContext* decCtx = avcodec_alloc_context3(decoder);
        avcodec_parameters_to_context(decCtx, inFmtCtx->streams[videoStreamIdx]->codecpar);
        if (avcodec_open2(decCtx, decoder, nullptr) < 0) {
            std::cerr << "[StreamRelayer] Error: Could not open decoder. Retrying..." << std::endl;
            avcodec_free_context(&decCtx);
            avformat_close_input(&inFmtCtx);
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        // Setup H.264 Encoder (libx264)
        const AVCodec* encoder = avcodec_find_encoder_by_name("libx264");
        if (!encoder) {
            std::cerr << "[StreamRelayer] Error: libx264 encoder not found. Retrying..." << std::endl;
            avcodec_free_context(&decCtx);
            avformat_close_input(&inFmtCtx);
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        AVCodecContext* encCtx = avcodec_alloc_context3(encoder);
        encCtx->width = decCtx->width > 0 ? decCtx->width : 1280;
        encCtx->height = decCtx->height > 0 ? decCtx->height : 720;
        encCtx->pix_fmt = AV_PIX_FMT_YUV420P; // Standard web color space
        encCtx->time_base = AVRational{1, 1000}; // millisecond precision
        encCtx->framerate = AVRational{25, 1};
        encCtx->gop_size = 25;
        encCtx->max_b_frames = 0; // Zero B-frames for real-time low latency

        // Apply low latency configurations
        av_opt_set(encCtx->priv_data, "preset", "ultrafast", 0);
        av_opt_set(encCtx->priv_data, "tune", "zerolatency", 0);
        av_opt_set(encCtx->priv_data, "profile", "baseline", 0); // avc1.42E01E compatible

        if (avcodec_open2(encCtx, encoder, nullptr) < 0) {
            std::cerr << "[StreamRelayer] Error: Could not open encoder. Retrying..." << std::endl;
            avcodec_free_context(&encCtx);
            avcodec_free_context(&decCtx);
            avformat_close_input(&inFmtCtx);
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        // Allocation of SwsContext and YUVFrame in case input format is not YUV420P
        SwsContext* swsCtx = nullptr;
        AVFrame* yuvFrame = av_frame_alloc();
        yuvFrame->format = AV_PIX_FMT_YUV420P;
        yuvFrame->width = encCtx->width;
        yuvFrame->height = encCtx->height;
        av_frame_get_buffer(yuvFrame, 0);

        AVPacket* pkt = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        AVPacket* encPkt = av_packet_alloc();

        int64_t start_time = av_gettime();

        std::cout << "[StreamRelayer] Relaying stream successfully initialized." << std::endl;

        while (running_) {
            if (av_read_frame(inFmtCtx, pkt) < 0) {
                std::cerr << "[StreamRelayer] Warning: Failed to read frame (end of stream or timeout)." << std::endl;
                break;
            }

            if (pkt->stream_index == videoStreamIdx) {
                // Decode frame
                if (avcodec_send_packet(decCtx, pkt) >= 0) {
                    while (avcodec_receive_frame(decCtx, frame) >= 0) {
                        AVFrame* sourceFrame = frame;

                        // Perform color space scaling if necessary
                        if (frame->format != AV_PIX_FMT_YUV420P) {
                            swsCtx = sws_getCachedContext(
                                swsCtx,
                                frame->width, frame->height, (AVPixelFormat)frame->format,
                                encCtx->width, encCtx->height, AV_PIX_FMT_YUV420P,
                                SWS_BICUBIC, nullptr, nullptr, nullptr
                            );
                            sws_scale(
                                swsCtx, frame->data, frame->linesize, 0, frame->height,
                                yuvFrame->data, yuvFrame->linesize
                            );
                            yuvFrame->pts = frame->pts;
                            sourceFrame = yuvFrame;
                        }

                        // Compute presentation timestamps relative to startup
                        int64_t current_time = (av_gettime() - start_time) / 1000; // ms
                        sourceFrame->pts = current_time;

                        // Encode frame to baseline H.264
                        if (avcodec_send_frame(encCtx, sourceFrame) >= 0) {
                            while (avcodec_receive_packet(encCtx, encPkt) >= 0) {
                                // Construct Binary Frame:
                                // [1 byte: isKeyFrame] [8 bytes: PTS] [8 bytes: DTS] [4 bytes: Payload Size] [Payload...]
                                size_t wsFrameSize = 1 + 8 + 8 + 4 + encPkt->size;
                                std::vector<uint8_t> wsFrame(wsFrameSize);

                                wsFrame[0] = (encPkt->flags & AV_PKT_FLAG_KEY) ? 1 : 0;
                                writeInt64LE(&wsFrame[1], encPkt->pts);
                                writeInt64LE(&wsFrame[9], encPkt->dts);
                                writeUint32LE(&wsFrame[17], encPkt->size);
                                std::memcpy(&wsFrame[21], encPkt->data, encPkt->size);

                                // Broadcast to active WebSocket connections
                                WebSocketHub::getInstance().broadcastFrame(wsFrame);

                                av_packet_unref(encPkt);
                            }
                        }
                        av_frame_unref(frame);
                    }
                }
            }
            av_packet_unref(pkt);
        }

        // Cleanup current connection contexts
        av_packet_free(&pkt);
        av_packet_free(&encPkt);
        av_frame_free(&frame);
        av_frame_free(&yuvFrame);
        if (swsCtx) {
            sws_freeContext(swsCtx);
        }

        avcodec_free_context(&encCtx);
        avcodec_free_context(&decCtx);
        avformat_close_input(&inFmtCtx);

        std::cout << "[StreamRelayer] Connection cleaned up. If running state remains true, will retry." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    avformat_network_deinit();
}
