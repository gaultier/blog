.PHONY: check
check:
	# Catch incorrect `an` e.g. `an fox`.
	rg --max-depth=1 '\san\s+[bcdfgjklmnpqrstvwxyz]' -i -t markdown --glob='!todo.md' || true
	# Catch incorrect `a` e.g. `a opening`.
	rg --max-depth=1 '\sa\s+[aei]' -i -t markdown --glob='!todo.md' || true
