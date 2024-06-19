# Let's write a video game from scratch with X11

In a [previous article](/blog/x11_x64.html) I've done the 'Hello, world!' of GUI: A black window with a white text, using X11 without any libraries, just talking directly over a socket. 

In a [later article](/blog/wayland_from_scratch.html) I've done the same with Wayland, showing a static image.

I hope that I've shown that no libraries are needed and the ~~overly complicated~~ venerable `libX11` and `libxcb` libraries (along with the tens of separate libraries you need to write a GUI - `libXau`, `libXext`, `libXinerama`, etc), or the mainstream SDL, may complicate your build and obscure what is really going on. It something does not work, that's tens of thousands of lines of code you have to now troubleshoot.

The advantage of this approach is that the application is tiny and stand-alone: statically linked with the few bits of libC it uses (and that's it), it can be trivially compiled on every Unix, and copied around, and it will work on every machine (with the same OS and architecture, that is). Even on ancient Linuxes from 20 years ago.

However these GUIs of mine were mere proof of concepts, too simplistic to convince the skeptic that this approach is actually viable.

Per chance, I recently stumbled upon this [Hacker News post](https://news.ycombinator.com/item?id=40647278): 

> 	Microsoft's official Minesweeper app has ads, pay-to-win, and is hundreds of MBs

And I thought it would be fun to make with the same principles a full-fledged GUI application: the cult video game Minesweeper.

Will it be hundred of megabytes when we finish? How much work is it really? Can a hobbyist make this in a few hours? 

![Screenshot](https://github.com/gaultier/minesweeper-from-scratch/raw/master/screenshot.png)

![Screencast](https://github.com/gaultier/minesweeper-from-scratch/raw/master/screencast.webm)


*Press enter to reset and press any mouse button to uncover the cell under the mouse cursor.*

The result is a ~300 KiB statically linked executable, that requires no libraries, and uses a constant ~ 1 MiB of resident heap memory (allocated at the start, to hold the assets). That's roughly a thousand times smaller in size than Microsoft's. And it only is a few hundred lines of code.


Here are the steps we need to take:

- Open a window
- Upload image data (the one sprite with all the assets)
- Draw parts of the sprite to the window
- React to keyboard/mouse events

And that's it. Spoiler alert: every step is 1-3 X11 messages that we need to craft and send. The only messages that we receive are the keyboard and mouse events. It's really not much at all!

We will implement this in the [Odin programming language](https://odin-lang.org/) which I really enjoy. But if you want to follow along with C or anything really, go for it. All we need is to be able to open a Unix socket, send and receive data on it, and load an image. We will use PNG for thats since Odin has in its standard library support for PNGs, but we could also very easily use a simple format like PPM (like I did in the Wayland article) that is trivial to parse. Since Odin has support for both in its standard library, I stuck with PNG.


