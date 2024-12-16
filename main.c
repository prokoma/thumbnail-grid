#include <libavcodec/avcodec.h>
#include <libavcodec/codec_id.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <webp/encode.h>

int numRows = 3;
int numCols = 3;
int imgMaxWidth = 150;
int imgMaxHeight = 0;
int quality = 100;
bool shift = false;
const char *inputFilePath = NULL;
const char *outputFilePath = NULL;

void copy_frame_to_result(AVFrame *frame, int dstX, int dstY,
                          AVFrame *resultFrame) {
  for (int y = 0; y < frame->height; y++) {
    memcpy(resultFrame->data[0] + (y + dstY) * resultFrame->linesize[0] +
               dstX * 3,
           frame->data[0] + y * frame->linesize[0], frame->width * 3);
  }
}

int save_frame_to_webp(AVFrame *frame, const char *filename, int quality) {
  uint8_t *output_data;
  // Encode the RGB24 image to WebP
  int output_size = WebPEncodeRGB(frame->data[0], frame->width, frame->height,
                                  frame->linesize[0], quality, &output_data);
  if (output_size == 0) {
    fprintf(stderr, "Error encoding to WebP\n");
    return 1;
  }

  // Write the WebP data to a file
  FILE *outfile = fopen(filename, "wb");
  if (!outfile) {
    fprintf(stderr, "Could not open output file %s\n", filename);
    WebPFree(output_data);
    return 1;
  }
  fwrite(output_data, output_size, 1, outfile);
  fclose(outfile);

  WebPFree(output_data);
  return 0;
}

AVFrame *allocate_frame(int width, int height, int format) {
  AVFrame *frame = av_frame_alloc();
  frame->width = width;
  frame->height = height;
  frame->format = format;
  if (av_image_alloc(frame->data, frame->linesize, width, height, format, 16) <
      0) {
    return NULL;
  }
  return frame;
}

int main(int argc, char *argv[]) {
  int i = 1;
  for (; i < argc; i++) {
    if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
      numRows = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
      numCols = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
      imgMaxWidth = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
      imgMaxHeight = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-s") == 0) {
      shift = true;
    } else if (strcmp(argv[i], "-q") == 0 && i + 1 < argc) {
      quality = atoi(argv[++i]);
    } else if (inputFilePath == NULL) {
      inputFilePath = argv[i];
    } else if (outputFilePath == NULL) {
      outputFilePath = argv[i];
    } else {
      break;
    }
  }
  if (i != argc || numRows < 0 || numCols < 0 || inputFilePath == NULL ||
      outputFilePath == NULL || quality < 0 || quality > 100 ||
      imgMaxWidth < 0 || imgMaxHeight < 0) {
    fprintf(stderr,
            "Usage: %s [-r num_rows] [-c num_cols] [-w img_max_width] [-h "
            "img_max_height] [-s] [-q quality] "
            "<input_file> "
            "<output_file>\n",
            argv[0]);
    return 1;
  }

  // av_log_set_level(AV_LOG_DEBUG);

  AVFormatContext *inputCtx = NULL;
  if (avformat_open_input(&inputCtx, inputFilePath, NULL, NULL) < 0) {
    fprintf(stderr, "Could not open input file %s\n", inputFilePath);
    return 1;
  }

  if (avformat_find_stream_info(inputCtx, NULL) < 0) {
    fprintf(stderr, "Could not find stream information\n");
    return 1;
  }

  int videoStreamIdx = -1;
  for (int i = 0; i < inputCtx->nb_streams; i++) {
    if (inputCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
        !(inputCtx->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
      videoStreamIdx = i;
      break;
    }
  }
  if (videoStreamIdx == -1) {
    fprintf(stderr, "Could not find a video stream\n");
    return 1;
  }

  AVStream * videoStream = inputCtx->streams[videoStreamIdx];

  const AVCodec *codec = avcodec_find_decoder(
      videoStream->codecpar->codec_id);
  if (codec == NULL) {
    fprintf(stderr, "Unsupported codec!\n");
    return -1;
  }

  AVCodecContext *decoderCtx = avcodec_alloc_context3(codec);

  // copy codec parameters from input stream to output codec context
  if (avcodec_parameters_to_context(
          decoderCtx, videoStream->codecpar) < 0) {
    fprintf(stderr, "Could not copy codec parameters to decoder context\n");
    return -1;
  }

  if (avcodec_open2(decoderCtx, codec, NULL) < 0) {
    fprintf(stderr, "Could not open codec\n");
    return -1;
  }

  int imgCount = numRows * numCols;

  int frameContentWidth = decoderCtx->width;
  int frameContentHeight = decoderCtx->height;

  if (imgMaxWidth > 0 && frameContentWidth > imgMaxWidth) {
    frameContentHeight = frameContentHeight * imgMaxWidth / frameContentWidth;
    frameContentWidth = imgMaxWidth;
  }
  if (imgMaxHeight > 0 && frameContentHeight > imgMaxHeight) {
    frameContentWidth = frameContentWidth * imgMaxHeight / frameContentHeight;
    frameContentHeight = imgMaxHeight;
  }

  int frameWidth = imgMaxWidth > 0 ? imgMaxWidth : frameContentWidth;
  int frameHeight = imgMaxHeight > 0 ? imgMaxHeight : frameContentHeight;

  int frameContentOffsetX = (frameWidth - frameContentWidth) / 2;
  int frameContentOffsetY = (frameHeight - frameContentHeight) / 2;

  int resultWidth = frameWidth * numCols;
  int resultHeight = frameHeight * numRows;

  // allocate result frame (mosaic)
  AVFrame *frameResult =
      allocate_frame(resultWidth, resultHeight, AV_PIX_FMT_RGB24);
  if (frameResult == NULL) {
    fprintf(stderr, "Could not allocate result frame\n");
    return 1;
  }
  // clear frame to black
  memset(frameResult->data[0], 0,
         frameResult->linesize[0] * frameResult->height);

  // raw decoded frame
  AVFrame *frame = av_frame_alloc();

  // allocate scaled frame (single screenshot)
  AVFrame *frameScaled =
      allocate_frame(frameContentWidth, frameContentHeight, AV_PIX_FMT_RGB24);
  if (frameScaled == NULL) {
    fprintf(stderr, "Could not allocate scaled frame\n");
    return 1;
  }

  struct SwsContext *swsCtx =
      sws_getContext(decoderCtx->width, decoderCtx->height, decoderCtx->pix_fmt,
                     frameContentWidth, frameContentHeight, frameScaled->format,
                     SWS_BILINEAR, NULL, NULL, NULL);

  int64_t duration = videoStream->duration;
  const AVRational time_base = videoStream->time_base;
  int64_t interval = duration / (shift ? imgCount + 1 : imgCount);

  int ret;
  char errBuf[AV_ERROR_MAX_STRING_SIZE];

  for (int i = 0; i < imgCount; i++) {
    int64_t seek_target = (shift ? i + 1 : i) * interval;
    int64_t seek_target_sec = av_rescale(seek_target, time_base.num, time_base.den);

    printf("processing frame %d/%d (at %02d:%02d:%02d)\n", i + 1, imgCount,
           (int)(seek_target_sec / 3600), (int)(seek_target_sec / 60), (int)(seek_target_sec % 60));
    if (av_seek_frame(inputCtx, videoStreamIdx, seek_target,
                      AVSEEK_FLAG_BACKWARD) < 0) {
      fprintf(stderr, "Error while seeking\n");
      return 1;
    }

    avcodec_flush_buffers(decoderCtx);
    AVPacket pkt;
    while (av_read_frame(inputCtx, &pkt) == 0) {
      if (pkt.stream_index != videoStreamIdx) {
        av_packet_unref(&pkt);
        continue;
      }
      if ((ret = avcodec_send_packet(decoderCtx, &pkt)) < 0) {
        av_packet_unref(&pkt);
        if (ret == AVERROR(EAGAIN)) {
          continue;
        }
        fprintf(stderr, "Error while sending packet: %s\n",
                av_make_error_string(errBuf, sizeof(errBuf), ret));
        return 1;
      }
      if ((ret = avcodec_receive_frame(decoderCtx, frame)) < 0) {
        av_packet_unref(&pkt);
        if (ret == AVERROR(EAGAIN)) {
          continue;
        }
        fprintf(stderr, "Error while receiving frame %d\n", AVERROR(ret));
        return 1;
      }

      sws_scale(swsCtx, (const uint8_t *const *)frame->data, frame->linesize, 0,
                frame->height, frameScaled->data, frameScaled->linesize);

      // char tmpfn[64];
      // snprintf(tmpfn, sizeof(tmpfn), "out%d.webp", i);
      // save_frame_to_webp(frameScaled, tmpfn);

      int dstY = (i / numCols) * frameHeight + frameContentOffsetY;
      int dstX = (i % numCols) * frameWidth + frameContentOffsetX;

      copy_frame_to_result(frameScaled, dstX, dstY, frameResult);

      av_frame_unref(frame);
      av_packet_unref(&pkt);
      break;
    }
  }

  printf("saving output file\n");

  ret = save_frame_to_webp(frameResult, outputFilePath, quality);

  av_freep(frameResult->data);
  av_free(frameResult);
  sws_freeContext(swsCtx);
  av_freep(frameScaled->data);
  av_free(frameScaled);
  av_free(frame);
  avcodec_free_context(&decoderCtx);
  avformat_close_input(&inputCtx);

  return ret;
}
