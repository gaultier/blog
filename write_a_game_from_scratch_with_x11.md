# Let's write a video game from scratch with X11

In a [previous article](/blog/x11_x64.html) I've done the 'Hello, world!' of GUI: A black window with a white text, using X11 without any libraries, just talking directly over a socket. 

In a [later article](/blog/wayland_from_scratch.html) I've done the same with Wayland, showing a static image.

I hope that I've shown that no libraries are needed and the ~~overly complicated~~ venerable `libX11` and `libxcb` libraries (along with the tens of separate libraries you need to write a GUI - `libXau`, `libXext`, `libXinerama`, etc), or the mainstream SDL, may complicate your build and obscure what is really going on.

The advantage of this approach is that the application is tiny and stand-alone: statically linked with the few bits of libC it uses (and that's it), it can be trivially compiled on every Unix, and copied around, and it will work on every machine (with the same OS and architecture, that is). Even on ancient Linuxes from 20 years ago.

However these applications of mine were mere simplistic proof of concepts. 

Per chance, I recently stumbled upon this [Hacker News post](https://news.ycombinator.com/item?id=40647278): 

> 	Microsoft's official Minesweeper app has ads, pay-to-win, and is hundreds of MBs

And I thought it would be fun to make with the same principles a full-fledged GUI application: the cult video game Minesweeper.

Will it be hundred of megabytes when we finish? How much work is it really? Can a hobbyist make this in a few hours? 

![Screenshot](screenshot.png)

https://github.com/gaultier/minesweeper-from-scratch/raw/master/screencast.webm

