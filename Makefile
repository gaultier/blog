all:
	./zig-out/bin/blog gen_all
	ls *.dot | parallel 'make {/.}.svg'
    

%.svg: %.dot
	dot -T svg $< -o $@
