all:
	./zig-out/bin/blog
	ls *.dot | parallel 'make {/.}.svg'
	go run feed.go > feed.xml
    

%.svg: %.dot
	dot -T svg $< -o $@
