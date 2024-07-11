# Quick and easy PNG image size reduction

*<svg aria-hidden="true" focusable="false" role="img" class="octicon octicon-tag" viewBox="0 0 16 16" width="16" height="16" fill="currentColor" style="display:inline-block;user-select:none;overflow:visible"><path d="M1 7.775V2.75C1 1.784 1.784 1 2.75 1h5.025c.464 0 .91.184 1.238.513l6.25 6.25a1.75 1.75 0 0 1 0 2.474l-5.026 5.026a1.75 1.75 0 0 1-2.474 0l-6.25-6.25A1.752 1.752 0 0 1 1 7.775Zm1.5 0c0 .066.026.13.073.177l6.25 6.25a.25.25 0 0 0 .354 0l5.025-5.025a.25.25 0 0 0 0-.354l-6.25-6.25a.25.25 0 0 0-.177-.073H2.75a.25.25 0 0 0-.25.25ZM6 5a1 1 0 1 1 0 2 1 1 0 0 1 0-2Z"></path></svg> [Optimization](/blog/articles-per-tag.html#Optimization), [PNG](/blog/articles-per-tag.html#PNG)*

I seredenpitously noticed that my blog had somewhat big PNG images. But these are just very simple screenshots. There surely must be a way to reduce their size, without affecting their size or legibility?
Well yes, let's quantize them!
What? Quant-what?

Quoting Wikipedia:

> Quantization, involved in image processing, is a lossy compression technique achieved by compressing a range of values to a single quantum (discrete) value. When the number of discrete symbols in a given stream is reduced, the stream becomes more compressible. For example, reducing the number of colors required to represent a digital image makes it possible to reduce its file size

In other words, by picking the right color palette for an image, we can reduce its size without the human eye noticing. For example, an image which has multiple red variants, all very close, are a prime candidate to be converted to the same red color (perhaps the average value) so long as the human eye does not see the difference. Since PNG images use compression, it will compress better.

At least, that's my layman understanding.

Fortunately there is an open-source [command line tool](https://github.com/kornelski/pngquant) that is very easy to use and works great. So go give them a star and come back!

I simply ran the tool on all images to convert them in place in parallel:

```sh
$ ls *.png | parallel 'pngquant {} -o {}.tmp && mv {}.tmp {}'
```

It finished instantly, and here is the result:

```sh
$ git show 2e126f55a77e75e182ea18b36fb535a0e37793e4 --compact-summary
commit 2e126f55a77e75e182ea18b36fb535a0e37793e4 (HEAD -> master, origin/master, origin/HEAD)

    use pgnquant to shrink images

 feed.png                        | Bin 167641 -> 63272 bytes
 gnuplot.png                     | Bin 4594 -> 3316 bytes
 mem_prof1.png                   | Bin 157587 -> 59201 bytes
 mem_prof2.png                   | Bin 209046 -> 81028 bytes
 mem_prof3.png                   | Bin 75019 -> 27259 bytes
 mem_prof4.png                   | Bin 50964 -> 21345 bytes
 wayland-screenshot-floating.png | Bin 54620 -> 19272 bytes
 wayland-screenshot-red.png      | Bin 101047 -> 45230 bytes
 wayland-screenshot-tiled.png    | Bin 188549 -> 107573 bytes
 wayland-screenshot-tiled1.png   | Bin 505994 -> 170804 bytes
 x11_x64_black_window.png        | Bin 32977 -> 16898 bytes
 x11_x64_final.png               | Bin 47985 -> 16650 bytes
 12 files changed, 0 insertions(+), 0 deletions(-)
```

Eye-balling it, every image was on average halved. Not bad, for no visible difference!

Initially, I wanted to use the new hotness: AVIF. Here's an example using the `avifenc` tool on the original image:

```sh
$ avifenc feed.png feed.avif
$ stat -c '%n %s' feed.{png,avif}
feed.png 167641
feed.avif 36034
```

That's almost a x5 reduction in size! However this format is not yet well supported by all browsers. It's recommended to still serve a PNG as fallback, which is a bit too complex for this blog. Still, this format is very promising so I thought I should mention it.


So as of now, all PNG images on this blog are much lighter! Not too bad for 10m of work.


