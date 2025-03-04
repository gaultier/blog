package cmark
import "core:c"

strbuf :: struct {
	mem:   ^rawptr, // FIXME
	ptr:   cstring,
	asize: c.int32_t,
	size:  c.int32_t,
}

heading :: struct {
	level:  c.int,
	setext: bool,
}

footnote :: struct #raw_union {
	ref_ix:    c.int,
	def_count: c.int,
}

node_as :: struct #raw_union {
	heading:         heading,
	html_block_type: c.int,
	literal:         chunk,
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
	extension:           ^rawptr,
	ancestor_extension:  ^rawptr,
	footnote:            footnote,
	parent_footnote_def: ^node,
	as:                  node_as,
}

OPT_UNSAFE: c.int : (1 << 17)
OPT_VALIDATE_UTF8: c.int : (1 << 9)
OPT_FOOTNOTES: c.int : (1 << 13)


NODE_TYPE_PRESENT: u16 : 0x8000
NODE_TYPE_BLOCK: u16 : NODE_TYPE_PRESENT | 0x0000
NODE_TYPE_INLINE: u16 : NODE_TYPE_PRESENT | 0x4000
NODE_TYPE_MASK: u16 : 0xc000
NODE_VALUE_MASK: u16 : 0x3fff

/* Error status */
NODE_NONE: u16 : 0x0000

/* Block */
NODE_DOCUMENT: u16 : NODE_TYPE_BLOCK | 0x0001
NODE_BLOCK_QUOTE: u16 : NODE_TYPE_BLOCK | 0x0002
NODE_LIST: u16 : NODE_TYPE_BLOCK | 0x0003
NODE_ITEM: u16 : NODE_TYPE_BLOCK | 0x0004
NODE_CODE_BLOCK: u16 : NODE_TYPE_BLOCK | 0x0005
NODE_HTML_BLOCK: u16 : NODE_TYPE_BLOCK | 0x0006
NODE_CUSTOM_BLOCK: u16 : NODE_TYPE_BLOCK | 0x0007
NODE_PARAGRAPH: u16 : NODE_TYPE_BLOCK | 0x0008
NODE_HEADING: u16 : NODE_TYPE_BLOCK | 0x0009
NODE_THEMATIC_BREAK: u16 : NODE_TYPE_BLOCK | 0x000a
NODE_FOOTNOTE_DEFINITION: u16 : NODE_TYPE_BLOCK | 0x000b

/* Inline */
NODE_TEXT: u16 : NODE_TYPE_INLINE | 0x0001
NODE_SOFTBREAK: u16 : NODE_TYPE_INLINE | 0x0002
NODE_LINEBREAK: u16 : NODE_TYPE_INLINE | 0x0003
NODE_CODE: u16 : NODE_TYPE_INLINE | 0x0004
NODE_HTML_INLINE: u16 : NODE_TYPE_INLINE | 0x0005
NODE_CUSTOM_INLINE: u16 : NODE_TYPE_INLINE | 0x0006
NODE_EMPH: u16 : NODE_TYPE_INLINE | 0x0007
NODE_STRONG: u16 : NODE_TYPE_INLINE | 0x0008
NODE_LINK: u16 : NODE_TYPE_INLINE | 0x0009
NODE_IMAGE: u16 : NODE_TYPE_INLINE | 0x000a
NODE_FOOTNOTE_REFERENCE: u16 : NODE_TYPE_INLINE | 0x000b

event_type :: enum c.int {
	NONE  = 0,
	DONE  = 1,
	ENTER = 2,
	EXIT  = 3,
}

iter_state :: struct {
	ev_type: event_type,
	node:    ^node,
}
iter :: struct {
	mem:  ^rawptr,
	root: ^node,
	cur:  iter_state,
	next: iter_state,
}

chunk :: struct {
	data:  [^]u8,
	len:   i32,
	alloc: i32,
}

llist :: struct {
	next: ^llist,
	data: ^rawptr,
}

foreign import gfm "system:libcmark-gfm-extensions.a"
@(default_calling_convention = "c", link_prefix = "cmark_gfm_")
foreign gfm {
	core_extensions_ensure_registered :: proc() ---
}

foreign import cmark "system:libcmark-gfm.a"
@(default_calling_convention = "c", link_prefix = "cmark_")
foreign cmark {
	parse_document :: proc(buffer: [^]u8, len: c.uint, options: c.int) -> ^node ---
	render_html :: proc(root: ^node, options: c.int, extensions: ^llist) -> cstring ---
	find_syntax_extension :: proc(name: cstring) -> ^rawptr ---
	parser_new_with_mem :: proc(options: c.int, mem: ^rawptr) -> ^rawptr ---
	parser_attach_syntax_extension :: proc(parser: ^rawptr, extensions: ^rawptr) -> c.int ---
	parser_feed :: proc(parser: ^rawptr, buf: [^]u8, len: c.uint) ---
	parser_finish :: proc(parser: ^rawptr) -> ^node ---
	get_arena_mem_allocator :: proc() -> ^rawptr ---
	arena_reset :: proc() ---
	iter_new :: proc(node: ^node) -> ^iter ---
	iter_next :: proc(iter: ^iter) -> event_type ---
	iter_get_node :: proc(iter: ^iter) -> ^node ---
	node_new_with_mem :: proc(type: c.int, mem: ^rawptr) -> ^node ---
	node_replace :: proc(old_node: ^node, new_node: ^node) -> c.int ---
	node_set_literal :: proc(node: ^node, content: cstring) -> c.int ---
	node_set_string_content :: proc(node: ^node, content: cstring) -> c.int ---
	enable_safety_checks :: proc(enable: bool) ---
}
