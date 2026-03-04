.PHONY: check
check:
	# Catch incorrect `an` e.g. `an fox`.
	rg --max-depth=1 '\san\s+[bcdfgjklmnpqrstvwxyz]' -i -t markdown --glob='!todo.md' || true
	# Catch incorrect `a` e.g. `a opening`.
	rg --max-depth=1 '\sa\s+[aei]' -i -t markdown --glob='!todo.md' || true
	# Catch code blocks without explicit type.
	rg --max-depth=1 '^[ ]*```[ ]*\n\S' -t markdown --multiline --glob='!todo.md' --glob='!todo.md' || true
	# Avoid mixing `KiB` and `Kib` - prefer the former.
	rg --max-depth=1 '\b[KMGT]ib\b' -t markdown --glob='!todo.md' || true
	# Catch empty first line in code block.
	rg --max-depth=1 '^\s*```\w+\n\n' -t markdown --multiline --glob='!todo.md' || true
	# Catch incorrect casing of `DTrace`.
	rg --max-depth=1 '[^`_/](dt|dT|Dt)race\b' -t markdown --multiline --glob='!todo.md' || true
