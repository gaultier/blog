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

We will implement this in the [Odin programming language](https://odin-lang.org/) which I really enjoy. But if you want to follow along with C or anything really, go for it. All we need is to be able to open a Unix socket, send and receive data on it, and load an image. We will use PNG for thats since Odin has in its standard library support for PNGs, but we could also very easily use a simple format like PPM (like I did in the linked Wayland article) that is trivial to parse. Since Odin has support for both in its standard library, I stuck with PNG.


Finally, if you're into writing X11 applications even with libraries, lots of things are undocumented and this article can be a good learning resource.


## Authentication

In previous articles, we connected to the X server without any authentication.

Let's be a bit more refined: we now also support the X authentication protocol. 

That's because when running under Wayland with XWayland, we have to use authentication.

This requires our application to read a 16 bytes long token that's present in a file in the user's home directory, and include it in the handshake we send to the X server.

This mechanism is called `MIT-MAGIC-COOKIE-1`.

The catch is that this file contains multiple tokens for multiple authentication mechanisms, and for multiple hosts. Remember, X11 is designed to work over the network. However we only ccare here about the entry for localhost.

So we need to parse a little bit. It's basically what `libXau` does. From its docs:

``` 
The .Xauthority file is a binary file consisting of a sequence of entries
in the following format:
	2 bytes		Family value (second byte is as in protocol HOST)
	2 bytes		address length (always MSB first)
	A bytes		host address (as in protocol HOST)
	2 bytes		display "number" length (always MSB first)
	S bytes		display "number" string
	2 bytes		name length (always MSB first)
	N bytes		authorization name string
	2 bytes		data length (always MSB first)
	D bytes		authorization data string
```

First let's define some types and constants:

```odin

AUTH_ENTRY_FAMILY_LOCAL: u16 : 1
AUTH_ENTRY_MAGIC_COOKIE: string : "MIT-MAGIC-COOKIE-1"

AuthToken :: [16]u8

AuthEntry :: struct {
	family:    u16,
	auth_name: []u8,
	auth_data: []u8,
}
```

We only define fields we are interested in.

Let's now parse each entry accordingly:

```odin
read_x11_auth_entry :: proc(buffer: ^bytes.Buffer) -> (AuthEntry, bool) {
	entry := AuthEntry{}

	{
		n_read, err := bytes.buffer_read(buffer, mem.ptr_to_bytes(&entry.family))
		if err == .EOF {return {}, false}

		assert(err == .None)
		assert(n_read == size_of(entry.family))
	}

	address_len: u16 = 0
	{
		n_read, err := bytes.buffer_read(buffer, mem.ptr_to_bytes(&address_len))
		assert(err == .None)

		address_len = bits.byte_swap(address_len)
		assert(n_read == size_of(address_len))
	}

	address := make([]u8, address_len)
	{
		n_read, err := bytes.buffer_read(buffer, address)
		assert(err == .None)
		assert(n_read == cast(int)address_len)
	}

	display_number_len: u16 = 0
	{
		n_read, err := bytes.buffer_read(buffer, mem.ptr_to_bytes(&display_number_len))
		assert(err == .None)

		display_number_len = bits.byte_swap(display_number_len)
		assert(n_read == size_of(display_number_len))
	}

	display_number := make([]u8, display_number_len)
	{
		n_read, err := bytes.buffer_read(buffer, display_number)
		assert(err == .None)
		assert(n_read == cast(int)display_number_len)
	}

	auth_name_len: u16 = 0
	{
		n_read, err := bytes.buffer_read(buffer, mem.ptr_to_bytes(&auth_name_len))
		assert(err == .None)

		auth_name_len = bits.byte_swap(auth_name_len)
		assert(n_read == size_of(auth_name_len))
	}

	entry.auth_name = make([]u8, auth_name_len)
	{
		n_read, err := bytes.buffer_read(buffer, entry.auth_name)
		assert(err == .None)
		assert(n_read == cast(int)auth_name_len)
	}

	auth_data_len: u16 = 0
	{
		n_read, err := bytes.buffer_read(buffer, mem.ptr_to_bytes(&auth_data_len))
		assert(err == .None)

		auth_data_len = bits.byte_swap(auth_data_len)
		assert(n_read == size_of(auth_data_len))
	}

	entry.auth_data = make([]u8, auth_data_len)
	{
		n_read, err := bytes.buffer_read(buffer, entry.auth_data)
		assert(err == .None)
		assert(n_read == cast(int)auth_data_len)
	}

	return entry, true
}
```

Now we can sift through the different entries in the file to find the one we are after:

```odin
load_x11_auth_token :: proc(allocator := context.allocator) -> (token: AuthToken, ok: bool) {
	context.allocator = allocator
	defer free_all(allocator)

	filename_env := os.get_env("XAUTHORITY")

	filename :=
		len(filename_env) != 0 \
		? filename_env \
		: filepath.join([]string{os.get_env("HOME"), ".Xauthority"})

	data := os.read_entire_file_from_filename(filename) or_return

	buffer := bytes.Buffer{}
	bytes.buffer_init(&buffer, data[:])


	for {
		auth_entry := read_x11_auth_entry(&buffer) or_break

		if auth_entry.family == AUTH_ENTRY_FAMILY_LOCAL &&
		   slice.equal(auth_entry.auth_name, transmute([]u8)AUTH_ENTRY_MAGIC_COOKIE) &&
		   len(auth_entry.auth_data) == size_of(AuthToken) {

			mem.copy_non_overlapping(
				raw_data(&token),
				raw_data(auth_entry.auth_data),
				size_of(AuthToken),
			)
			return token, true
		}
	}

    // Did not find a fitting token.
	return {}, false
}
```


Odin has a nice shorthand to return early on errors: `or_return`, which is the equivalent of `?` in Rust or `try` in Zig. Same thing with `or_break`.

And we use it in this manner in `main`:

```odin
main :: proc() {
	auth_token, _ := load_x11_auth_token(context.temp_allocator)
}
```

If we did not find a fitting token, no matter, we will simply carry on with an empty one.

One interesting thing: in Odin, similarly to Zig, allocators are passed to functions wishing to allocate memory. Contrary to Zig though, Odin has a mechanism to make that less tedious (and a bit more implicit as a result). Odin is nice enough to also provide us two allocators that we can use right away: A general purpose allocator, and a temporary allocator that uses an arena.

Since authentication entries can be large, we have to allocate - the stack is only so big. However, we do not want to retain the parsed entries from the file in memory after finding the 16 bytes token, so we `defer free_all(allocator)`. This is much better than going through each entry and freeing individually each field. We simply free the whole arena (but the backing memory remains around to be reused later).

It's very elegant compared to other languages.

## Opening a window

This part is almost exactly the same as the first linked article so I'll speed run this.

First we open a UNIX domain socket:

```odin
connect_x11_socket :: proc() -> os.Socket {
	SockaddrUn :: struct #packed {
		sa_family: os.ADDRESS_FAMILY,
		sa_data:   [108]u8,
	}

	socket, err := os.socket(os.AF_UNIX, os.SOCK_STREAM, 0)
	assert(err == os.ERROR_NONE)

	possible_socket_paths := [2]string{"/tmp/.X11-unix/X0", "/tmp/.X11-unix/X1"}
	for &socket_path in possible_socket_paths {
		addr := SockaddrUn {
			sa_family = cast(u16)os.AF_UNIX,
		}
		mem.copy_non_overlapping(&addr.sa_data, raw_data(socket_path), len(socket_path))

		err = os.connect(socket, cast(^os.SOCKADDR)&addr, size_of(addr))
		if (err == os.ERROR_NONE) {return socket}
	}

	os.exit(1)
}
```

We try a few possible paths for the socket, that can vary a bit from distribution to distribution.

We now can send the handshake and receive general information from the server, let's define some structs for that per the X11 protocol:

```odin
Screen :: struct #packed {
	id:             u32,
	colormap:       u32,
	white:          u32,
	black:          u32,
	input_mask:     u32,
	width:          u16,
	height:         u16,
	width_mm:       u16,
	height_mm:      u16,
	maps_min:       u16,
	maps_max:       u16,
	root_visual_id: u32,
	backing_store:  u8,
	save_unders:    u8,
	root_depth:     u8,
	depths_count:   u8,
}

ConnectionInformation :: struct {
	root_screen:      Screen,
	resource_id_base: u32,
	resource_id_mask: u32,
}
```

The structs are `#packed` to match the network protocol format, otherwise the compiler may insert padding between fields.

We can now send the handshake. We leverage the `writev` system call to send multiple separate buffers of different lengths in one call.

We skip over most of the information the server sends us, since we only are after a few fields:

```odin
x11_handshake :: proc(socket: os.Socket, auth_token: ^AuthToken) -> ConnectionInformation {
	Request :: struct #packed {
		endianness:             u8,
		pad1:                   u8,
		major_version:          u16,
		minor_version:          u16,
		authorization_len:      u16,
		authorization_data_len: u16,
		pad2:                   u16,
	}

	request := Request {
		endianness             = 'l',
		major_version          = 11,
		authorization_len      = len(AUTH_ENTRY_MAGIC_COOKIE),
		authorization_data_len = size_of(AuthToken),
	}


	{
		padding := [2]u8{0, 0}
		n_sent, err := linux.writev(
			cast(linux.Fd)socket,
			[]linux.IO_Vec {
				{base = &request, len = size_of(Request)},
				{base = raw_data(AUTH_ENTRY_MAGIC_COOKIE), len = len(AUTH_ENTRY_MAGIC_COOKIE)},
				{base = raw_data(padding[:]), len = len(padding)},
				{base = raw_data(auth_token[:]), len = len(auth_token)},
			},
		)
		assert(err == .NONE)
		assert(
			n_sent ==
			size_of(Request) + len(AUTH_ENTRY_MAGIC_COOKIE) + len(padding) + len(auth_token),
		)
	}

	StaticResponse :: struct #packed {
		success:       u8,
		pad1:          u8,
		major_version: u16,
		minor_version: u16,
		length:        u16,
	}

	static_response := StaticResponse{}
	{
		n_recv, err := os.recv(socket, mem.ptr_to_bytes(&static_response), 0)
		assert(err == os.ERROR_NONE)
		assert(n_recv == size_of(StaticResponse))
		assert(static_response.success == 1)
	}


	recv_buf: [1 << 15]u8 = {}
	{
		assert(len(recv_buf) >= cast(u32)static_response.length * 4)

		n_recv, err := os.recv(socket, recv_buf[:], 0)
		assert(err == os.ERROR_NONE)
		assert(n_recv == cast(u32)static_response.length * 4)
	}


	DynamicResponse :: struct #packed {
		release_number:              u32,
		resource_id_base:            u32,
		resource_id_mask:            u32,
		motion_buffer_size:          u32,
		vendor_length:               u16,
		maximum_request_length:      u16,
		screens_in_root_count:       u8,
		formats_count:               u8,
		image_byte_order:            u8,
		bitmap_format_bit_order:     u8,
		bitmap_format_scanline_unit: u8,
		bitmap_format_scanline_pad:  u8,
		min_keycode:                 u8,
		max_keycode:                 u8,
		pad2:                        u32,
	}

	read_buffer := bytes.Buffer{}
	bytes.buffer_init(&read_buffer, recv_buf[:])

	dynamic_response := DynamicResponse{}
	{
		n_read, err := bytes.buffer_read(&read_buffer, mem.ptr_to_bytes(&dynamic_response))
		assert(err == .None)
		assert(n_read == size_of(DynamicResponse))
	}


	// Skip over the vendor information.
	bytes.buffer_next(&read_buffer, cast(int)round_up_4(cast(u32)dynamic_response.vendor_length))
	// Skip over the format information (each 8 bytes long).
	bytes.buffer_next(&read_buffer, 8 * cast(int)dynamic_response.formats_count)

	screen := Screen{}
	{
		n_read, err := bytes.buffer_read(&read_buffer, mem.ptr_to_bytes(&screen))
		assert(err == .None)
		assert(n_read == size_of(screen))
	}

	return(
		ConnectionInformation {
			resource_id_base = dynamic_response.resource_id_base,
			resource_id_mask = dynamic_response.resource_id_mask,
			root_screen = screen,
		} \
	)
}
```

Our `main` now becomes:

```odin
main :: proc() {
	auth_token, _ := load_x11_auth_token(context.temp_allocator)
	socket := connect_x11_socket()
	connection_information := x11_handshake(socket, &auth_token)
}
```

The next step is to create a graphical context. When creating a new entity, we generate an id for it, and send that in the create request. Afterwards, we can refer to the entity by this id:

```odin
next_x11_id :: proc(current_id: u32, info: ConnectionInformation) -> u32 {
	return 1 + ((info.resource_id_mask & (current_id)) | info.resource_id_base)
}
```

Time to create a graphical context:


```odin
x11_create_graphical_context :: proc(socket: os.Socket, gc_id: u32, root_id: u32) {
	opcode: u8 : 55
	FLAG_GC_BG: u32 : 8
	BITMASK: u32 : FLAG_GC_BG
	VALUE1: u32 : 0x00_00_ff_00

	Request :: struct #packed {
		opcode:   u8,
		pad1:     u8,
		length:   u16,
		id:       u32,
		drawable: u32,
		bitmask:  u32,
		value1:   u32,
	}
	request := Request {
		opcode   = opcode,
		length   = 5,
		id       = gc_id,
		drawable = root_id,
		bitmask  = BITMASK,
		value1   = VALUE1,
	}

	{
		n_sent, err := os.send(socket, mem.ptr_to_bytes(&request), 0)
		assert(err == os.ERROR_NONE)
		assert(n_sent == size_of(Request))
	}
}
```

Finally we create a window. We subscribe to a few events as well:

- `Exposure`: when our window becomes visible,
- `KEY_PRESS`: when a keyboard key is pressed
- `KEY_RELEASE`: when a keyboard key is released
- `BUTTON_PRESS`: when a mouse button is pressed
- `BUTTON_RELEASE`: when a mouse button is released

We also pick an arbitrary background color, yellow. It does not matter because we will always cover every part of the window with our assets.

```odin
x11_create_window :: proc(
	socket: os.Socket,
	window_id: u32,
	parent_id: u32,
	x: u16,
	y: u16,
	width: u16,
	height: u16,
	root_visual_id: u32,
) {
	FLAG_WIN_BG_PIXEL: u32 : 2
	FLAG_WIN_EVENT: u32 : 0x800
	FLAG_COUNT: u16 : 2
	EVENT_FLAG_EXPOSURE: u32 = 0x80_00
	EVENT_FLAG_KEY_PRESS: u32 = 0x1
	EVENT_FLAG_KEY_RELEASE: u32 = 0x2
	EVENT_FLAG_BUTTON_PRESS: u32 = 0x4
	EVENT_FLAG_BUTTON_RELEASE: u32 = 0x8
	flags: u32 : FLAG_WIN_BG_PIXEL | FLAG_WIN_EVENT
	depth: u8 : 24
	border_width: u16 : 0
	CLASS_INPUT_OUTPUT: u16 : 1
	opcode: u8 : 1
	BACKGROUND_PIXEL_COLOR: u32 : 0x00_ff_ff_00

	Request :: struct #packed {
		opcode:         u8,
		depth:          u8,
		request_length: u16,
		window_id:      u32,
		parent_id:      u32,
		x:              u16,
		y:              u16,
		width:          u16,
		height:         u16,
		border_width:   u16,
		class:          u16,
		root_visual_id: u32,
		bitmask:        u32,
		value1:         u32,
		value2:         u32,
	}
	request := Request {
		opcode         = opcode,
		depth          = depth,
		request_length = 8 + FLAG_COUNT,
		window_id      = window_id,
		parent_id      = parent_id,
		x              = x,
		y              = y,
		width          = width,
		height         = height,
		border_width   = border_width,
		class          = CLASS_INPUT_OUTPUT,
		root_visual_id = root_visual_id,
		bitmask        = flags,
		value1         = BACKGROUND_PIXEL_COLOR,
		value2         = EVENT_FLAG_EXPOSURE | EVENT_FLAG_BUTTON_RELEASE | EVENT_FLAG_BUTTON_PRESS | EVENT_FLAG_KEY_PRESS | EVENT_FLAG_KEY_RELEASE,
	}

	{
		n_sent, err := os.send(socket, mem.ptr_to_bytes(&request), 0)
		assert(err == os.ERROR_NONE)
		assert(n_sent == size_of(Request))
	}
}

```


We decide that our game will have 16 rows and 16 columns, and each asset is 16x16 pixels.

`main` is now:

```odin
ENTITIES_ROW_COUNT :: 16
ENTITIES_COLUMN_COUNT :: 16
ENTITIES_WIDTH :: 16
ENTITIES_HEIGHT :: 16

main :: proc() {
	auth_token, _ := load_x11_auth_token(context.temp_allocator)
	socket := connect_x11_socket()
	connection_information := x11_handshake(socket, &auth_token)

	gc_id := next_x11_id(0, connection_information)
	x11_create_graphical_context(socket, gc_id, connection_information.root_screen.id)

	window_id := next_x11_id(gc_id, connection_information)
	x11_create_window(
		socket,
		window_id,
		connection_information.root_screen.id,
		200,
		200,
		ENTITIES_COLUMN_COUNT * ENTITIES_WIDTH,
		ENTITIES_ROW_COUNT * ENTITIES_HEIGHT,
		connection_information.root_screen.root_visual_id,
	)
}
```

Note that the window dimensions are a hint, they might now be respected, for example in a tiling window manager. We do not handle this case here since the assets are fixed size.

If you have followed along, you will now see.. nothing. That's because we need to tell X11 to show our window with the `map_window` call:

```odin
x11_map_window :: proc(socket: os.Socket, window_id: u32) {
	opcode: u8 : 8

	Request :: struct #packed {
		opcode:         u8,
		pad1:           u8,
		request_length: u16,
		window_id:      u32,
	}
	request := Request {
		opcode         = opcode,
		request_length = 2,
		window_id      = window_id,
	}
	{
		n_sent, err := os.send(socket, mem.ptr_to_bytes(&request), 0)
		assert(err == os.ERROR_NONE)
		assert(n_sent == size_of(Request))
	}

}
```

We now see:

![Empty yellow window](game-x11-empty-background.png)
