# NAME #

curlm3u8 - Download media files listed in m3u8-file.

# SYNOPSIS #

curlm3u8 \<URL of a m3u8-file\>

# DESCRIPTION #

**curl m3u8** downloads a m3u8-files given by a URL via the curl-library.
The tool is intended to download the part of a movie (usually ts-files)
and afterwards merge them to a video-file (in mp4-format) via ffmpeg.

# TODO #

- Merge the ts-fiels to a mp4-video using ffmpeg. Call ffmpeg on the command-line:<br/>
	ffmpeg -i <part1.ts> <part2.ts> ... -acodec copy -vcodec copy <movie.mp4>
- Some m3u8-files (e.g. on arte.tv) only contain links to some other m3u8-files,
  that contain the movie in different resolutions. Support this.
  That means one should be able to pick, which resolution should be downloaded.

