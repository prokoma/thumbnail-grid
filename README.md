# thumbnail-grid

A simple tool to generate a grid of thumbnails from a video file. Uses `libavformat` and `libavcodec` to read the input file. The frames are scaled to the width specified by `-w` option (default 150px) and then saved to WEBP output file with configurable quality (`-q`, default 100 = lossless).

## Usage

```
thumbnail-grid [-r num_rows] [-c num_cols] [-w img_width] [-q quality] <input_file> <output_file>
```

## Dependencies

```bash
sudo apt install libwebp-dev libavcodec-dev libavutil-dev libavformat-dev libswscale-dev
```

## Compiling

```bash
make
```
