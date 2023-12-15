all:
	ls *.md | parallel 'make {/.}.html'
	ls *.dot | parallel 'make {/.}.svg'
	go run feed.go > feed.xml
    

%.svg: %.dot
	dot -T svg $< -o $@

%.html: %.md
	cmark --unsafe $< > $@
