#include "ffmpeg_encode_mp4.h"

void FFmpegEncodeMp4::getMP4File(const char *h264FilePath_, const char *aacFilePath_, const char *mp4FilePath_, float fps) {
    //文件路径信息
    const char *in_filename_v = h264FilePath_;
    const char *in_filename_a = aacFilePath_;
    const char *out_filename = mp4FilePath_;

    AVOutputFormat *ofmt = NULL;
    //Input AVFormatContext and Output AVFormatContext
    AVFormatContext *ifmt_ctx_v = NULL, *ifmt_ctx_a = NULL, *ofmt_ctx = NULL;
    AVPacket pkt;
    int i;
    int videoindex_v = -1, videoindex_out = -1;
    int audioindex_a = -1, audioindex_out = -1;
    int frame_index = 0;
    int64_t cur_pts_v = 0, cur_pts_a = 0;

    av_register_all();
    //Input
    if (avformat_open_input(&ifmt_ctx_v, in_filename_v, 0, 0) < 0) {
        goto end;
    }
    if (avformat_find_stream_info(ifmt_ctx_v, 0) < 0) {
        goto end;
    }
    if (avformat_open_input(&ifmt_ctx_a, in_filename_a, 0, 0) < 0) {
        goto end;
    }
    if (avformat_find_stream_info(ifmt_ctx_a, 0) < 0) {
        goto end;
    }
    //输入信息
    av_dump_format(ifmt_ctx_v, 0, in_filename_v, 0);
    av_dump_format(ifmt_ctx_a, 0, in_filename_a, 0);
    //输出信息
    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, out_filename);
    if (!ofmt_ctx) {
        goto end;
    }
    ofmt = ofmt_ctx->oformat;

    for (i = 0; i < ifmt_ctx_v->nb_streams; i++) {
        if (ifmt_ctx_v->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            AVStream *in_stream = ifmt_ctx_v->streams[i];
            AVStream *out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
            videoindex_v = i;
            if (!out_stream) {
                goto end;
            }
            videoindex_out = out_stream->index;
            if (avcodec_copy_context(out_stream->codec, in_stream->codec) < 0) {
                goto end;
            }
            out_stream->codec->codec_tag = 0;
            if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
                out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
            break;
        }
    }

    for (i = 0; i < ifmt_ctx_a->nb_streams; i++) {
        if (ifmt_ctx_a->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            AVStream *in_stream = ifmt_ctx_a->streams[i];
            AVStream *out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
            audioindex_a = i;
            if (!out_stream) {
                goto end;
            }
            audioindex_out = out_stream->index;
            if (avcodec_copy_context(out_stream->codec, in_stream->codec) < 0) {
                goto end;
            }
            out_stream->codec->codec_tag = 0;
            if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
                out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
            break;
        }
    }
    av_dump_format(ofmt_ctx, 0, out_filename, 1);
    //Open output file
    if (!(ofmt->flags & AVFMT_NOFILE)) {
        if (avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE) < 0) {
            goto end;
        }
    }
    //Write file header
    if (avformat_write_header(ofmt_ctx, NULL) < 0) {
        goto end;
    }
    //对于某些封装格式（例如MP4/FLV/MKV等）中的H.264，需要用到名称为“h264_mp4toannexb”的bitstream filter。
#if USE_H264BSF
    AVBitStreamFilterContext* h264bsfc =  av_bitstream_filter_init("h264_mp4toannexb");
#endif
    //对于某些封装格式（例如MP4/FLV/MKV等）中的AAC，需要用到名称为“aac_adtstoasc”的bitstream filter。
#if USE_AACBSF
    AVBitStreamFilterContext* aacbsfc =  av_bitstream_filter_init("aac_adtstoasc");
#endif
    while (1) {
        AVFormatContext *ifmt_ctx;
        int stream_index = 0;
        AVStream *in_stream, *out_stream;

        //Get an AVPacket
        if (av_compare_ts(cur_pts_v, ifmt_ctx_v->streams[videoindex_v]->time_base, cur_pts_a,
                          ifmt_ctx_a->streams[audioindex_a]->time_base) <= 0) {
            ifmt_ctx = ifmt_ctx_v;
            stream_index = videoindex_out;

            if (av_read_frame(ifmt_ctx, &pkt) >= 0) {
                do {
                    in_stream = ifmt_ctx->streams[pkt.stream_index];
                    out_stream = ofmt_ctx->streams[stream_index];

                    if (pkt.stream_index == videoindex_v) {
                        //FIX：No PTS (Example: Raw H.264)
                        //Simple Write PTS
                        if (pkt.pts == AV_NOPTS_VALUE) {
                            //Write PTS
                            AVRational time_base1 = in_stream->time_base;
                            //Duration between 2 frames (us)
                            int64_t calc_duration =
                                    (double) AV_TIME_BASE / av_q2d(in_stream->r_frame_rate);
                            //Parameters
                            pkt.pts = (double) (frame_index * calc_duration) /
                                      (double) (av_q2d(time_base1) * AV_TIME_BASE);
                            pkt.dts = pkt.pts;
                            pkt.duration = (double) calc_duration /
                                           (double) (av_q2d(time_base1) * AV_TIME_BASE);
                            frame_index++;
                        }
                        cur_pts_v = pkt.pts;
                        break;
                    }
                } while (av_read_frame(ifmt_ctx, &pkt) >= 0);
            } else {
                break;
            }
        } else {
            ifmt_ctx = ifmt_ctx_a;
            stream_index = audioindex_out;
            if (av_read_frame(ifmt_ctx, &pkt) >= 0) {
                do {
                    in_stream = ifmt_ctx->streams[pkt.stream_index];
                    out_stream = ofmt_ctx->streams[stream_index];
                    if (pkt.stream_index == audioindex_a) {
                        //FIX：No PTS
                        //Simple Write PTS
                        if (pkt.pts == AV_NOPTS_VALUE) {
                            //Write PTS
                            AVRational time_base1 = in_stream->time_base;
                            //Duration between 2 frames (us)
                            int64_t calc_duration =
                                    (double) AV_TIME_BASE / av_q2d(in_stream->r_frame_rate);
                            //Parameters
                            pkt.pts = (double) (frame_index * calc_duration) /
                                      (double) (av_q2d(time_base1) * AV_TIME_BASE);
                            pkt.dts = pkt.pts;
                            pkt.duration = (double) calc_duration /
                                           (double) (av_q2d(time_base1) * AV_TIME_BASE);
                            frame_index++;
                        }
                        cur_pts_a = pkt.pts;

                        break;
                    }
                } while (av_read_frame(ifmt_ctx, &pkt) >= 0);
            } else {
                break;
            }

        }
        //FIX:Bitstream Filter
#if USE_H264BSF
        av_bitstream_filter_filter(h264bsfc, in_stream->codec, NULL, &pkt.data, &pkt.size, pkt.data, pkt.size, 0);
#endif
#if USE_AACBSF
        av_bitstream_filter_filter(aacbsfc, out_stream->codec, NULL, &pkt.data, &pkt.size, pkt.data, pkt.size, 0);
#endif
        //Convert PTS/DTS
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base,
                                   (AVRounding) (AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base,
                                   (AVRounding) (AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;
        pkt.stream_index = stream_index;
        //Write
        if (av_interleaved_write_frame(ofmt_ctx, &pkt) < 0) {
            break;
        }
        av_free_packet(&pkt);
    }
    //Write file trailer
    av_write_trailer(ofmt_ctx);
#if USE_H264BSF
    av_bitstream_filter_close(h264bsfc);
#endif
#if USE_AACBSF
    av_bitstream_filter_close(aacbsfc);
#endif
    end:
    avformat_close_input(&ifmt_ctx_v);
    avformat_close_input(&ifmt_ctx_a);
    /* close output */
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE)) {
        avio_close(ofmt_ctx->pb);
    }
    avformat_free_context(ofmt_ctx);
}