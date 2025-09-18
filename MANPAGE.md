# NAME #

curl_m3u8 - Download media files listed in m3u8-file (via libcurl) and concat them (via ffmpeg).

# SYNOPSIS #

curl_m3u8 [-v|--verbose] --name &lt;NAME&gt; &lt;URL of a m3u8-file&gt;

# DESCRIPTION #

**curl_m3u8** downloads all the parts of a playlist m3u8-file given by a URL via the libcurl-library
and afterwards concats them via ffmpeg to &lt;NAME&gt;.mp4.

The parts are downloaded in parallel (five at a time) to the current directory!
The download-speed is limited to 1 MB/s per file, so 5 MB/s in total.
After all parts are concated via ffmpeg, they are deleted.

