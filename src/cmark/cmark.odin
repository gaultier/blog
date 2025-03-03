package cmark
import "core:c"

strbuf :: struct {
	mem:   ^rawptr, // FIXME
	ptr:   cstring,
	asize: c.int32_t,
	size:  c.int32_t,
}

node :: struct {
	content:             strbuf,
	next:                ^node,
	prev:                ^node,
	parent:              ^node,
	first_child:         ^node,
	last_child:          ^node,
	user_data:           ^rawptr,
	user_data_free_func: ^rawptr, // FIXME
	start_line:          c.int,
	start_column:        c.int,
	end_line:            c.int,
	end_column:          c.int,
	internal_offset:     c.int,
	type:                u16,
	flags:               u16,
	extension:           ^rawptr, // FIXME
	ancestor_extension:  ^rawptr, // FIXME

	// TODO: more.
}

OPT_UNSAFE: c.int : (1 << 17)
OPT_VALIDATE_UTF8: c.int : (1 << 9)
OPT_FOOTNOTES: c.int : (1 << 13)

llist :: struct {
	next: ^llist,
	data: ^rawptr,
}

foreign import gfm "system:libcmark-gfm-extensions.a"
@(default_calling_convention = "c", link_prefix = "cmark_gfm_")
foreign gfm {
	core_extensions_ensure_registered:: proc() ---
}

foreign import cmark "system:libcmark-gfm.a"
@(default_calling_convention = "c", link_prefix = "cmark_")
foreign cmark {
	parse_document :: proc(buffer: [^]u8, len: c.uint, options: c.int) -> ^node ---
	render_html :: proc(root: ^node, options: c.int, extensions: ^llist) -> cstring ---
	find_syntax_extension :: proc(name: cstring) -> ^rawptr ---
	llist_append :: proc(mem: ^rawptr, head: ^llist, data: ^rawptr) -> ^llist ---
	get_default_mem_allocator :: proc() -> ^rawptr ---
	parser_new_with_mem :: proc(options: c.int, mem: ^rawptr) -> ^rawptr ---
	parser_attach_syntax_extension :: proc(parser: ^rawptr, extensions: ^rawptr) -> c.int ---
	parser_feed :: proc(parser: ^rawptr, buf: [^]u8, len: c.uint) ---
	parser_finish :: proc(parser: ^rawptr) -> ^node ---
}


